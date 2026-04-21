#pragma once

#include "regatron_state.hpp"

#include <optional>
#include <shared_mutex>

namespace bms
{

    class LatestRegatronState
    {
    public:
        void update(const RegatronStatusSnapshot &snapshot)
        {
            std::unique_lock lock(mutex_);
            snapshot_ = snapshot;
        }

        [[nodiscard]] std::optional<RegatronStatusSnapshot> get() const
        {
            std::shared_lock lock(mutex_);
            return snapshot_;
        }

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