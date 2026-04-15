#pragma once

#include "influxdb.hpp"
#include "measurement_bus.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace bms
{
    struct DBPublisherDiagnostics final
    {
        std::uint64_t voltage_rows_written{0};
        std::uint64_t temperature_rows_written{0};
        std::uint64_t write_failures{0};
        std::string last_error{};
    };

    class DBPublisherTask final
    {
    public:
        DBPublisherTask(InfluxHTTPClient &client, const MeasurementBus &bus)
            : client_(client), bus_(bus)
        {
        }

        void operator()();
        const DBPublisherDiagnostics &diagnostics() const noexcept { return diagnostics_; }

    private:
        bool publish_voltage_current_(const VoltageCurrentSample &sample);
        bool publish_temperature_(const TemperatureSample &sample);

        InfluxHTTPClient &client_;
        const MeasurementBus &bus_;

        std::optional<std::uint64_t> last_voltage_sequence_{};
        std::optional<std::uint64_t> last_temperature_sequence_{};
        DBPublisherDiagnostics diagnostics_{};
    };

} // namespace bms
