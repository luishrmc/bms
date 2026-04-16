#include "soc.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace bms
{
    SoCTask::SoCTask(SoCTaskConfig cfg, VoltageQueue &voltage_queue, TemperatureQueue &temperature_queue)
        : cfg_(std::move(cfg)), voltage_queue_(voltage_queue), temperature_queue_(temperature_queue)
    {
    }

    void SoCTask::operator()()
    {
        VoltageCurrentSample *vc_ptr = nullptr;
        TemperatureSample *temp_ptr = nullptr;

        while (true)
        {
            bool got_voltage = false;
            if (voltage_queue_.try_pop(vc_ptr))
            {
                got_voltage = true;
            }
            else
            {
                if (voltage_queue_.is_closed() && temperature_queue_.is_closed())
                {
                    break;
                }

                got_voltage = voltage_queue_.wait_for_and_pop(vc_ptr, std::chrono::milliseconds(250));
                if (!got_voltage)
                {
                    while (temperature_queue_.try_pop(temp_ptr))
                    {
                        latest_temperature_ = *temp_ptr;
                        diag_.last_temperature_sequence = temp_ptr->sequence;
                        temperature_queue_.dispose(temp_ptr);
                        temp_ptr = nullptr;
                    }
                    continue;
                }
            }

            while (temperature_queue_.try_pop(temp_ptr))
            {
                latest_temperature_ = *temp_ptr;
                diag_.last_temperature_sequence = temp_ptr->sequence;
                temperature_queue_.dispose(temp_ptr);
                temp_ptr = nullptr;
            }

            if (vc_ptr != nullptr)
            {
                // Process every voltage/current sample in FIFO order and attach the
                // most recent temperature context available at this point.
                diag_.frames_observed += 1;
                diag_.last_voltage_sequence = vc_ptr->sequence;

                if (latest_temperature_.has_value())
                {
                    diag_.frames_with_both_measurements += 1;
                }

                if (cfg_.enable_diagnostics_logging)
                {
                    std::cout << "[SoC][interface] consumed vc_seq=" << vc_ptr->sequence;
                    if (latest_temperature_.has_value())
                    {
                        std::cout << " temp_seq=" << latest_temperature_->sequence;
                    }
                    else
                    {
                        std::cout << " temp_seq=none";
                    }
                    std::cout << std::endl;
                }

                voltage_queue_.dispose(vc_ptr);
                vc_ptr = nullptr;
            }
        }
    }

} // namespace bms
