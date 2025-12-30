#pragma once

#include "batch_structures.hpp"

#include <boost/lockfree/stack.hpp>
#include <boost/atomic.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>

namespace bms
{

    /**
     * BatchPool - Lock-free object pool for batch allocation
     *
     * Thread-safe pool using boost::lockfree::stack for wait-free acquire/release.
     * Preallocates objects to eliminate runtime allocation overhead.
     *
     * Features:
     * - Bounded memory (preallocated capacity)
     * - Optional heap fallback (disabled by default for determinism)
     * - Safe shutdown (avoids UAF by leaking checked-out objects)
     * - RAII handles via unique_ptr with custom deleter
     * - SafeQueue disposer integration
     *
     * Shutdown sequence (required for safety):
     * 1. Stop producer threads (PeriodicTask::stop())
     * 2. Close queues (SafeQueue::close())
     * 3. Join threads (PeriodicTask::join())
     * 4. Drain consumer queue (process remaining batches)
     * 5. Destroy pool (BatchPool destructor)
     *
     * This ensures no objects are checked out when pool is destroyed.
     */
    template <typename T>
    class BatchPool final
    {
    public:
        using value_type = T;
        using pointer = T *;

        // Deleter for SafeQueue integration (std::function wrapper)
        using Deleter = std::function<void(pointer)>;

        // Deleter struct for unique_ptr (RAII handles)
        struct UniquePtrDeleter
        {
            BatchPool *pool{nullptr};

            void operator()(pointer p) const noexcept
            {
                if (pool && p)
                {
                    pool->release(p);
                }
            }
        };

        using unique_ptr = std::unique_ptr<T, UniquePtrDeleter>;

        /**
         * Construct pool with preallocated capacity.
         *
         * @param capacity Number of objects to preallocate
         * @param allow_heap_fallback If true, acquire() allocates from heap when pool exhausted
         *                            Default false maintains real-time determinism
         *
         * Uses std::nothrow to handle allocation failures gracefully.
         * If preallocation fails, pool operates with reduced capacity.
         */
        explicit BatchPool(std::size_t capacity = 256, bool allow_heap_fallback = false)
            : free_(capacity), capacity_(capacity), allow_heap_(allow_heap_fallback)
        {
            // Allocate ownership table
            owned_ = std::make_unique<pointer[]>(capacity_);
            if (!owned_)
            {
                capacity_ = 0;
                return;
            }

            // Preallocate objects individually (robust to partial failure)
            for (std::size_t i = 0; i < capacity_; ++i)
            {
                pointer obj = new (std::nothrow) T();
                if (!obj)
                {
                    // Continue with reduced capacity
                    owned_[i] = nullptr;
                    allocation_failures_.fetch_add(1, boost::memory_order_relaxed);
                    continue;
                }

                owned_[i] = obj;
                preallocated_.fetch_add(1, boost::memory_order_relaxed);

                // Push to free stack
                if (free_.push(obj))
                {
                    in_pool_.fetch_add(1, boost::memory_order_relaxed);
                }
                else
                {
                    // Stack full (shouldn't happen), but object still owned
                    push_failures_.fetch_add(1, boost::memory_order_relaxed);
                }
            }
        }

        BatchPool(const BatchPool &) = delete;
        BatchPool &operator=(const BatchPool &) = delete;

        ~BatchPool() noexcept
        {
            // Safe shutdown: only delete objects not checked out
            // Drain free stack
            pointer p = nullptr;
            while (free_.pop(p))
            {
                in_pool_.fetch_sub(1, boost::memory_order_relaxed);
            }

            // Delete owned objects only if none are in use
            const auto in_use = in_use_count();
            if (in_use == 0)
            {
                // Safe to delete all preallocated objects
                for (std::size_t i = 0; i < capacity_; ++i)
                {
                    delete owned_[i];
                    owned_[i] = nullptr;
                }
            }
            else
            {
                // Objects still checked out - leak them to prevent UAF
                // This indicates incorrect shutdown sequence (threads not joined)
                leaked_on_shutdown_.store(in_use, boost::memory_order_relaxed);
            }
        }

        /**
         * Acquire object from pool (non-blocking).
         *
         * Returns pooled object if available, otherwise:
         * - If heap fallback disabled: returns nullptr
         * - If heap fallback enabled: allocates from heap
         *
         * @return Pointer to batch object, or nullptr on failure
         */
        pointer acquire() noexcept
        {
            pointer obj = nullptr;

            // Try to pop from free stack
            if (free_.pop(obj))
            {
                in_pool_.fetch_sub(1, boost::memory_order_relaxed);
                acquired_.fetch_add(1, boost::memory_order_relaxed);
                reset_batch(obj);
                return obj;
            }

            // Pool exhausted
            allocation_failures_.fetch_add(1, boost::memory_order_relaxed);

            if (!allow_heap_)
            {
                return nullptr;
            }

            // Heap fallback (non-deterministic)
            obj = new (std::nothrow) T();
            if (!obj)
            {
                heap_failures_.fetch_add(1, boost::memory_order_relaxed);
                return nullptr;
            }

            heap_allocs_.fetch_add(1, boost::memory_order_relaxed);
            acquired_.fetch_add(1, boost::memory_order_relaxed);
            reset_batch(obj);
            return obj;
        }

