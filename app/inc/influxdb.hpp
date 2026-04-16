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

    class InfluxHTTPClient final
    {
    public:
        explicit InfluxHTTPClient(const InfluxDBConfig &cfg);
        ~InfluxHTTPClient();

        InfluxHTTPClient(const InfluxHTTPClient &) = delete;
        InfluxHTTPClient &operator=(const InfluxHTTPClient &) = delete;

        bool ping() noexcept;
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
