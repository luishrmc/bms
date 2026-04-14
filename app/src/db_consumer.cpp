/**
 * @file        db_consumer.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Ordered DB polling and fan-out implementation for SoC/SoH scaffold.
 * @version     0.0.1
 * @date        2026-04-12
 */

#include "db_consumer.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/once.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace bms
{
    namespace
    {
        boost::once_flag g_curl_once = BOOST_ONCE_INIT;

        void init_curl_global_once()
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }

        size_t write_body_callback(void *contents, size_t size, size_t nmemb, void *user)
        {
            auto *dst = static_cast<std::string *>(user);
            dst->append(static_cast<const char *>(contents), size * nmemb);
            return size * nmemb;
        }

        std::chrono::system_clock::time_point parse_timestamp_utc(const std::string &iso8601)
        {
            std::tm tm = {};
            std::istringstream input(iso8601);
            input >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (input.fail())
            {
                return std::chrono::system_clock::now();
            }

            const auto parsed = std::chrono::system_clock::from_time_t(timegm(&tm));
            if (input.peek() == '.')
            {
                input.get();
                int millis = 0;
                input >> millis;
                return parsed + std::chrono::milliseconds(millis % 1000);
            }
            return parsed;
        }

        std::vector<float> parse_numeric_array(const nlohmann::json &node)
        {
            std::vector<float> out;
            if (!node.is_array())
            {
                return out;
            }

            out.reserve(node.size());
            for (const auto &v : node)
            {
                if (v.is_number())
                {
                    out.push_back(v.get<float>());
                }
            }
            return out;
        }
    } // namespace

    InfluxTelemetryQueryBackend::InfluxTelemetryQueryBackend(InfluxQueryConfig cfg)
        : cfg_(std::move(cfg))
    {
        boost::call_once(g_curl_once, init_curl_global_once);

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            throw std::runtime_error("Failed to initialize CURL for Influx query backend");
        }

        curl_ = curl;

        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(cfg_.connect_timeout.count()));
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(cfg_.request_timeout.count()));
    }

    InfluxTelemetryQueryBackend::~InfluxTelemetryQueryBackend()
    {
        if (curl_)
        {
            curl_easy_cleanup(static_cast<CURL *>(curl_));
        }
    }

    TelemetryQueryResult InfluxTelemetryQueryBackend::fetch_after(std::uint64_t cursor, std::size_t limit)
    {
        TelemetryQueryResult result;

        const std::size_t bounded_limit = std::max<std::size_t>(1, limit);

        std::ostringstream sql;
        sql << "SELECT "
            << cfg_.cursor_column << ","
            << cfg_.timestamp_column << ","
            << cfg_.voltage_column << ","
            << cfg_.current_column << ","
            << cfg_.temperature_column << ","
            << cfg_.valid_column << ","
            << cfg_.status_column
            << " FROM " << cfg_.table
            << " WHERE " << cfg_.cursor_column << " > " << cursor
            << " ORDER BY " << cfg_.cursor_column << " ASC"
            << " LIMIT " << bounded_limit;

        nlohmann::json payload;
        payload["db"] = cfg_.database;
        payload["sql"] = sql.str();

        std::string response_body;
        curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");

        const std::string auth_header = "Authorization: Bearer " + cfg_.token;
        if (!cfg_.token.empty())
        {
            headers = curl_slist_append(headers, auth_header.c_str());
        }

        CURL *curl = static_cast<CURL *>(curl_);
        const std::string url = cfg_.base_url + "/api/v3/query_sql";
        const std::string body = payload.dump();

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        const CURLcode curl_res = curl_easy_perform(curl);

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);

        if (curl_res != CURLE_OK)
        {
            result.success = false;
            result.transient_failure = true;
            result.error_message = curl_easy_strerror(curl_res);
            return result;
        }

        if (http_code < 200 || http_code >= 300)
        {
            result.success = false;
            result.transient_failure = true;
            result.error_message = "HTTP " + std::to_string(http_code) + " body=" + response_body;
            return result;
        }

        try
        {
            const nlohmann::json parsed = nlohmann::json::parse(response_body);
            if (!parsed.is_array())
            {
                result.success = false;
                result.error_message = "Unexpected query response format (expected JSON array)";
                return result;
            }

            result.rows.reserve(parsed.size());
            for (const auto &node : parsed)
            {
                if (!node.is_object())
                {
                    continue;
                }

                TelemetryRow row;
                row.cursor = node.value(cfg_.cursor_column, static_cast<std::uint64_t>(0));
                row.timestamp = parse_timestamp_utc(node.value(cfg_.timestamp_column, std::string{}));
                row.voltages = parse_numeric_array(node.value(cfg_.voltage_column, nlohmann::json::array()));
                row.current_a = node.value(cfg_.current_column, 0.0F);
                row.temperatures = parse_numeric_array(node.value(cfg_.temperature_column, nlohmann::json::array()));
                row.valid = node.value(cfg_.valid_column, true);
                row.status = node.value(cfg_.status_column, std::string{});

                result.rows.push_back(std::move(row));
            }
        }
        catch (const std::exception &ex)
        {
            result.success = false;
            result.error_message = ex.what();
            return result;
        }

        return result;
    }

    DBConsumerTask::DBConsumerTask(
        DBConsumerConfig cfg,
        ITelemetryQueryBackend &backend,
        RowQueue &soc_queue,
        RowQueue &soh_queue)
        : cfg_(std::move(cfg)), backend_(backend), soc_queue_(soc_queue), soh_queue_(soh_queue),
          cursor_checkpoint_(cfg_.initial_cursor)
    {
        diag_.last_processed_cursor.store(cursor_checkpoint_);
    }

    void DBConsumerTask::operator()()
    {
        TelemetryQueryResult query;

        for (int attempt = 0; attempt <= cfg_.max_retries; ++attempt)
        {
            query = backend_.fetch_after(cursor_checkpoint_, cfg_.query_limit);
            if (query.success)
            {
                break;
            }

            diag_.query_failures.fetch_add(1);
            if (attempt < cfg_.max_retries)
            {
                boost::this_thread::sleep_for(cfg_.retry_delay);
            }
        }

        if (!query.success)
        {
            std::cerr << "[DBConsumer] Query failed after retries: " << query.error_message << std::endl;
            return;
        }

        if (query.rows.empty())
        {
            if (!applied_empty_backoff_)
            {
                applied_empty_backoff_ = true;
                boost::this_thread::sleep_for(cfg_.empty_poll_backoff);
            }
            return;
        }

        applied_empty_backoff_ = false;

        for (const auto &row : query.rows)
        {
            diag_.total_rows_fetched.fetch_add(1);

            if (!accept_ordered_row_(row))
            {
                continue;
            }

            if (!publish_to_both_(row))
            {
                diag_.fanout_failures.fetch_add(1);
                break;
            }

            cursor_checkpoint_ = row.cursor;
            diag_.last_processed_cursor.store(cursor_checkpoint_);
            record_latency_(row);
        }
    }

    bool DBConsumerTask::accept_ordered_row_(const TelemetryRow &row)
    {
        if (row.cursor <= cursor_checkpoint_)
        {
            if (row.cursor < cursor_checkpoint_)
            {
                diag_.out_of_order_rows.fetch_add(1);
            }
            diag_.duplicates_skipped.fetch_add(1);
            return false;
        }

        if (row.cursor > cursor_checkpoint_ + 1)
        {
            diag_.missing_cursor_gaps.fetch_add(1);
            std::cerr << "[DBConsumer] Cursor gap detected: expected " << (cursor_checkpoint_ + 1)
                      << " but received " << row.cursor << std::endl;
        }

        return true;
    }

    bool DBConsumerTask::publish_to_both_(const TelemetryRow &row)
    {
        auto *shared_row = new TelemetryRow(row);

        shared_row->add_ref();
        if (!soc_queue_.push(shared_row))
        {
            shared_row->release();
            std::cerr << "[DBConsumer] Failed to enqueue row for SoC pipeline." << std::endl;
            return false;
        }

        shared_row->add_ref();
        if (!soh_queue_.push(shared_row))
        {
            shared_row->release();
            TelemetryRow *rollback = nullptr;
            if (soc_queue_.try_pop(rollback))
            {
                if (rollback && rollback == shared_row)
                {
                    soc_queue_.dispose(rollback);
                }
                else if (rollback)
                {
                    if (!soc_queue_.push(rollback))
                    {
                        soc_queue_.dispose(rollback);
                    }
                }
            }
            std::cerr << "[DBConsumer] Failed to enqueue row for SoH pipeline." << std::endl;
            return false;
        }

        return true;
    }

    void DBConsumerTask::record_latency_(const TelemetryRow &row)
    {
        const auto now = std::chrono::system_clock::now();
        const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(now - row.timestamp).count();
        diag_.last_latency_ms.store(latency);
    }

} // namespace bms
