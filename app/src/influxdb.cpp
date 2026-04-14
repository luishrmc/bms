/**
 * @file        influxdb.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       Source file for the BMS data-logger module.
 * @version     0.0.1
 * @date        2026-03-25
 */

#include "influxdb.hpp"
#include "db_consumer.hpp"

#include <curl/curl.h>
#include <boost/thread/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/once.hpp>
#include <iostream>
#include <mutex>

namespace bms
{

    // libcurl callbacks
    /**
 * @brief Discards HTTP response bytes received by libcurl.
 * @param[in] unused Pointer to received data (unused).
 * @param[in] size Element size in bytes.
 * @param[in] nmemb Number of elements received.
 * @param[in] user_data User pointer passed by libcurl (unused).
 * @return Total number of consumed bytes to signal success to libcurl.
 */
static size_t discard_callback(void *, size_t size, size_t nmemb, void *)
    {
        return size * nmemb;
    }

    /**
 * @brief Captures HTTP error-body bytes returned by InfluxDB.
 * @param[in] contents Pointer to received payload chunk.
 * @param[in] size Element size in bytes.
 * @param[in] nmemb Number of elements received.
 * @param[in,out] userp Pointer to std::string accumulating error text.
 * @return Total number of consumed bytes to signal success to libcurl.
 */
static size_t error_callback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        std::string *error = static_cast<std::string *>(userp);
        error->append(static_cast<char *>(contents), size * nmemb);
        return size * nmemb;
    }

    // Global init (thread-safe singleton using boost::call_once)
    static boost::once_flag curl_init_flag = BOOST_ONCE_INIT;
    /**
 * @brief Initializes global libcurl state once per process.
 * @note Thread-safe when called through boost::call_once.
 */
static void init_curl_global()
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    /**
 * @brief Ensures process-wide libcurl initialization has run.
 */
static void ensure_curl_global_init()
    {
        boost::call_once(curl_init_flag, init_curl_global);
    }

    /**
 * @brief Constructs an HTTP client bound to one InfluxDB configuration.
 * @param[in] cfg InfluxDB endpoint, retry and batching configuration values.
 */
InfluxHTTPClient::InfluxHTTPClient(const InfluxDBConfig &cfg)
        : cfg_(cfg)
    {
        ensure_curl_global_init();

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            throw std::runtime_error("Failed to initialize libcurl");
        }

        curl_ = curl;

        // Common options
        long timeout_ms = static_cast<long>(cfg_.request_timeout.count());
        long connect_timeout_ms = static_cast<long>(cfg_.connect_timeout.count());

        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);

        // HTTP headers
        curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: text/plain; charset=utf-8");

        // Authentication
        if (!cfg_.token.empty())
        {
            std::string auth_header = "Authorization: Bearer " + cfg_.token;
            headers = curl_slist_append(headers, auth_header.c_str());
        }

        headers_ = headers;
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    /** @brief Releases libcurl handles owned by this client instance. */
InfluxHTTPClient::~InfluxHTTPClient()
    {
        if (headers_)
        {
            curl_slist_free_all(static_cast<curl_slist *>(headers_));
        }

        if (curl_)
        {
            curl_easy_cleanup(static_cast<CURL *>(curl_));
        }
    }

    /** @brief Builds the InfluxDB write endpoint URL.
 * @return Fully qualified write_lp URL with database and precision query parameters.
 */
std::string InfluxHTTPClient::make_write_url_() const
    {
        return cfg_.base_url + "/api/v3/write_lp?db=" + cfg_.database + "&precision=ns";
    }

    /** @brief Builds the InfluxDB ping endpoint URL.
 * @return Fully qualified /ping URL.
 */
std::string InfluxHTTPClient::make_ping_url_() const
    {
        return cfg_.base_url + "/ping";
    }

    /**
 * @brief Checks server availability via the /ping endpoint.
 * @return True when the server replies with HTTP 204, otherwise false.
 */
bool InfluxHTTPClient::ping() noexcept
    {
        CURL *curl = static_cast<CURL *>(curl_);

        std::string url = make_ping_url_();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

        CURLcode res = curl_easy_perform(curl);

        curl_easy_setopt(curl, CURLOPT_NOBODY, 0L); // Reset

        if (res != CURLE_OK)
        {
            std::cerr << "[InfluxDB] Ping failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }

        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code != 204)
        {
            std::cerr << "[InfluxDB] Ping returned HTTP " << http_code << std::endl;
            return false;
        }

        std::cout << "[InfluxDB] Connected to " << cfg_.base_url << std::endl;
        return true;
    }

    /**
 * @brief Sends newline-delimited Line Protocol payload to InfluxDB 3.
 * @param[in] payload Line Protocol text to post.
 * @param[out] error_out Detailed transport/HTTP error message on failure.
 * @return True when InfluxDB replies HTTP 204, otherwise false.
 */
