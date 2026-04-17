/**
 * @file temperature.cpp
 * @brief Temperature acquisition cycle implementation for a 16-channel MODBUS source.
 */

#include "temperature.hpp"

#include <boost/chrono.hpp>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace bms
{
    TemperatureAcquisition::TemperatureAcquisition(TemperatureAcquisitionConfig cfg)
        : cfg_(std::move(cfg)), device_(cfg_.device)
    {
    }

    bool TemperatureAcquisition::connect()
    {
        return device_.connect();
    }

    void TemperatureAcquisition::disconnect()
    {
        device_.disconnect();
    }

    float TemperatureAcquisition::decode_channel_(
        const std::array<std::uint16_t, kRegisterBlockCount> &regs,
        std::size_t channel_index) noexcept
    {
        const std::size_t hi = 3 + (2 * channel_index);
        const std::size_t lo = hi + 1;
        return modbus_registers_to_float(regs[hi], regs[lo]);
    }

    std::string TemperatureAcquisition::format_timestamp_(std::chrono::system_clock::time_point tp)
    {
        const auto time = std::chrono::system_clock::to_time_t(tp);
        std::tm utc_tm = *std::gmtime(&time);

        std::ostringstream oss;
        oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%S");

        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                                tp.time_since_epoch())
                                .count() % 1000;

        oss << '.' << std::setw(3) << std::setfill('0') << millis << 'Z';
        return oss.str();
    }

    void TemperatureAcquisition::log_success_(const TemperatureSample &sample)
    {
        const auto [min_it, max_it] = std::minmax_element(sample.temperatures.begin(), sample.temperatures.end());

        std::cout << "[Temperature] seq=" << sample.sequence
                  << " ts=" << format_timestamp_(sample.timestamp)
                  << " temp_ok=1"
                  << " sensors={"
                  << "t1=" << sample.temperatures[0]
                  << ", t8=" << sample.temperatures[7]
                  << ", t9=" << sample.temperatures[8]
                  << ", t16=" << sample.temperatures[15]
                  << "}"
                  << " min_c=" << *min_it
                  << " max_c=" << *max_it
                  << std::endl;
    }

    void TemperatureAcquisition::log_failure_()
    {
        std::cout << "[Temperature] seq=" << sequence_
                  << " temp_ok=0"
                  << " err=\"" << device_.status().last_error << "\""
                  << std::endl;
    }

    void TemperatureAcquisition::log_diagnostics_()
    {
        const auto attempts = diagnostics_.attempts.load();
        const auto successes = diagnostics_.successes.load();
        const auto failures = diagnostics_.failures.load();

        const double success_rate = attempts > 0
                                        ? (100.0 * static_cast<double>(successes) / static_cast<double>(attempts))
                                        : 0.0;

        std::cout << "[Temperature][diag] attempts=" << attempts
                  << " success=" << successes
                  << " failures=" << failures
                  << " success_rate_pct=" << std::fixed << std::setprecision(1) << success_rate
                  << " cycle_ms=" << diagnostics_.last_cycle_duration_ms.load()
                  << std::defaultfloat
                  << std::endl;
    }

    void TemperatureAcquisition::operator()()
    {
        // Measure end-to-end cycle latency for diagnostics.
        const auto cycle_start = boost::chrono::steady_clock::now();

        diagnostics_.attempts.fetch_add(1);

        // Read the canonical register block and decode all channels when successful.
        std::array<std::uint16_t, kRegisterBlockCount> regs{};
        const bool read_ok = device_.read_bms_block(regs);

        if (read_ok)
        {
            TemperatureSample sample;
            sample.timestamp = std::chrono::system_clock::now();
            sample.sequence = sequence_;
            float temp = 0;
            for (std::size_t i = 0; i < kChannelCount; ++i)
            {
                temp = decode_channel_(regs, i); 
                sample.temperatures[i] = temp > 0 ? temp : 0;
            }

            diagnostics_.successes.fetch_add(1);
            if (cfg_.enable_sample_logging)
            {
                log_success_(sample);
            }

            if (on_sample_)
            {
                // Publish decoded sample to downstream fan-out callback.
                on_sample_(sample);
            }
        }
        else
        {
            diagnostics_.failures.fetch_add(1);
            if (cfg_.enable_sample_logging)
            {
                log_failure_();
            }
        }

        // Advance sequence even on failed reads to preserve attempt chronology.
        ++sequence_;

        const auto cycle_end = boost::chrono::steady_clock::now();
        const auto cycle_duration = boost::chrono::duration_cast<boost::chrono::milliseconds>(
            cycle_end - cycle_start);
        diagnostics_.last_cycle_duration_ms.store(cycle_duration.count());

        // Emit summary diagnostics at configured cycle intervals.
        if (cfg_.diagnostics_every_cycles > 0 &&
            (sequence_ % cfg_.diagnostics_every_cycles) == 0)
        {
            log_diagnostics_();
        }
    }

} // namespace bms
