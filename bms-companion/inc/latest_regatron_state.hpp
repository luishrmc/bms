#pragma once

#include "regatron_state.hpp"

#include <optional>
#include <shared_mutex>

namespace bms
{

    /**
     * @brief Thread-safe shared snapshot of Regatron runtime feedback.
     *
     * The Regatron task updates this structure and readers (for example MQTT
     * status publication) consume copies without blocking writers for long.
     */
    class LatestRegatronState
    {
    public:
        /**
         * @brief Stores a new Regatron status snapshot.
         * @param snapshot Snapshot produced by the Regatron CAN/FSM loop.
         */
        void update(const RegatronStatusSnapshot &snapshot)
        {
            std::unique_lock lock(mutex_);
            snapshot_ = snapshot;
        }

        /**
         * @brief Reads the latest Regatron status snapshot.
         * @return Optional snapshot copy.
         */
        [[nodiscard]] std::optional<RegatronStatusSnapshot> get() const
        {
            std::shared_lock lock(mutex_);
            return snapshot_;
        }

        /**
         * @brief Indicates whether a Regatron snapshot has been published yet.
         * @return True when a snapshot is available.
         */
        [[nodiscard]] bool has_value() const
        {
            std::shared_lock lock(mutex_);
            return snapshot_.has_value();
        }

    private:
        mutable std::shared_mutex mutex_;
        std::optional<RegatronStatusSnapshot> snapshot_;
    };

} // namespace bms
