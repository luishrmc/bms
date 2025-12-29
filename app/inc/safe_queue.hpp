#pragma once

#include <boost/lockfree/queue.hpp>
#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace bms
{

    // Default disposal policy: delete the object.
    // If you use an object pool, pass a custom Disposer that returns to the pool.
    template <typename T>
    struct default_disposer
    {
        void operator()(T *p) const noexcept { delete p; }
    };

    /**
     * SafeQueue<T, Disposer>
     *
     * MPSC (multi-producer / single-consumer) lock-free pointer queue for BMS batches.
     *
     * - Stores pointers (T*) to avoid copying large batch payloads.
     * - Fixed capacity (required by boost::lockfree::queue).
     * - Non-blocking push/pop; provides pop_for() with cooperative yielding.
     * - Explicit close() for clean shutdown.
     * - Disposal policy is configurable to support pools safely.
     *
     * Requirements:
     * - T is your payload type (e.g., VoltageBatch or TemperatureBatch).
     * - Producers allocate/acquire T*, fill it, then push().
     * - Consumer pops T*, processes it, then calls dispose(p) or lets caller handle it.
     *
     * Ownership contract:
     * - If push() succeeds, ownership transfers to the queue/consumer.
     * - If push() fails (queue full or closed), the producer still owns the pointer and must dispose it.
     */
    template <typename T, typename Disposer = default_disposer<T>>
    class SafeQueue final
    {
    public:
        using value_type = T;
        using pointer = T *;
        using disposer_type = Disposer;

        explicit SafeQueue(std::size_t capacity = 128, Disposer disposer = Disposer())
            : queue_(capacity),
              disposer_(std::move(disposer)),
              capacity_(capacity)
        {
            // boost::lockfree::queue requires trivially copyable element type.
            static_assert(std::is_trivially_copyable<pointer>::value,
                          "Queue element type must be trivially copyable.");
        }

        SafeQueue(const SafeQueue &) = delete;
        SafeQueue &operator=(const SafeQueue &) = delete;

        ~SafeQueue() noexcept
        {
            // Drain safely using the disposer policy (delete by default, or return to pool).
            pointer p = nullptr;
            while (queue_.pop(p))
            {
                disposer_(p);
            }
        }

        // Close the queue: producers should stop pushing; consumer can drain remaining items.
        void close() noexcept
        {
            closed_.store(true, boost::memory_order_release);
        }

        bool is_closed() const noexcept
        {
            return closed_.load(boost::memory_order_acquire);
        }

        /**
         * Push a batch pointer (non-blocking).
         * @return true if enqueued, false if queue full or closed.
         */
        bool push(pointer p) noexcept
        {
            if (p == nullptr)
            {
                return false;
            }
            if (is_closed())
            {
                dropped_.fetch_add(1, boost::memory_order_relaxed);
                return false;
            }
            if (queue_.push(p))
            {
                pushed_.fetch_add(1, boost::memory_order_relaxed);
                return true;
            }
            dropped_.fetch_add(1, boost::memory_order_relaxed);
            return false;
        }

        /**
         * Pop a batch pointer (non-blocking).
         * @return true if dequeued, false if empty.
         */
        bool try_pop(pointer &out) noexcept
        {
            if (queue_.pop(out))
            {
                popped_.fetch_add(1, boost::memory_order_relaxed);
                return true;
            }
            return false;
        }

        /**
         * Pop with a cooperative timeout (non-blocking queue + yield loop).
         * This avoids a tight spin while still keeping the queue lock-free.
         *
         * @return true if popped within timeout, false otherwise.
         */
        bool pop_for(pointer &out, const boost::chrono::milliseconds &timeout) noexcept
        {
            const auto start = boost::chrono::steady_clock::now();
            while (boost::chrono::steady_clock::now() - start < timeout)
            {
                if (try_pop(out))
                {
                    return true;
                }
                if (is_closed())
                {
                    // If closed and empty, do not wait the entire timeout.
                    return false;
                }
                boost::this_thread::yield();
            }
            return false;
        }

        // Diagnostics (approximate).
        std::uint64_t dropped_count() const noexcept
        {
            return dropped_.load(boost::memory_order_relaxed);
        }

        std::uint64_t total_pushed() const noexcept
        {
            return pushed_.load(boost::memory_order_relaxed);
        }

        std::uint64_t total_popped() const noexcept
        {
            return popped_.load(boost::memory_order_relaxed);
        }

        std::uint64_t approximate_size() const noexcept
        {
            const auto pu = pushed_.load(boost::memory_order_relaxed);
            const auto po = popped_.load(boost::memory_order_relaxed);
            return (pu > po) ? (pu - po) : 0;
        }

        /**
         * Convenience disposal hook for callers who want the queue to define
         * the correct disposal policy (delete vs pool release).
         */
        void dispose(pointer p) noexcept
        {
            if (p != nullptr)
            {
                disposer_(p);
            }
        }

        std::size_t capacity() const noexcept
        {
            // Boost lockfree doesn't expose this directly,
            // so document it or pass as constructor param
            return capacity_;
        }

    private:
        boost::lockfree::queue<pointer> queue_;
        Disposer disposer_;
        std::size_t capacity_;

        boost::atomic<bool> closed_{false};
        boost::atomic<std::uint64_t> dropped_{0};
        boost::atomic<std::uint64_t> pushed_{0};
        boost::atomic<std::uint64_t> popped_{0};
    };

} // namespace bms
