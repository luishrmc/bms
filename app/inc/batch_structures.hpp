// inc/batch_structures.hpp
#pragma once

#include <array>
#include <cstdint>
#include <cmath> // std::isfinite
#include <chrono>
#include <cstring> // std::memcpy
#include <ctime>   // std::time_t

namespace bms
{

    // ============================================================================
    // MODBUS Register Map Constants
    // ============================================================================

    // All MODBUS constants centralized here for single source of truth
    inline constexpr int kModbusStartAddr = 3;             // First register address
    inline constexpr std::size_t kRegisterBlockCount = 35; // Registers 3-37 (35 total)
    inline constexpr std::size_t kChannelCount = 16;       // Voltage/temperature channels

    // Reference epoch: Jan 1, 2000 00:00:00 UTC (device timestamp base)
    inline constexpr std::time_t kUnixEpoch2000 = 946684800;

    // ============================================================================
    // Diagnostic Flags
    // ============================================================================

    enum class SampleFlags : std::uint32_t
    {
        None = 0u,
        CommError = 1u << 0,        // MODBUS read failed
        TimestampInvalid = 1u << 1, // Device timestamp unreasonable
        DecodeError = 1u << 2,      // Float decode produced NaN/Inf
        RangeError = 1u << 3        // Value outside physical limits
    };

    inline constexpr SampleFlags operator|(SampleFlags a, SampleFlags b) noexcept
    {
        return static_cast<SampleFlags>(
            static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    inline constexpr SampleFlags operator&(SampleFlags a, SampleFlags b) noexcept
    {
        return static_cast<SampleFlags>(
            static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }

    inline constexpr bool any(SampleFlags f) noexcept
    {
        return static_cast<std::uint32_t>(f) != 0u;
    }

    // ============================================================================
    // Device Timestamp Structure
    // ============================================================================

    struct DeviceTimestamp final
    {
        std::uint32_t device_epoch{0};  // Seconds since 2000-01-01
        std::uint16_t subseconds_ms{0}; // Millisecond subsecond
        std::chrono::system_clock::time_point timestamp{};
        bool valid{false};
    };

    // ============================================================================
    // Batch Structures
    // ============================================================================

    struct VoltageBatch final
    {
        DeviceTimestamp ts{};
        std::array<float, kChannelCount> voltages{};
        std::uint32_t seq{0};
        SampleFlags flags{SampleFlags::None};
    };

    struct TemperatureBatch final
    {
        DeviceTimestamp ts{};
        std::array<float, kChannelCount> temperatures{};
        std::uint32_t seq{0};
        SampleFlags flags{SampleFlags::None};
    };

    // ============================================================================
    // Timestamp Conversion
    // ============================================================================

    inline std::chrono::system_clock::time_point device_epoch_to_timepoint(
        std::uint32_t device_epoch_seconds,
        std::uint16_t subseconds_ms) noexcept
    {
        const auto base = std::chrono::system_clock::from_time_t(kUnixEpoch2000);
        return base + std::chrono::seconds(device_epoch_seconds) + std::chrono::milliseconds(subseconds_ms);
    }

    inline std::int64_t to_influxdb_ns(std::chrono::system_clock::time_point tp) noexcept
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(tp.time_since_epoch()).count();
    }

    // ============================================================================
    // MODBUS Float Decoding
    // ============================================================================

    inline float modbus_registers_to_float(
        std::uint16_t high_word,
        std::uint16_t low_word) noexcept
    {
        const std::uint32_t raw =
            (static_cast<std::uint32_t>(high_word) << 16) |
            static_cast<std::uint32_t>(low_word);

        float result;
        std::memcpy(&result, &raw, sizeof(float));
        return result;
    }

    // ============================================================================
    // Batch Population from MODBUS (TYPE-SAFE VERSION)
    // ============================================================================

    /**
     * Populate VoltageBatch from 35-register array.
     * Type-safe: enforces correct array size at compile time.
     */
    inline void populate_voltage_batch(
        VoltageBatch &batch,
        const std::array<std::uint16_t, kRegisterBlockCount> &regs) noexcept
    {
        // Extract timestamp (registers 0-2)
        batch.ts.device_epoch =
            (static_cast<std::uint32_t>(regs[0]) << 16) | regs[1];
        batch.ts.subseconds_ms = regs[2];
        batch.ts.timestamp = device_epoch_to_timepoint(
            batch.ts.device_epoch,
            batch.ts.subseconds_ms);
        batch.ts.valid = true;

        // Extract 16 voltage channels (registers 3-34)
        for (std::size_t i = 0; i < kChannelCount; ++i)
        {
            const std::uint16_t hi = regs[3 + 2 * i];
            const std::uint16_t lo = regs[3 + 2 * i + 1];
            batch.voltages[i] = modbus_registers_to_float(hi, lo);
        }
    }

    /**
     * Populate TemperatureBatch from 35-register array.
     */
    inline void populate_temperature_batch(
        TemperatureBatch &batch,
        const std::array<std::uint16_t, kRegisterBlockCount> &regs) noexcept
    {
        batch.ts.device_epoch =
            (static_cast<std::uint32_t>(regs[0]) << 16) | regs[1];
        batch.ts.subseconds_ms = regs[2];
        batch.ts.timestamp = device_epoch_to_timepoint(
            batch.ts.device_epoch,
            batch.ts.subseconds_ms);
        batch.ts.valid = true;

        for (std::size_t i = 0; i < kChannelCount; ++i)
        {
            const std::uint16_t hi = regs[3 + 2 * i];
            const std::uint16_t lo = regs[3 + 2 * i + 1];
            batch.temperatures[i] = modbus_registers_to_float(hi, lo);
        }
    }

    // ============================================================================
    // Validation Utilities
    // ============================================================================

    inline bool is_timestamp_reasonable(
        const std::chrono::system_clock::time_point &tp) noexcept
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto diff = duration_cast<hours>(now > tp ? now - tp : tp - now);
        constexpr auto one_year = hours(24 * 365);
        return diff < one_year;
    }

    inline SampleFlags validate_voltage_batch(const VoltageBatch &batch) noexcept
    {
        SampleFlags result = SampleFlags::None;

        if (any(batch.flags))
        {
            return batch.flags;
        }

        if (!batch.ts.valid || !is_timestamp_reasonable(batch.ts.timestamp))
        {
            result = result | SampleFlags::TimestampInvalid;
        }

        // Voltage limits (adjust for battery chemistry)
        constexpr float min_voltage = 2.0f;
        constexpr float max_voltage = 4.5f;

        for (float v : batch.voltages)
        {
            if (!std::isfinite(v))
            {
                result = result | SampleFlags::DecodeError;
            }
            else if (v < min_voltage || v > max_voltage)
            {
                result = result | SampleFlags::RangeError;
            }
        }

        return result;
    }

    inline SampleFlags validate_temperature_batch(const TemperatureBatch &batch) noexcept
    {
        SampleFlags result = SampleFlags::None;

        if (any(batch.flags))
        {
            return batch.flags;
        }

        if (!batch.ts.valid || !is_timestamp_reasonable(batch.ts.timestamp))
        {
            result = result | SampleFlags::TimestampInvalid;
        }

        // Battery operating range: -40°C to +85°C
        constexpr float min_temp = -50.0f;
        constexpr float max_temp = 100.0f;

        for (float t : batch.temperatures)
        {
            if (!std::isfinite(t))
            {
                result = result | SampleFlags::DecodeError;
            }
            else if (t < min_temp || t > max_temp)
            {
                result = result | SampleFlags::RangeError;
            }
        }

        return result;
    }

} // namespace bms
