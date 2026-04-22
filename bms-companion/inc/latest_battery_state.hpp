#pragma once

#include "battery_snapshot.hpp"

#include <mutex>
#include <optional>

namespace bms
{

    /**
     * @brief Thread-safe single-slot storage for the latest battery snapshot.
     *
     * Writers replace the full snapshot atomically under a mutex. Readers
     * receive a copy to avoid sharing mutable state between tasks.
     */
    class LatestBatteryState
    {
    public:
        /**
         * @brief Replaces the currently stored snapshot.
         * @param snapshot New snapshot value.
         */
        void update(BatterySnapshot snapshot)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            latest_ = std::move(snapshot);
        }

        /**
         * @brief Returns a copy of the stored snapshot, if available.
         * @return Optional snapshot copy.
         */
        std::optional<BatterySnapshot> get_copy() const
        {
            std::lock_guard<std::mutex> lock(mtx_);
            return latest_;
        }

        /**
         * @brief Indicates whether at least one snapshot has been stored.
         * @return True when a snapshot is available.
         */
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
