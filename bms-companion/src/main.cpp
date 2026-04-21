#include "latest_battery_state.hpp"
#include "latest_regatron_state.hpp"
#include "mqtt_control_task.hpp"
#include "mqtt_task.hpp"
#include "regatron_command_state.hpp"
#include "regatron_task.hpp"
#include "rs485_task.hpp"

#include <boost/atomic.hpp>
#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <csignal>
#include <cstdlib>
#include <iostream>

namespace
{
    boost::atomic<bool> g_running{true};

    void signal_handler(int)
    {
        std::cout << "\n[Main] Shutdown signal received..." << std::endl;
        g_running = false;
    }
} // namespace

int main(void)
{
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "========================================" << std::endl;
    std::cout << " BMS RS485 + MQTT + Regatron Runtime " << std::endl;
    std::cout << "========================================" << std::endl;

    bms::LatestBatteryState latest_battery_state;
    bms::RegatronCommandState regatron_command_state;
    bms::LatestRegatronState latest_regatron_state;

    bms::RS485Task::Config rs485_task_cfg;
    rs485_task_cfg.rs485.response_timeout_sec = 0;
    rs485_task_cfg.rs485.response_timeout_usec = 300000;
    rs485_task_cfg.rs485.read_start_address = 0;
    rs485_task_cfg.rs485.read_register_count = 125;
    rs485_task_cfg.rs485.current_scale_a_per_lsb = 0.0F;
    rs485_task_cfg.connect_retry_delay_ms = 1000;
    rs485_task_cfg.poll_interval_ms = 1000;
    rs485_task_cfg.print_snapshot = false;

    bms::MQTTTaskConfig battery_mqtt_cfg;
    bms::MQTTControlTaskConfig regatron_mqtt_cfg;
    bms::RegatronTask::Config regatron_cfg;
    regatron_cfg.default_supervision_u_min_v = 44.0F;
    regatron_cfg.default_supervision_u_max_v = 54.2F;
    regatron_cfg.default_supervision_i_min_a = -10.0F;
    regatron_cfg.default_supervision_i_max_a = 10.0F;

    bms::RS485Task rs485_task(rs485_task_cfg, latest_battery_state, g_running);
    bms::MQTTTask battery_mqtt_task(battery_mqtt_cfg, latest_battery_state, g_running);
    bms::MQTTControlTask regatron_mqtt_task(
        regatron_mqtt_cfg,
        regatron_command_state,
        latest_regatron_state,
        g_running);
    bms::RegatronTask regatron_task(
        regatron_cfg,
        regatron_command_state,
        latest_regatron_state,
        latest_battery_state,
        g_running);

    try
    {
        boost::thread rs485_thread(std::ref(rs485_task));
        boost::thread battery_mqtt_thread(std::ref(battery_mqtt_task));
        boost::thread regatron_mqtt_thread(std::ref(regatron_mqtt_task));
        boost::thread regatron_thread(std::ref(regatron_task));

        while (g_running)
        {
            boost::this_thread::sleep_for(boost::chrono::seconds(2));

            const auto reg = latest_regatron_state.get();
            if (reg.has_value())
            {
                std::cout << "[Main] Regatron state=" << bms::to_string(reg->fsm_state)
                          << " U=" << reg->actual_voltage_v
                          << " I=" << reg->actual_current_a
                          << " switch=" << bms::to_string(reg->actual_switch)
                          << " fault=" << (reg->fault_active ? "yes" : "no")
                          << std::endl;
            }
        }

        rs485_thread.join();
        battery_mqtt_thread.join();
        regatron_mqtt_thread.join();
        regatron_thread.join();

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[Main] FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
}