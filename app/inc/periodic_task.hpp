#pragma once

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>
#include <functional>
#include <utility>
#include <stdexcept>

namespace bms
{
    /**
     * PeriodicTask - Executes a callable at fixed intervals in a dedicated thread.
     *
     * Features:
     * - Drift-free scheduling using boost::chrono::steady_clock
     * - Thread-safe start/stop with atomic flag
     * - Exception handling within task execution
     * - Compatible with SafeQueue::close() for coordinated shutdown
     *
     * Usage:
     *   PeriodicTask task(boost::chrono::milliseconds(100),
     *                     []{ read_sensors(); });
     *   task.start();
     *   // ... work ...
     *   task.stop();  // Signals stop
     *   task.join();  // Waits for clean exit
     */
    class PeriodicTask final
    {
    public:
        using duration = boost::chrono::steady_clock::duration;
        using work_function = std::function<void()>;

        /**
         * @param interval Period between task executions (minimum resolution ~1ms)
         * @param work Callable to execute periodically (should not throw)
         */
        template <typename Callable>
        PeriodicTask(duration interval, Callable &&work)
            : interval_(interval), work_(std::forward<Callable>(work)), stop_requested_(false), started_(false)
        {
            if (interval_.count() <= 0)
            {
                throw std::invalid_argument("PeriodicTask interval must be positive");
            }
        }

        // Non-copyable, non-movable for safety
        PeriodicTask(const PeriodicTask &) = delete;
        PeriodicTask &operator=(const PeriodicTask &) = delete;

        /**
         * Start periodic execution in new thread.
         * Safe to call multiple times (only first call takes effect).
         */
        void start()
        {
            bool expected = false;
            if (!started_.compare_exchange_strong(expected, true,
                                                  boost::memory_order_acq_rel))
            {
                return; // Already started
            }

            stop_requested_.store(false, boost::memory_order_release);
            thread_ = boost::thread(&PeriodicTask::run_loop, this);
        }

        /**
         * Signal periodic task to stop (non-blocking).
         * Call join() afterward to wait for thread completion.
         */
        void stop() noexcept
        {
            stop_requested_.store(true, boost::memory_order_release);
        }

        /**
         * Wait for periodic task thread to complete.
         * Must call stop() first to signal termination.
         */
        void join()
        {
            if (thread_.joinable())
            {
                thread_.join();
            }
        }

        /**
         * Check if stop has been requested (useful for work functions).
         */
        bool should_stop() const noexcept
        {
            return stop_requested_.load(boost::memory_order_acquire);
        }

        /**
         * Get configured interval for diagnostics.
         */
        duration interval() const noexcept
        {
            return interval_;
        }

    private:
        void run_loop()
        {
            using clock = boost::chrono::steady_clock;

            // Initialize next wake time to prevent initial drift
            auto next_wake = clock::now() + interval_;

            while (!should_stop())
            {
                try
                {
                    // Execute work function
                    work_();
                }
                catch (const std::exception &ex)
                {
                    // Log error but continue execution
                    // TODO: integrate with logging system when available
                    (void)ex; // Suppress unused warning for now
                }
                catch (...)
                {
                    // Catch all to prevent thread termination
                }

                // Calculate next wake time (drift-free scheduling)
                next_wake += interval_;
                const auto now = clock::now();

                // Handle case where execution took longer than interval
                if (next_wake < now)
                {
                    // Execution overrun - reschedule immediately but track drift
                    next_wake = now + interval_;
                }
                else
                {
                    // Sleep until next wake time
                    boost::this_thread::sleep_until(next_wake);
                }
            }
        }

        duration interval_;
        work_function work_;

        boost::atomic<bool> stop_requested_;
        boost::atomic<bool> started_;
        boost::thread thread_;
    };

} // namespace bms
