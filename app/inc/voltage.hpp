#pragma once

#include "batch_structures.hpp"
#include "batch_pool.hpp"
#include "modbus_reader.hpp"

#include <cstdint>
#include <utility>

namespace bms
{

    /**
     * VoltageAcquisitionConfig - Configuration for dual-device voltage acquisition
     *
     * Reuses ModbusTcpConfig for each device to avoid parameter duplication.
     */
    struct VoltageAcquisitionConfig final
    {
        ModbusTcpConfig device1{};
        ModbusTcpConfig device2{};

        // If true: publish batches even on read failure (with CommError flag)
        // If false: drop failed batches (release to pool)
        bool push_failed_reads{true};

        // If true: run validate_voltage_batch() after MODBUS read
        // Catches decode errors, timestamp issues, and out-of-range values
        bool enable_validation{true};
    };

    /**
     * VoltageAcquisition - Periodic voltage reader for two MODBUS/TCP devices
     *
     * Template-based producer for PeriodicTask integration.
     * See temp.hpp for detailed usage pattern.
     *
     * Queue Contract:
     *   bool push(VoltageBatch* p) - returns false if full/closed
     *
     * Thread Safety:
     *   operator() must be called from single thread only (PeriodicTask guarantees this)
     */
    template <typename Queue>
    class VoltageAcquisition final
    {
    public:
        using pool_type = VoltageBatchPool;
        using pointer = VoltageBatch *;

        /**
         * Construct voltage acquisition with pool and queue references.
         */
        VoltageAcquisition(VoltageAcquisitionConfig cfg, pool_type &pool, Queue &queue)
            : cfg_(std::move(cfg)), pool_(pool), queue_(queue), dev1_(cfg_.device1),
              dev2_(cfg_.device2)
        {
        }

        // Non-copyable (owns MODBUS clients)
        VoltageAcquisition(const VoltageAcquisition &) = delete;
        VoltageAcquisition &operator=(const VoltageAcquisition &) = delete;

        /**
         * Periodic work function - reads both devices sequentially.
         *
         * Called by PeriodicTask from single thread.
         * Reads device1, then device2. Each gets independent sequence and device_id.
         */
        void operator()()
        {
            // read_and_publish_(dev1_, device1_seq_, /*device_id=*/1);
            read_and_publish_(dev1_, device1_seq_, /*device_id=*/1);
            read_and_publish_(dev2_, device2_seq_, /*device_id=*/2);
        }

        bool connect()
        {
            const bool d1 = dev1_.connect();
            const bool d2 = dev2_.connect();
            return d1 && d2;
        }

        void disconnect()
        {
            dev1_.disconnect();
            dev2_.disconnect();
        }

        bool is_connected() const noexcept
        {
            return dev1_.is_connected() && dev2_.is_connected();
        }

        const ModbusStatus &device1_status() const noexcept
        {
            return dev1_.status();
        }

        const ModbusStatus &device2_status() const noexcept
        {
            return dev2_.status();
        }

        std::uint64_t total_published() const noexcept
        {
            return published_count_;
        }

        std::uint64_t total_dropped() const noexcept
        {
            return dropped_count_;
        }

        const VoltageAcquisitionConfig &config() const noexcept
        {
            return cfg_;
        }

    private:
        /**
         * Read single device and publish to queue.
         *
         * Sequence incremented before read (tracks all attempts).
         * Optional validation catches decode/range/timestamp errors.
         */
        void read_and_publish_(
            ModbusTcpClient &client,
            std::uint32_t &seq,
            std::uint8_t device_id)
        {
            // Acquire batch from pool
            pointer batch = pool_.acquire();
            if (!batch)
            {
                dropped_count_++;
                return; // Pool exhausted
            }

            // Initialize batch metadata (deterministic state)
            batch->flags = SampleFlags::None;
            batch->ts.valid = false;
            batch->seq = seq++; // Pre-increment (tracks attempts)
            batch->device_id = device_id;

            // Perform MODBUS read + population
            SampleFlags flags = client.read_voltage_batch(*batch);

            // Optional validation (range, timestamp, decode sanity)
            if (cfg_.enable_validation)
            {
                flags = flags | validate_voltage_batch(*batch);
            }

            // Handle read or validation failure
            if (any(flags))
            {
                batch->flags = batch->flags | flags; // Combine flags
                batch->ts.valid = false;

                if (!cfg_.push_failed_reads)
                {
                    pool_.release(batch);
                    dropped_count_++;
                    return; // Drop failed batch
                }
                // Otherwise continue to publish with error flags
            }

            // Publish to queue (transfers ownership)
            if (!queue_.push(batch))
            {
                // Queue full or closed - return to pool
                pool_.release(batch);
                dropped_count_++;
                return;
            }

            published_count_++;
        }

        VoltageAcquisitionConfig cfg_;

        // Shared resources (non-owning references)
        pool_type &pool_;
        Queue &queue_;

        // MODBUS clients (one per device)
        ModbusTcpClient dev1_;
        ModbusTcpClient dev2_;

        // Per-device sequence numbers
        std::uint32_t device1_seq_{0};
        std::uint32_t device2_seq_{0};

        // Diagnostics (modified only by operator() single-thread)
        std::uint64_t published_count_{0};
        std::uint64_t dropped_count_{0};
    };

} // namespace bms
