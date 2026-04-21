#pragma once

#include "battery_snapshot.hpp"

#include <mutex>
#include <optional>

namespace bms
{

    class LatestBatteryState
    {
    public:
        void update(BatterySnapshot snapshot)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            latest_ = std::move(snapshot);
        }

        std::optional<BatterySnapshot> get_copy() const
        {
            std::lock_guard<std::mutex> lock(mtx_);
            return latest_;
        }

        bool has_value() const
        {
            std::lock_guard<std::mutex> lock(mtx_);
            return latest_.has_value();
        }

    private:
        mutable std::mutex mtx_;
        std::optional<BatterySnapshot> latest_;
    };

} // namespace bms