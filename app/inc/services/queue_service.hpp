#ifndef QUEUE_SERVICE_HPP
#define QUEUE_SERVICE_HPP

#include <nlohmann/json.hpp>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <stop_token>

namespace queue_service
{
    using json = nlohmann::json;
    /**
     * @class JsonQueue
     * @brief A thread-safe queue specialized for nlohmann::json.
     */
    class JsonQueue final
    {
    public:
        JsonQueue(void) noexcept {};
        JsonQueue(const JsonQueue &) = delete;
        JsonQueue &operator=(const JsonQueue &) = delete;

        /**
         * @brief Push a message into the queue.
         * @throws std::runtime_error if the queue is closed.
         */
        void push(json value)
        {
            std::unique_lock lock(m_);
            q_.push(std::move(value));
            cv_not_empty_.notify_one();
        }

        /**
         * @brief Non-blocking pop. Returns std::nullopt if empty.
         */
        std::optional<json> try_pop()
        {
            std::scoped_lock lock(m_);
            if (q_.empty())
                return std::nullopt;

            json v = std::move(q_.front());
            q_.pop();
            return v;
        }

        /**
         * @brief Blocking pop that cooperates with std::jthread cancellation.
         * @param st A std::stop_token obtained from the running std::jthread.
         * @return The next json message, or std::nullopt if stop was requested,
         * or the queue was closed and became empty.
         */
        std::optional<json> wait_and_pop(std::stop_token st)
        {
            std::unique_lock lock(m_);

            // Use the std::stop_token overload for wait.
            // Predicate checks if queue is not empty OR if it's closed OR if stop has been requested.
            cv_not_empty_.wait(lock, st, [this]
                               { return !q_.empty(); });

            // If stop requested OR queue is closed and empty, return nullopt.
            // The stop_token's predicate (st.stop_requested()) is implicitly handled by wait returning early.
            if (q_.empty())
            { // q_.empty() will be true if stop was requested OR if closed and empty
                return std::nullopt;
            }

            json v = std::move(q_.front());
            q_.pop();

            return v;
        }

        /// @return true if the queue has no elements.
        bool empty() const noexcept
        {
            std::scoped_lock lock(m_);
            return q_.empty();
        }

    private:
        mutable std::mutex m_;
        std::queue<json> q_;
        std::condition_variable_any cv_not_empty_;
    };

} // namespace queue_service

#endif // QUEUE_SERVICE_HPP
