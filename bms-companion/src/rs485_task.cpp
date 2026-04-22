#include "rs485_task.hpp"

#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <iostream>
#include <string>

namespace bms
{

    /**
     * @brief Creates the RS485 polling loop task.
     * @param cfg Poll and reconnect settings.
     * @param latest_state Shared destination for decoded snapshots.
     * @param running_flag Global lifecycle flag.
     */
    RS485Task::RS485Task(const Config &cfg,
                         LatestBatteryState &latest_state,
                         boost::atomic<bool> &running_flag)
        : cfg_(cfg),
          latest_state_(latest_state),
          running_(running_flag)
    {
    }

    /**
     * @brief Polls the battery continuously and refreshes the latest snapshot.
     * @note On read failure the Modbus connection is dropped and reopened.
     */
    void RS485Task::operator()()
    {
        ModbusCodec codec(cfg_.rs485);

        while (running_)
        {
            if (!codec.is_connected())
            {
                std::cout << "[RS485] Connecting to " << cfg_.rs485.device
                          << " slave=" << cfg_.rs485.slave_id
                          << " baud=" << cfg_.rs485.baudrate << "..." << std::endl;

                if (!codec.connect())
                {
                    std::cerr << "[RS485] Connection failed. Retrying in "
                              << cfg_.connect_retry_delay_ms << " ms" << std::endl;
                    boost::this_thread::sleep_for(
                        boost::chrono::milliseconds(cfg_.connect_retry_delay_ms));
                    continue;
                }

                std::cout << "[RS485] Connected." << std::endl;
            }

            BatterySnapshot snapshot;
            std::string error;

            if (!codec.read_snapshot(snapshot, error))
            {
                std::cerr << "[RS485] Read failed: " << error << std::endl;
                codec.disconnect();
                boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(cfg_.connect_retry_delay_ms));
                continue;
            }

            if (cfg_.print_snapshot)
            {
                snapshot.print(std::cout);
            }

            latest_state_.update(std::move(snapshot));

            boost::this_thread::sleep_for(
                boost::chrono::milliseconds(cfg_.poll_interval_ms));
        }

        codec.disconnect();
        std::cout << "[RS485] Task stopped." << std::endl;
    }

} // namespace bms
