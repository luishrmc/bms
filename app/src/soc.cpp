#include "soc.hpp"

#include <iostream>
#include <utility>

namespace bms
{
    SoCTask::SoCTask(SoCTaskConfig cfg, const MeasurementBus &input_bus)
        : cfg_(std::move(cfg)), input_bus_(input_bus)
    {
    }

    void SoCTask::operator()()
    {
        const MeasurementFrame frame = input_bus_.latest();

        if (!frame.voltage_current.has_value() || !frame.temperature.has_value())
        {
            return;
        }

        const auto &vc = frame.voltage_current.value();
        const auto &temp = frame.temperature.value();

        if (vc.sequence == last_seen_voltage_sequence_ &&
            temp.sequence == last_seen_temperature_sequence_)
        {
            return;
        }

        last_seen_voltage_sequence_ = vc.sequence;
        last_seen_temperature_sequence_ = temp.sequence;

        diag_.frames_observed += 1;
        diag_.frames_with_both_measurements += 1;
        diag_.last_voltage_sequence = vc.sequence;
        diag_.last_temperature_sequence = temp.sequence;

        if (cfg_.enable_diagnostics_logging)
        {
            std::cout << "[SoC][interface] received frame vc_seq=" << vc.sequence
                      << " temp_seq=" << temp.sequence << std::endl;
        }
    }

} // namespace bms
