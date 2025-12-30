#include "influxdb.hpp"

#include <curl/curl.h>
#include <boost/thread/thread.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/once.hpp>
#include <iostream>
#include <mutex>

namespace bms
{

    // libcurl callbacks
    static size_t discard_callback(void *, size_t size, size_t nmemb, void *)
    {
        return size * nmemb;
    }

    static size_t error_callback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        std::string *error = static_cast<std::string *>(userp);
        error->append(static_cast<char *>(contents), size * nmemb);
        return size * nmemb;
    }

    // Global init (thread-safe singleton using boost::call_once)
    static boost::once_flag curl_init_flag = BOOST_ONCE_INIT;
    static void init_curl_global()
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    static void ensure_curl_global_init()
    {
        boost::call_once(curl_init_flag, init_curl_global);
    }

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

    std::string InfluxHTTPClient::make_write_url_() const
    {
        return cfg_.base_url + "/api/v3/write_lp?db=" + cfg_.database + "&precision=ns";
    }

    std::string InfluxHTTPClient::make_ping_url_() const
    {
        return cfg_.base_url + "/ping";
    }

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
