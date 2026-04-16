/**
 * @file        influxdb.hpp
 * @brief       Minimal InfluxDB HTTP write client used by DBPublisherTask.
 */

#pragma once

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>

#include <cstdint>
#include <string>

namespace bms
{
    /**
     * @brief Connection and retry settings for InfluxDB HTTP writes.
     */
    struct InfluxDBConfig final
    {
        std::string base_url{"http://influxdb3:8181"};
        std::string database{"battery_data"};
        std::string token{""};

        boost::chrono::milliseconds connect_timeout{1500};
        boost::chrono::milliseconds request_timeout{5000};

        int max_retries{3};
        boost::chrono::milliseconds retry_delay{100};
    };

    /**
     * @brief Minimal libcurl-based client for InfluxDB line protocol writes.
     * @note The class is not thread-safe; use one thread per client instance.
     */
    class InfluxHTTPClient final
    {
    public:
        /**
         * @brief Initializes a reusable HTTP client with static configuration.
         * @param cfg URL, database, token, timeout, and retry settings.
         */
        explicit InfluxHTTPClient(const InfluxDBConfig &cfg);
        ~InfluxHTTPClient();

        InfluxHTTPClient(const InfluxHTTPClient &) = delete;
        InfluxHTTPClient &operator=(const InfluxHTTPClient &) = delete;

        /**
         * @brief Probes server reachability through the write endpoint.
         * @return True when network and HTTP response indicate endpoint availability.
         */
        bool ping() noexcept;
        /**
         * @brief Writes one line protocol payload.
         * @param payload Fully formatted line protocol rows.
         * @param error_out Receives transport/HTTP error details on failure.
         * @return True when write succeeds with HTTP 204.
         */
        bool write_lp(const std::string &payload, std::string &error_out) noexcept;

        std::uint64_t total_posts() const noexcept { return total_posts_.load(); }
        std::uint64_t total_failures() const noexcept { return total_failures_.load(); }
        std::uint64_t total_retries() const noexcept { return total_retries_.load(); }
        int last_http_code() const noexcept { return last_http_code_.load(); }
        const InfluxDBConfig &config() const noexcept { return cfg_; }

    private:
        std::string make_write_url_() const;
        std::string make_ping_url_() const;

        InfluxDBConfig cfg_;
        void *curl_{nullptr};    // CURL*
        void *headers_{nullptr}; // curl_slist*

        boost::atomic<std::uint64_t> total_posts_{0};
        boost::atomic<std::uint64_t> total_failures_{0};
        boost::atomic<std::uint64_t> total_retries_{0};
        boost::atomic<int> last_http_code_{0};
    };

} // namespace bms