        /**
         * Acquire with RAII handle.
         *
         * @return unique_ptr that automatically releases on scope exit
         */
        unique_ptr acquire_unique() noexcept
        {
            return unique_ptr(acquire(), UniquePtrDeleter{this});
        }

        /**
         * Release object back to pool (non-blocking).
         *
         * Returns object to free stack for reuse. If stack is full,
         * heap-allocated objects are deleted; pool objects are leaked
         * (accounted in release_failures).
         *
         * @param obj Pointer previously acquired from this pool
         */
        void release(pointer obj) noexcept
        {
            if (!obj)
            {
                return;
            }

            // Try to return to pool
            if (free_.push(obj))
            {
                released_.fetch_add(1, boost::memory_order_relaxed);
                in_pool_.fetch_add(1, boost::memory_order_relaxed);
                return;
            }

            // Stack full (shouldn't happen with correct accounting)
            release_failures_.fetch_add(1, boost::memory_order_relaxed);

            // Delete heap-allocated objects only
            if (!is_preallocated(obj))
            {
                delete obj;
                deletes_.fetch_add(1, boost::memory_order_relaxed);
            }
            // Preallocated objects remain alive but temporarily unreachable
        }

        /**
         * Create disposer functor for SafeQueue integration.
         *
         * Returns a std::function that matches the Deleter type alias.
         * This ensures type compatibility across translation units.
         *
         * Usage:
         *   BatchPool<VoltageBatch> pool(256);
         *   SafeQueue<VoltageBatch, BatchPool<VoltageBatch>::Deleter>
         *       queue(128, pool.disposer());
         */
        Deleter disposer() noexcept
        {
            return [this](pointer obj) noexcept
            {
                this->release(obj);
            };
        }

        // ========================================================================
        // Diagnostics
        // ========================================================================

        std::size_t capacity() const noexcept
        {
            return capacity_;
        }

        std::uint64_t preallocated() const noexcept
        {
            return preallocated_.load(boost::memory_order_relaxed);
        }

        std::uint64_t total_acquired() const noexcept
        {
            return acquired_.load(boost::memory_order_relaxed);
        }

        std::uint64_t total_released() const noexcept
        {
            return released_.load(boost::memory_order_relaxed);
        }

        std::uint64_t in_pool() const noexcept
        {
            return in_pool_.load(boost::memory_order_relaxed);
        }

        std::uint64_t in_use_count() const noexcept
        {
            const auto acq = acquired_.load(boost::memory_order_relaxed);
            const auto rel = released_.load(boost::memory_order_relaxed);
            return (acq > rel) ? (acq - rel) : 0;
        }

        std::uint64_t allocation_failures() const noexcept
        {
            return allocation_failures_.load(boost::memory_order_relaxed);
        }

        std::uint64_t heap_allocations() const noexcept
        {
            return heap_allocs_.load(boost::memory_order_relaxed);
        }

        std::uint64_t leaked_on_shutdown() const noexcept
        {
            return leaked_on_shutdown_.load(boost::memory_order_relaxed);
        }

    private:
        /**
         * Reset batch to clean state before returning from acquire().
         */
        void reset_batch(pointer batch) noexcept
        {
            batch->ts.valid = false;
            batch->flags = SampleFlags::None;
            // seq managed by producer
            // Data arrays will be overwritten
        }

        /**
         * Check if object is preallocated (vs heap-allocated).
         * Used in release() to decide whether to delete.
         */
        bool is_preallocated(pointer p) const noexcept
        {
            if (!owned_ || capacity_ == 0)
            {
                return false;
            }

            // Linear search (acceptable since only called on release failure path)
            for (std::size_t i = 0; i < capacity_; ++i)
            {
                if (owned_[i] == p)
                {
                    return true;
                }
            }

            return false;
        }

        // Lock-free free list
        boost::lockfree::stack<pointer> free_;

        // Configuration
        std::size_t capacity_;
        bool allow_heap_;

        // Ownership table for preallocated objects
        std::unique_ptr<pointer[]> owned_;

        // Diagnostic counters (relaxed ordering)
        boost::atomic<std::uint64_t> preallocated_{0};
        boost::atomic<std::uint64_t> acquired_{0};
        boost::atomic<std::uint64_t> released_{0};
        boost::atomic<std::uint64_t> in_pool_{0};
        boost::atomic<std::uint64_t> allocation_failures_{0};
        boost::atomic<std::uint64_t> heap_allocs_{0};
        boost::atomic<std::uint64_t> heap_failures_{0};
        boost::atomic<std::uint64_t> deletes_{0};
        boost::atomic<std::uint64_t> push_failures_{0};
        boost::atomic<std::uint64_t> release_failures_{0};
        boost::atomic<std::uint64_t> leaked_on_shutdown_{0};
    };

    // ============================================================================
    // Convenience Aliases
    // ============================================================================

    using VoltageBatchPool = BatchPool<VoltageBatch>;
    using TemperatureBatchPool = BatchPool<TemperatureBatch>;

} // namespace bms