bool InfluxHTTPClient::write_lp(const std::string &payload, std::string &error_out) noexcept
    {
        CURL *curl = static_cast<CURL *>(curl_);

        std::string url = make_write_url_();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));

        // Error response buffer
        std::string error_response;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &error_response);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, error_callback);

        // Retry loop
        CURLcode res = CURLE_OK;
        for (int attempt = 0; attempt <= cfg_.max_retries; ++attempt)
        {
            res = curl_easy_perform(curl);

            if (res == CURLE_OK)
            {
                break;
            }

            if (attempt < cfg_.max_retries)
            {
                total_retries_.fetch_add(1);
                boost::this_thread::sleep_for(cfg_.retry_delay);
            }
        }

        // Reset write callback
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);

        if (res != CURLE_OK)
        {
            error_out = curl_easy_strerror(res);
            total_failures_.fetch_add(1);
            return false;
        }

        // Check HTTP status
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        last_http_code_.store(static_cast<int>(http_code));

        if (http_code != 204)
        {
            error_out = "HTTP " + std::to_string(http_code) + ": " + error_response;
            total_failures_.fetch_add(1);
            return false;
        }

        total_posts_.fetch_add(1);
        return true;
    }

} // namespace bms

namespace bms
{
ProcessedTelemetryWriterTask::ProcessedTelemetryWriterTask(
    InfluxDBConfig cfg,
    InfluxHTTPClient &client,
    RowQueue &queue)
    : cfg_(std::move(cfg)), client_(client), queue_(queue)
{
}

void ProcessedTelemetryWriterTask::operator()()
{
    TelemetryRow *row = nullptr;
    while (queue_.try_pop(row))
    {
        if (row == nullptr)
        {
            continue;
        }

        append_row_line_(*row);
        queue_.dispose(row);
    }

    if (buffered_lines_ > 0U)
    {
        (void)flush_buffer_();
    }
}

void ProcessedTelemetryWriterTask::append_row_line_(const TelemetryRow &row)
{
    const std::int64_t ts_ns = to_influxdb_ns_(row.timestamp);

    buffer_ += table_name_;
    buffer_ += " ";

    buffer_ += "cursor=";
    append_uint64_(buffer_, row.cursor);
    buffer_ += "u";

    buffer_ += ",current_a=";
    append_float_fixed_(buffer_, row.current_a, cfg_.voltage_precision);

    buffer_ += ",valid=";
    buffer_ += (row.valid ? "true" : "false");

    buffer_ += ",voltages=";
    append_escaped_string_(buffer_, [&]() {
        std::string payload;
        payload.reserve(128);
        append_numeric_array_json_(payload, row.voltages, kMaxVoltages, cfg_.voltage_precision);
        return payload;
    }());

    buffer_ += ",temperatures=";
    append_escaped_string_(buffer_, [&]() {
        std::string payload;
        payload.reserve(128);
        append_numeric_array_json_(payload, row.temperatures, kMaxTemperatures, cfg_.temperature_precision);
        return payload;
    }());

    buffer_ += ",status=";
    append_escaped_string_(buffer_, row.status);

    buffer_ += " ";
    append_int64_(buffer_, ts_ns);
    buffer_ += "\n";

    ++buffered_lines_;
    buffered_bytes_ = buffer_.size();
}

bool ProcessedTelemetryWriterTask::flush_buffer_()
{
    std::string err;
    if (!client_.write_lp(buffer_, err))
    {
        post_failures_.fetch_add(1);
        diag_.write_failures.fetch_add(buffered_lines_);
        last_error_ = std::move(err);
        buffer_.clear();
        buffered_lines_ = 0;
        buffered_bytes_ = 0;
        return false;
    }

    total_posts_.fetch_add(1);
    diag_.rows_written.fetch_add(buffered_lines_);
    buffer_.clear();
    buffered_lines_ = 0;
    buffered_bytes_ = 0;
    return true;
}
} // namespace bms
