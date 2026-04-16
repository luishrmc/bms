/**
 * @file safe_queue.hpp
 * @brief Lock-free pointer queue with close-aware blocking and ownership transfer semantics.
 */

#pragma once

#include <boost/atomic.hpp>
#include <boost/lockfree/queue.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <utility>

namespace bms
{

    /**
     * @brief Default pointer disposer used by @ref SafeQueue.
     * @note Replace with a custom policy for pooled allocation strategies.
     */
    template <typename T>
    struct default_disposer
    {
        void operator()(T *p) const noexcept { delete p; }
    };

    /**
     * @brief MPSC lock-free queue for pointer payloads with shutdown-aware waits.
     * @tparam T Object type stored by pointer.
     * @tparam Disposer Callable used to release pointers after consumption.
     * @details Ownership is transferred to the queue only on successful push.
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
            static_assert(std::is_trivially_copyable<pointer>::value,
                          "Queue element type must be trivially copyable.");
        }

        SafeQueue(const SafeQueue &) = delete;
        SafeQueue &operator=(const SafeQueue &) = delete;

        ~SafeQueue() noexcept
        {
            close();
            pointer p = nullptr;
            while (queue_.pop(p))
            {
                disposer_(p);
            }
        }

        void close() noexcept
        {
            closed_.store(true, boost::memory_order_release);
            cv_not_empty_.notify_all();
            cv_not_full_.notify_all();
        }

        bool is_closed() const noexcept
        {
            return closed_.load(boost::memory_order_acquire);
        }

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
                on_push_success_();
                return true;
            }

            dropped_.fetch_add(1, boost::memory_order_relaxed);
            return false;
        }

        bool push_blocking(pointer p) noexcept
        {
            if (p == nullptr)
            {
                return false;
            }

            for (;;)
            {
                if (is_closed())
                {
                    dropped_.fetch_add(1, boost::memory_order_relaxed);
                    return false;
                }

                if (queue_.push(p))
                {
                    on_push_success_();
                    return true;
                }

                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_not_full_.wait(lock, [this] {
                    return is_closed() || approximate_size() < capacity_;
                });
            }
        }

        template <typename Rep, typename Period>
        bool push_for(pointer p, const std::chrono::duration<Rep, Period> &timeout) noexcept
        {
            if (p == nullptr)
            {
                return false;
            }

            const auto deadline = std::chrono::steady_clock::now() + timeout;
            for (;;)
            {
                if (is_closed())
                {
                    dropped_.fetch_add(1, boost::memory_order_relaxed);
                    return false;
                }

                if (queue_.push(p))
                {
                    on_push_success_();
                    return true;
                }

                std::unique_lock<std::mutex> lock(cv_mutex_);
                if (!cv_not_full_.wait_until(lock, deadline, [this] {
                        return is_closed() || approximate_size() < capacity_;
                    }))
                {
                    dropped_.fetch_add(1, boost::memory_order_relaxed);
                    return false;
                }
            }
        }

        bool try_pop(pointer &out) noexcept
        {
            if (queue_.pop(out))
            {
                popped_.fetch_add(1, boost::memory_order_relaxed);
                cv_not_full_.notify_one();
                return true;
            }
            return false;
        }

        bool wait_and_pop(pointer &out) noexcept
        {
            for (;;)
            {
                if (try_pop(out))
                {
                    return true;
                }

                if (is_closed())
                {
                    return false;
                }

                std::unique_lock<std::mutex> lock(cv_mutex_);
                cv_not_empty_.wait(lock, [this] {
                    return is_closed() || approximate_size() > 0;
                });
            }
        }

        template <typename Rep, typename Period>
        bool wait_for_and_pop(pointer &out, const std::chrono::duration<Rep, Period> &timeout) noexcept
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            for (;;)
            {
                if (try_pop(out))
                {
                    return true;
                }

                if (is_closed())
                {
                    return false;
                }

                std::unique_lock<std::mutex> lock(cv_mutex_);
                if (!cv_not_empty_.wait_until(lock, deadline, [this] {
                        return is_closed() || approximate_size() > 0;
                    }))
                {
                    return try_pop(out);
                }
            }
        }

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

        std::uint64_t peak_size() const noexcept
        {
            return peak_size_.load(boost::memory_order_relaxed);
        }

        void dispose(pointer p) noexcept
        {
            if (p != nullptr)
            {
                disposer_(p);
            }
        }

        std::size_t capacity() const noexcept
        {
            return capacity_;
        }

    private:
        void on_push_success_() noexcept
        {
            const std::uint64_t pushed_now = pushed_.fetch_add(1, boost::memory_order_relaxed) + 1;
            const std::uint64_t popped_now = popped_.load(boost::memory_order_relaxed);
            const std::uint64_t approx_now = (pushed_now > popped_now) ? (pushed_now - popped_now) : 0;
            update_peak_size_(approx_now);
            cv_not_empty_.notify_one();
        }

        void update_peak_size_(std::uint64_t candidate) noexcept
        {
            std::uint64_t current_peak = peak_size_.load(boost::memory_order_relaxed);
            while (candidate > current_peak &&
                   !peak_size_.compare_exchange_weak(
                       current_peak,
                       candidate,
                       boost::memory_order_relaxed,
                       boost::memory_order_relaxed))
            {
            }
        }

        boost::lockfree::queue<pointer> queue_;
        Disposer disposer_;
        std::size_t capacity_;

        mutable std::mutex cv_mutex_;
        std::condition_variable cv_not_empty_;
        std::condition_variable cv_not_full_;

        boost::atomic<bool> closed_{false};
        boost::atomic<std::uint64_t> dropped_{0};
        boost::atomic<std::uint64_t> pushed_{0};
        boost::atomic<std::uint64_t> popped_{0};
        boost::atomic<std::uint64_t> peak_size_{0};
    };

} // namespace bms
