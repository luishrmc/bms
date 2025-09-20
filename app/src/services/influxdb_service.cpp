/**
 * @file        influxdb_service.cpp
 * @author      Luis Maciel (luishrm@ufmg.br)
 * @brief       [Short description of the file’s purpose]
 * @version     0.0.1
 * @date        2025-08-23
 *               _   _  _____  __  __   _____
 *              | | | ||  ___||  \/  | / ____|
 *              | | | || |_   | \  / || |  __
 *              | | | ||  _|  | |\/| || | |_ |
 *              | |_| || |    | |  | || |__| |
 *               \___/ |_|    |_|  |_| \_____|
 *
 *            Universidade Federal de Minas Gerais
 *                DELT · BMS Project
 */

// ----------------------------- Includes ----------------------------- //

#include "influxdb_service.hpp"
#include "logging_service.hpp"
#include <iostream>
#include <sstream>

// -------------------------- Private Types --------------------------- //

// -------------------------- Private Defines -------------------------- //

// -------------------------- Private Macros --------------------------- //

// ------------------------ Private Variables -------------------------- //

// ---------------------- Function Prototypes -------------------------- //

// ------------------------- Main Functions ---------------------------- //

InfluxDBService::InfluxDBService(const std::string &host,
                                 int port,
                                 const std::string &token,
                                 const std::string &database)
    : host_(host), port_(port), token_(token), database_(database)
{
    curl_global_init(CURL_GLOBAL_ALL);
    curl_ = curl_easy_init();
    auth_ = "Authorization: Bearer " + token_;
    headers_ = curl_slist_append(headers_, ("Authorization: Bearer " + token_).c_str());
    headers_ = curl_slist_append(headers_, "Content-Type: text/plain; charset=utf-8");
}

InfluxDBService::~InfluxDBService()
{
    if (curl_)
    {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}

bool InfluxDBService::connect()
{
    app_log(LogLevel::Info, fmt::format("Attempting to connect to InfluxDB at {}:{}", host_, port_));

    std::ostringstream url;
    url << "http://" << host_ << ":" << port_ << "/health";

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, auth_.c_str());

    curl_easy_setopt(curl_, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 5L);

    long code = 0;
    CURLcode rc = curl_easy_perform(curl_);
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);

    if (rc != CURLE_OK || code != 200)
    {
        app_log(LogLevel::Error, fmt::format("Failed to connect to InfluxDB: {} (HTTP {})", curl_easy_strerror(rc), code));
        return false;
    }

    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPIDLE, 30L);  // seconds before probes
    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPINTVL, 15L); // seconds between probes
    // Try HTTP/2 if libcurl is built with nghttp2 and server supports it
    curl_easy_setopt(curl_, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    // General timeouts (tune as you prefer)
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    // Thread-safety nicety
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);

    app_log(LogLevel::Info, "HTTP connection to InfluxDB is healthy.");
    return true;
}

bool InfluxDBService::insert(const std::string &lp_line)
{
    std::ostringstream url;
    url << "http://" << host_ << ":" << port_
        << "/api/v3/write_lp?db=" << database_
        << "&precision=nanosecond";

    curl_easy_setopt(curl_, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, lp_line.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(lp_line.size()));

    long code = 0;
    CURLcode rc = curl_easy_perform(curl_);
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &code);

    if (rc != CURLE_OK || code != 204)
    {
        app_log(LogLevel::Error, fmt::format("Insert failed: {} (HTTP {})", curl_easy_strerror(rc), code));
        return false;
    }
    app_log(LogLevel::Info, "Insert successful.");
    return true;
}

bool InfluxDBService::insert_batch(const std::vector<std::string> &lines)
{
    if (lines.empty())
        return true;

    std::string body;
    size_t total = 0;
    for (const auto &s : lines)
        total += s.size() + 1;
    body.reserve(total);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        body.append(lines[i]);
        if (i + 1 < lines.size())
            body.push_back('\n');
    }
    return insert(body);
}

// *********************** END OF FILE ******************************* //
