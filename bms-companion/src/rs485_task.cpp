#include "rs485_task.hpp"

#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <iostream>

namespace bms
{

    RS485Task::RS485Task(const Config &cfg,
                         SnapshotQueue &output_queue,
                         boost::atomic<bool> &running_flag)
        : cfg_(cfg),
          output_queue_(output_queue),
          running_(running_flag)
    {
    }

    void RS485Task::operator()()
    {
        ModbusCodec codec(cfg_.rs485);

        while (running_)
        {
            // if (!codec.is_connected())
            // {
            //     std::cout << "[RS485] Connecting to " << cfg_.rs485.device
            //               << " slave=" << cfg_.rs485.slave_id
            //               << " baud=" << cfg_.rs485.baudrate << "...\n";

            //     if (!codec.connect())
            //     {
            //         std::cerr << "[RS485] Connection failed. Retrying in "
            //                   << cfg_.connect_retry_delay_ms << " ms\n";
            //         boost::this_thread::sleep_for(
            //             boost::chrono::milliseconds(cfg_.connect_retry_delay_ms));
            //         continue;
            //     }

            //     std::cout << "[RS485] Connected.\n";
            // }

            BatterySnapshot snapshot
            {
                .timestamp = std::chrono::system_clock::now(),
            };

            std::string error;

            // if (!codec.read_snapshot(snapshot, error))
            // {
            //     std::cerr << "[RS485] Read failed: " << error << "\n";
            //     codec.disconnect();
            //     boost::this_thread::sleep_for(
            //         boost::chrono::milliseconds(cfg_.connect_retry_delay_ms));
            //     continue;
            // }

            auto *queued = new BatterySnapshot(std::move(snapshot));
            if (!output_queue_.push_blocking(queued))
            {
                output_queue_.dispose(queued);
                break;
            }

            boost::this_thread::sleep_for(boost::chrono::milliseconds(cfg_.poll_interval_ms));
        }

        codec.disconnect();
        std::cout << "[RS485] Task stopped.\n";
    }

} // namespace bms