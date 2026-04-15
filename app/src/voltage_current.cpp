#include "voltage_current.hpp"

#include <boost/chrono.hpp>

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

namespace bms
{
    VoltageCurrentAcquisition::VoltageCurrentAcquisition(VoltageCurrentAcquisitionConfig cfg)
        : cfg_(std::move(cfg)), dev1_(cfg_.device1), dev2_(cfg_.device2)
    {
    }

    bool VoltageCurrentAcquisition::connect()
    {
        const bool d1 = dev1_.connect();
        const bool d2 = dev2_.connect();
        return d1 && d2;
    }

    void VoltageCurrentAcquisition::disconnect()
    {
        dev1_.disconnect();
        dev2_.disconnect();
    }

    float VoltageCurrentAcquisition::decode_channel_(
        const std::array<std::uint16_t, kRegisterBlockCount> &regs,
        std::size_t channel_index) noexcept
    {
        const std::size_t hi = 3 + (2 * channel_index);
        const std::size_t lo = hi + 1;
        return modbus_registers_to_float(regs[hi], regs[lo]);
    }

    std::string VoltageCurrentAcquisition::format_timestamp_(std::chrono::system_clock::time_point tp)
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

    void VoltageCurrentAcquisition::log_success_(const VoltageCurrentSample &sample)
    {
        std::cout << "[VoltageCurrent] seq=" << sample.sequence
                  << " ts=" << format_timestamp_(sample.timestamp)
                  << " pair_ok=1"
                  << " current_a=" << sample.current_a
                  << " cells={"
                  << "c1=" << sample.cell_voltages[0]
                  << ", c8=" << sample.cell_voltages[7]
                  << ", c9=" << sample.cell_voltages[8]
                  << ", c15=" << sample.cell_voltages[14]
                  << "}"
                  << std::endl;
    }

    void VoltageCurrentAcquisition::log_failure_(bool dev1_ok, bool dev2_ok)
    {
        std::cout << "[VoltageCurrent] seq=" << sequence_
                  << " pair_ok=0"
                  << " device1_ok=" << (dev1_ok ? 1 : 0)
                  << " device2_ok=" << (dev2_ok ? 1 : 0);

        if (!dev1_ok)
        {
            std::cout << " d1_err=\"" << dev1_.status().last_error << "\"";
        }

        if (!dev2_ok)
        {
            std::cout << " d2_err=\"" << dev2_.status().last_error << "\"";
        }

        std::cout << std::endl;
    }

    void VoltageCurrentAcquisition::log_diagnostics_()
    {
        const auto attempts = diagnostics_.pair_attempts.load();
        const auto successes = diagnostics_.pair_successes.load();
        const auto failures = diagnostics_.pair_failures.load();

        const double success_rate = attempts > 0
                                        ? (100.0 * static_cast<double>(successes) / static_cast<double>(attempts))
                                        : 0.0;

        std::cout << "[VoltageCurrent][diag] attempts=" << attempts
                  << " success=" << successes
                  << " failures=" << failures
                  << " success_rate_pct=" << std::fixed << std::setprecision(1) << success_rate
                  << " d1_ok=" << diagnostics_.device1_successes.load()
                  << " d1_fail=" << diagnostics_.device1_failures.load()
                  << " d2_ok=" << diagnostics_.device2_successes.load()
                  << " d2_fail=" << diagnostics_.device2_failures.load()
                  << " cycle_ms=" << diagnostics_.last_cycle_duration_ms.load()
                  << std::defaultfloat
                  << std::endl;
    }

    void VoltageCurrentAcquisition::operator()()
    {
        const auto cycle_start = boost::chrono::steady_clock::now();

        diagnostics_.pair_attempts.fetch_add(1);

        std::array<std::uint16_t, kRegisterBlockCount> regs1{};
        std::array<std::uint16_t, kRegisterBlockCount> regs2{};

        const bool dev1_ok = dev1_.read_bms_block(regs1);
        if (dev1_ok)
        {
            diagnostics_.device1_successes.fetch_add(1);
        }
        else
        {
            diagnostics_.device1_failures.fetch_add(1);
        }

        const bool dev2_ok = dev2_.read_bms_block(regs2);
        if (dev2_ok)
        {
            diagnostics_.device2_successes.fetch_add(1);
        }
        else
        {
            diagnostics_.device2_failures.fetch_add(1);
        }

        if (dev1_ok && dev2_ok)
        {
            VoltageCurrentSample sample;
            sample.sequence = sequence_;
            sample.timestamp = std::chrono::system_clock::now();

            // Exact mapping required for stage stabilization:
            // Device 1 ch0..7  -> cell1..cell8
            // Device 2 ch0..6  -> cell9..cell15
            // Device 2 ch7     -> current_a
            for (std::size_t i = 0; i < 8; ++i)
            {
                sample.cell_voltages[i] = decode_channel_(regs1, i);
            }
            for (std::size_t i = 0; i < 7; ++i)
            {
                sample.cell_voltages[8 + i] = decode_channel_(regs2, i);
            }
            sample.current_a = decode_channel_(regs2, 7);

            diagnostics_.pair_successes.fetch_add(1);
            log_success_(sample);
        }
        else
        {
            diagnostics_.pair_failures.fetch_add(1);
            log_failure_(dev1_ok, dev2_ok);
        }

        ++sequence_;

        const auto cycle_end = boost::chrono::steady_clock::now();
        const auto cycle_duration = boost::chrono::duration_cast<boost::chrono::milliseconds>(
            cycle_end - cycle_start);
        diagnostics_.last_cycle_duration_ms.store(cycle_duration.count());

        if (cfg_.diagnostics_every_cycles > 0 &&
            (sequence_ % cfg_.diagnostics_every_cycles) == 0)
        {
            log_diagnostics_();
        }
    }

} // namespace bms
