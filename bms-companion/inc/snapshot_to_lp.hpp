#pragma once

#include "battery_snapshot.hpp"

#include <string>

namespace bms
{

    struct SnapshotToLP
    {
        static bool append_battery_snapshot_row(std::string &payload,
                                                const BatterySnapshot &snapshot);
    };

} // namespace bms