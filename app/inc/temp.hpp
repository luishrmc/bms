// inc/temp.hpp
#pragma once

#include "batch_structures.hpp"
#include "batch_pool.hpp"
#include "modbus_reader.hpp"

#include <cstdint>
#include <utility>

namespace bms
{

    /**
     * TemperatureAcquisitionConfig - Configuration for single-device temperature acquisition
     *
     * Reuses ModbusTcpConfig to avoid parameter duplication.
     */
    struct TemperatureAcquisitionConfig final
    {
        ModbusTcpConfig device{};

        // If true: publish batches even on read failure (with CommError flag)
        // If false: drop failed batches (release to pool)
        // Default true for error observability in production
        bool push_failed_reads{true};

        // If true: run validate_temperature_batch() after MODBUS read
        // Catches decode errors, timestamp issues, and out-of-range values
        // Default true for production safety (minimal overhead)
        bool enable_validation{true};
    };

    /**
     * TemperatureAcquisition - Periodic temperature reader for single MODBUS/TCP device
     *
     * Template-based producer for PeriodicTask integration:
     *
     *   TemperatureBatchPool pool(256);
     *   SafeQueue<TemperatureBatch> queue(128, pool.disposer());
     *   TemperatureAcquisitionConfig cfg{.device = {.host = "192.168.1.30"}};
     *
     *   TemperatureAcquisition<SafeQueue<TemperatureBatch>> temp_acq(cfg, pool, queue);
     *   PeriodicTask task(boost::chrono::milliseconds(200), std::ref(temp_acq));
     *
     * Queue Contract:
     *   bool push(TemperatureBatch* p) - returns false if full/closed
     *
     * Features:
     *   - Header-only template (consistent with voltage.hpp)
     *   - Optional validation for range/timestamp sanity checking
     *   - Zero-allocation batch management via pool
     *   - Sequence tracking (detects drops)
     */
    template <typename Queue>
    class TemperatureAcquisition final
    {
    public:
        using pool_type = TemperatureBatchPool;
        using pointer = TemperatureBatch *;

        /**
         * Construct temperature acquisition with pool and queue references.
         */
        TemperatureAcquisition(TemperatureAcquisitionConfig cfg, pool_type &pool, Queue &queue)
            : cfg_(std::move(cfg)), pool_(pool), queue_(queue), client_(cfg_.device)
        {
        }

        // Non-copyable (owns MODBUS client)
        TemperatureAcquisition(const TemperatureAcquisition &) = delete;
        TemperatureAcquisition &operator=(const TemperatureAcquisition &) = delete;

        /**
         * Periodic work function - reads device and publishes batch.
         */
        void operator()()
        {
            read_and_publish_();
        }

        /**
         * Explicitly connect device.
         */
        bool connect()
        {
            return client_.connect();
        }

        /**
         * Disconnect device.
         */
        void disconnect()
        {
            client_.disconnect();
        }

        /**
         * Check if device is connected.
         */
        bool is_connected() const noexcept
        {
            return client_.is_connected();
        }

        /**
         * Get MODBUS statistics for device.
         */
        const ModbusStatus &device_status() const noexcept
        {
            return client_.status();
        }

        /**
         * Get total batches successfully published.
         */
        std::uint64_t total_published() const noexcept
        {
            return published_count_;
        }

        /**
         * Get total batches dropped (pool exhaustion or queue full).
         */
        std::uint64_t total_dropped() const noexcept
        {
            return dropped_count_;
        }

        /**
         * Access configuration (diagnostics).
         */
        const TemperatureAcquisitionConfig &config() const noexcept
        {
            return cfg_;
        }

    private:
        /**
         * Read device and publish to queue.
         *
         * Sequence incremented before read (tracks all attempts).
         * Batch initialized deterministically before read.
         * Optional validation catches decode/range/timestamp errors.
         */
        void read_and_publish_()
        {
            // Acquire batch from pool
            pointer batch = pool_.acquire();
            if (!batch)
            {
                dropped_count_++;
                return; // Pool exhausted - skip this cycle
            }

            // Initialize batch metadata (deterministic state)
            batch->flags = SampleFlags::None;
            batch->ts.valid = false;
            batch->seq = seq_++; // Pre-increment (tracks attempts)

            // Perform MODBUS read + population
            SampleFlags flags = client_.read_temperature_batch(*batch);

            // Optional validation (range, timestamp, decode sanity)
            if (cfg_.enable_validation)
            {
                flags = flags | validate_temperature_batch(*batch);
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

        TemperatureAcquisitionConfig cfg_;

        // Shared resources (non-owning references)
        pool_type &pool_;
        Queue &queue_;

        // MODBUS client (single device)
        ModbusTcpClient client_;

        // Sequence number
        std::uint32_t seq_{0};

        // Diagnostics
        std::uint64_t published_count_{0};
        std::uint64_t dropped_count_{0};
    };

} // namespace bms
