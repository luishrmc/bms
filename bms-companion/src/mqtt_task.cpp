#include "mqtt_task.hpp"

#include <mqtt/async_client.h>
#include <nlohmann/json.hpp>

#include <boost/thread/thread.hpp>

#include <iostream>

namespace bms
{

    MQTTTask::MQTTTask(const MQTTTaskConfig &cfg,
                       SnapshotQueue &snapshot_queue,
                       boost::atomic<bool> &running_flag)
        : cfg_(cfg),
          snapshot_queue_(snapshot_queue),
          running_(running_flag)
    {
    }

    bool MQTTTask::ensure_connected_(mqtt::async_client &client,
                                     mqtt::connect_options &conn_opts)
    {
        if (client.is_connected())
        {
            return true;
        }

        while (running_)
        {
            try
            {
                diagnostics_.reconnect_attempts += 1;
                std::cout << "[MQTT] Connecting to broker " << cfg_.server_uri << " ..." << std::endl;

                auto tok = client.connect(conn_opts);
                tok->wait();

                std::cout << "[MQTT] Connected." << std::endl;
                diagnostics_.last_error.clear();
                return true;
            }
            catch (const mqtt::exception &e)
            {
                diagnostics_.last_error = e.what();
                std::cerr << "[MQTT] Connect failed: " << e.what() << std::endl;
            }
            catch (const std::exception &e)
            {
                diagnostics_.last_error = e.what();
                std::cerr << "[MQTT] Connect failed: " << e.what() << std::endl;
            }

            boost::this_thread::sleep_for(
                boost::chrono::milliseconds(cfg_.reconnect_delay_ms));
        }

        return false;
    }

    std::string MQTTTask::snapshot_to_json_(const BatterySnapshot &snapshot)
    {
        nlohmann::json j;

        const auto ts_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                snapshot.timestamp.time_since_epoch())
                .count();

        j["timestamp_ms"] = ts_ms;
        j["pack_voltage_v"] = snapshot.pack_voltage_v;

        if (snapshot.current_scaled)
        {
            j["pack_current_a"] = snapshot.pack_current_a;
        }
        else
        {
            j["pack_current_raw"] = static_cast<std::int16_t>(snapshot.raw_registers[1]);
        }

        j["soc_pct"] = snapshot.soc_pct;
        j["soh_pct"] = snapshot.soh_pct;

        j["remaining_capacity_ah"] = snapshot.remaining_capacity_ah;
        j["max_charge_current_a"] = snapshot.max_charge_current_a;

        j["bms_cooling_temp_c"] = snapshot.bms_cooling_temp_c;
        j["battery_internal_temp_c"] = snapshot.battery_internal_temp_c;
        j["max_cell_temp_c"] = snapshot.max_cell_temp_c;

        j["status_raw"] = snapshot.status_raw;
        j["status_text"] = BatterySnapshot::decode_status(snapshot.status_raw);
        j["alarm_raw"] = snapshot.alarm_raw;
        j["protection_raw"] = snapshot.protection_raw;
        j["error_raw"] = snapshot.error_raw;

        j["cycle_count"] = snapshot.cycle_count;
        j["full_charge_capacity_mas"] = snapshot.full_charge_capacity_mas;
        j["cell_count"] = snapshot.cell_count;
        j["full_charge_capacity_ah"] = snapshot.full_charge_capacity_ah;

        j["cell_voltage_mv"] = nlohmann::json::array();
        for (std::size_t i = 0; i < snapshot.cell_voltage_mv.size(); ++i)
        {
            j["cell_voltage_mv"].push_back(snapshot.cell_voltage_mv[i]);
        }

        if (!snapshot.serial_or_model.empty())
        {
            j["serial_or_model"] = snapshot.serial_or_model;
        }
        if (!snapshot.bms_version.empty())
        {
            j["bms_version"] = snapshot.bms_version;
        }
        if (!snapshot.manufacturer.empty())
        {
            j["manufacturer"] = snapshot.manufacturer;
        }

        return j.dump();
    }

    bool MQTTTask::publish_snapshot_(mqtt::async_client &client,
                                     const BatterySnapshot &snapshot)
    {
        try
        {
            const std::string payload = snapshot_to_json_(snapshot);

            auto msg = mqtt::make_message(cfg_.topic_battery_snapshot, payload);
            msg->set_qos(cfg_.qos);
            msg->set_retained(cfg_.retained);

            auto tok = client.publish(msg);
            tok->wait();

            diagnostics_.published_snapshots += 1;
            diagnostics_.last_error.clear();

            return true;
        }
        catch (const mqtt::exception &e)
        {
            diagnostics_.publish_failures += 1;
            diagnostics_.last_error = e.what();
            std::cerr << "[MQTT] Publish failed: " << e.what() << std::endl;
            return false;
        }
        catch (const std::exception &e)
        {
            diagnostics_.publish_failures += 1;
            diagnostics_.last_error = e.what();
            std::cerr << "[MQTT] Publish failed: " << e.what() << std::endl;
            return false;
        }
    }

    void MQTTTask::operator()()
    {
        mqtt::async_client client(cfg_.server_uri, cfg_.client_id);

        mqtt::connect_options conn_opts;
        conn_opts.set_clean_session(true);
        conn_opts.set_automatic_reconnect(false);

        BatterySnapshot *snapshot_ptr = nullptr;

        while (running_)
        {
            if (!ensure_connected_(client, conn_opts))
            {
                break;
            }

            if (!snapshot_queue_.wait_for_and_pop(snapshot_ptr, cfg_.wait_timeout))
            {
                continue;
            }

            if (snapshot_ptr == nullptr)
            {
                continue;
            }

            if (!publish_snapshot_(client, *snapshot_ptr))
            {
                try
                {
                    if (client.is_connected())
                    {
                        client.disconnect()->wait();
                    }
                }
                catch (...)
                {
                }

                snapshot_queue_.dispose(snapshot_ptr);
                snapshot_ptr = nullptr;
                boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(cfg_.reconnect_delay_ms));
                continue;
            }

            snapshot_queue_.dispose(snapshot_ptr);
            snapshot_ptr = nullptr;
        }

        while (snapshot_queue_.try_pop(snapshot_ptr))
        {
            if (snapshot_ptr != nullptr)
            {
                if (ensure_connected_(client, conn_opts))
                {
                    (void)publish_snapshot_(client, *snapshot_ptr);
                }
                snapshot_queue_.dispose(snapshot_ptr);
                snapshot_ptr = nullptr;
            }
        }

        try
        {
            if (client.is_connected())
            {
                client.disconnect()->wait();
            }
        }
        catch (...)
        {
        }

        std::cout << "[MQTT] Task stopped." << std::endl;
    }

} // namespace bms