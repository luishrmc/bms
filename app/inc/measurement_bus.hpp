#pragma once

#include "batch_structures.hpp"

#include <mutex>
#include <optional>

namespace bms
{
    struct MeasurementFrame final
    {
        std::optional<VoltageCurrentSample> voltage_current{};
        std::optional<TemperatureSample> temperature{};
    };

    class MeasurementBus final
    {
    public:
        void publish(const VoltageCurrentSample &sample)
        {
            std::scoped_lock lock(mutex_);
            voltage_current_ = sample;
        }

        void publish(const TemperatureSample &sample)
        {
            std::scoped_lock lock(mutex_);
            temperature_ = sample;
        }

        MeasurementFrame latest() const
        {
            std::scoped_lock lock(mutex_);
            return MeasurementFrame{voltage_current_, temperature_};
        }

    private:
        mutable std::mutex mutex_;
        std::optional<VoltageCurrentSample> voltage_current_{};
        std::optional<TemperatureSample> temperature_{};
    };

} // namespace bms
