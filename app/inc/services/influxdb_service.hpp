/**
 * @file        influxdb_service.hpp
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

#pragma once

// ----------------------------- Includes ----------------------------- //

#include <string>
#include <time.h>
#include <curl/curl.h>

#include <arrow/flight/api.h>
#include <arrow/flight/client.h>
#include <arrow/flight/sql/api.h>
#include <arrow/flight/sql/client.h>
// -------------------------- Public Types ---------------------------- //

// -------------------------- Public Defines -------------------------- //

// -------------------------- Public Macros --------------------------- //

// ------------------------ Public Functions -------------------------- //

class InfluxDBService
{
public:
    InfluxDBService(const std::string &host,
                    int port,
                    const std::string &token,
                    const std::string &database);
    ~InfluxDBService();
    arrow::Status connect();
    arrow::Status insert(const std::string &lp_line);
    arrow::Status insert_batch(const std::vector<std::string> &lines);

private:
    std::string host_;
    int port_;
    std::string token_;
    std::string database_;
    std::unique_ptr<arrow::flight::sql::FlightSqlClient> sql_client_;

    CURL *curl_;
    std::string auth_;
    struct curl_slist *headers_{nullptr};
};

// *********************** END OF FILE ******************************* //
