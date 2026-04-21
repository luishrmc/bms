#include "regatron_task.hpp"

#include <boost/chrono.hpp>
#include <boost/thread/thread.hpp>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace bms
{
    namespace
    {

        constexpr canid_t ID_ACT_U_I = 161;
        constexpr canid_t ID_SET_U_I = 177;
        constexpr canid_t ID_SET_SWITCH_CTRL = 193;
        constexpr canid_t ID_ACT_PWR_SWITCH_CTRL = 225;
        constexpr canid_t ID_ACT_SET_U_I = 264;
        constexpr canid_t ID_SET_SUPV_MX = 276;
        constexpr canid_t ID_SET_SUPV_MN = 308;
        constexpr canid_t ID_SET_SLOPE = 324;
        constexpr canid_t ID_SET_LIM_MX = 352;
        constexpr canid_t ID_SET_LIM_MN = 368;
        constexpr canid_t ID_ACT_FAULT = 1032;
        constexpr canid_t ID_SET_VIRTUAL_IN = 543;
        constexpr canid_t ID_CLEARANCE = 1824;
        constexpr canid_t ID_SET_CYCLE_TIME = 1855;

        std::uint16_t clamp_u16_scaled_0p1(float value)
        {
            const auto raw = static_cast<int>(std::lround(std::max(0.0F, value) * 10.0F));
            return static_cast<std::uint16_t>(std::clamp(raw, 0, 65535));
        }

        std::int32_t clamp_s24_scaled_0p1(float value)
        {
            const auto raw = static_cast<int>(std::lround(value * 10.0F));
            return std::clamp(raw, -8388608, 8388607);
        }

        std::array<std::uint8_t, 8> make_empty8_()
        {
            return {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
        }

        void put_u16_le_(std::array<std::uint8_t, 8> &d, int pos, std::uint16_t v)
        {
            d[static_cast<std::size_t>(pos + 0)] = static_cast<std::uint8_t>(v & 0xFFU);
            d[static_cast<std::size_t>(pos + 1)] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
        }

        void put_s24_le_(std::array<std::uint8_t, 8> &d, int pos, std::int32_t v)
        {
            const std::uint32_t u = static_cast<std::uint32_t>(v) & 0xFFFFFFU;
            d[static_cast<std::size_t>(pos + 0)] = static_cast<std::uint8_t>(u & 0xFFU);
            d[static_cast<std::size_t>(pos + 1)] = static_cast<std::uint8_t>((u >> 8) & 0xFFU);
            d[static_cast<std::size_t>(pos + 2)] = static_cast<std::uint8_t>((u >> 16) & 0xFFU);
        }

        void put_s32_le_(std::array<std::uint8_t, 8> &d, int pos, std::int32_t v)
        {
            const std::uint32_t u = static_cast<std::uint32_t>(v);
            d[static_cast<std::size_t>(pos + 0)] = static_cast<std::uint8_t>(u & 0xFFU);
            d[static_cast<std::size_t>(pos + 1)] = static_cast<std::uint8_t>((u >> 8) & 0xFFU);
            d[static_cast<std::size_t>(pos + 2)] = static_cast<std::uint8_t>((u >> 16) & 0xFFU);
            d[static_cast<std::size_t>(pos + 3)] = static_cast<std::uint8_t>((u >> 24) & 0xFFU);
        }

        std::int32_t get_s32_le_(const std::uint8_t *d, int pos)
        {
            const std::uint32_t u =
                (static_cast<std::uint32_t>(d[pos + 0]) << 0) |
                (static_cast<std::uint32_t>(d[pos + 1]) << 8) |
                (static_cast<std::uint32_t>(d[pos + 2]) << 16) |
                (static_cast<std::uint32_t>(d[pos + 3]) << 24);

            return static_cast<std::int32_t>(u);
        }

        bool send_frame_(int fd, canid_t id, const std::array<std::uint8_t, 8> &payload, std::uint8_t dlc)
        {
            struct can_frame frame{};
            frame.can_id = id;
            frame.can_dlc = dlc;
            std::memcpy(frame.data, payload.data(), dlc);

            const auto rc = ::write(fd, &frame, sizeof(frame));
            return rc == static_cast<ssize_t>(sizeof(frame));
        }

        int open_can_(const std::string &ifname)
        {
            const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
            if (fd < 0)
            {
                return -1;
            }

            struct ifreq ifr{};
            std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname.c_str());

            if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0)
            {
                ::close(fd);
                return -1;
            }

            struct sockaddr_can addr{};
            addr.can_family = AF_CAN;
            addr.can_ifindex = ifr.ifr_ifindex;

            if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
            {
                ::close(fd);
                return -1;
            }

            const int enable_nonblock = 1;
            ::ioctl(fd, FIONBIO, &enable_nonblock);

            return fd;
        }

    } // namespace

    RegatronTask::RegatronTask(const Config &cfg,
                               RegatronCommandState &command_state,
                               LatestRegatronState &latest_state,
                               const LatestBatteryState &latest_battery_state,
                               boost::atomic<bool> &running_flag)
        : cfg_(cfg),
          command_state_(command_state),
          latest_state_(latest_state),
          latest_battery_state_(latest_battery_state),
          running_(running_flag)
    {
    }

    void RegatronTask::operator()()
    {
        while (running_)
        {
            const int fd = open_can_(cfg_.can_ifname);
            if (fd < 0)
            {
                std::cerr << "[REGATRON] Failed to open CAN interface " << cfg_.can_ifname
                          << ". Retrying..." << std::endl;
                boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(cfg_.connect_retry_delay_ms));
                continue;
            }

            std::cout << "[REGATRON] CAN connected on " << cfg_.can_ifname << std::endl;

            RegatronStatusSnapshot status;
            status.timestamp = std::chrono::system_clock::now();
            status.can_online = true;
            status.fsm_state = RegatronFsmState::INIT;
            status.commanded_voltage_v = cfg_.default_voltage_v;

            auto state_enter_tp = std::chrono::steady_clock::now();
            auto last_rx_tp = std::chrono::steady_clock::now();
            auto cycle_tp = std::chrono::steady_clock::now();

            std::uint32_t tick_100ms = 0U;
            std::uint32_t tick_1s = 0U;
            bool pulse_rst_stop = false;

            while (running_)
            {
                // Receive all pending CAN frames.
                while (true)
                {
                    struct can_frame frame{};
                    const auto n = ::read(fd, &frame, sizeof(frame));
                    if (n < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }

                        std::cerr << "[REGATRON] CAN read error: " << std::strerror(errno) << std::endl;
                        status.can_online = false;
                        status.info = "CAN read error";
                        latest_state_.update(status);
                        ::close(fd);
                        goto reconnect;
                    }
                    if (n != static_cast<ssize_t>(sizeof(frame)))
                    {
                        break;
                    }

                    last_rx_tp = std::chrono::steady_clock::now();
                    status.can_online = true;

                    switch (frame.can_id)
                    {
                    case ID_ACT_U_I:
                        status.actual_voltage_v = static_cast<float>(get_s32_le_(frame.data, 0));
                        status.actual_current_a = static_cast<float>(get_s32_le_(frame.data, 4));
                        break;

                    case ID_ACT_SET_U_I:
                        status.actual_set_voltage_v = static_cast<float>(get_s32_le_(frame.data, 0));
                        status.actual_set_current_a = static_cast<float>(get_s32_le_(frame.data, 4));
                        break;

                    case ID_ACT_PWR_SWITCH_CTRL:
                        status.actual_power_kw = static_cast<float>(get_s32_le_(frame.data, 0));
                        status.actual_control_mode =
                            static_cast<RegatronControlMode>((frame.data[4] >> 0) & 0x0FU);

                        switch ((frame.data[4] >> 4) & 0x0FU)
                        {
                        case 0:
                            status.actual_switch = RegatronSwitchState::OFF;
                            break;
                        case 1:
                            status.actual_switch = RegatronSwitchState::STANDBY;
                            break;
                        case 2:
                            status.actual_switch = RegatronSwitchState::ON;
                            break;
                        case 5:
                            status.actual_switch = RegatronSwitchState::ERROR;
                            break;
                        default:
                            status.actual_switch = RegatronSwitchState::UNKNOWN;
                            break;
                        }
                        break;

                    case ID_ACT_FAULT:
                        status.fault_code = frame.data[0];
                        status.fault_msg_id = frame.data[1];
                        status.fault_active = (status.fault_code != 0U);
                        break;

                    default:
                        break;
                    }
                }

                const auto cmd = command_state_.snapshot_and_consume_pulses();
                pulse_rst_stop = cmd.clear_fault_requested;

                const auto now = std::chrono::steady_clock::now();
                const auto ms_in_state =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - state_enter_tp).count();

                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rx_tp).count() >
                    cfg_.can_rx_timeout_ms)
                {
                    status.can_online = false;
                    status.info = "CAN feedback timeout";
                }

                // FSM
                auto transition_to = [&](RegatronFsmState next, const char *reason)
                {
                    if (status.fsm_state != next)
                    {
                        status.fsm_state = next;
                        status.info = reason;
                        state_enter_tp = now;
                        if (next == RegatronFsmState::CHARGE || next == RegatronFsmState::DISCHARGE)
                        {
                            cycle_tp = now;
                        }
                    }
                };

                const auto battery = latest_battery_state_.get_copy();

                bool battery_valid = battery.has_value();
                bool battery_alarm = false;
                bool battery_protection = false;
                bool battery_protected_status = false;
                float battery_voltage_v = 0.0F;
                float battery_soc_pct = -1.0F;

                if (battery_valid)
                {
                    battery_voltage_v = battery->pack_voltage_v;
                    battery_soc_pct = battery->soc_pct;
                    battery_alarm = (battery->alarm_raw != 0U);
                    battery_protection = (battery->protection_raw != 0U);
                    battery_protected_status = (battery->status_raw == 0x0004U);
                }

                switch (status.fsm_state)
                {
                case RegatronFsmState::INIT:
                    transition_to(RegatronFsmState::OFF, "startup");
                    break;

                case RegatronFsmState::OFF:
                    status.cycle_active = false;
                    status.commanded_current_a = 0.0F;
                    if (cmd.enable && cmd.start_requested)
                    {
                        transition_to(RegatronFsmState::WAIT, "start");
                    }
                    break;

                case RegatronFsmState::WAIT:
                    status.commanded_current_a = 0.0F;
                    if (!cmd.enable || cmd.stop_requested)
                    {
                        transition_to(RegatronFsmState::OFF, "disabled");
                    }
                    else if (ms_in_state >= cfg_.wait_delay_ms)
                    {
                        transition_to(RegatronFsmState::STANDBY1, "wait elapsed");
                    }
                    break;

                case RegatronFsmState::STANDBY1:
                    status.commanded_current_a = 0.0F;
                    status.commanded_voltage_v = std::max(status.actual_voltage_v, 1.0F);
                    if (!cmd.enable || cmd.stop_requested)
                    {
                        transition_to(RegatronFsmState::OFF, "stop");
                    }
                    else if (status.actual_switch == RegatronSwitchState::ON)
                    {
                        transition_to(RegatronFsmState::STANDBY2, "switch on");
                    }
                    else if (ms_in_state >= cfg_.standby1_timeout_ms)
                    {
                        transition_to(RegatronFsmState::ERROR, "switch on timeout");
                    }
                    break;

                case RegatronFsmState::STANDBY2:
                    status.commanded_current_a = 0.0F;
                    if (!cmd.enable || cmd.stop_requested)
                    {
                        transition_to(RegatronFsmState::OFF, "stop");
                    }
                    else if (ms_in_state >= cfg_.standby2_delay_ms)
                    {
                        transition_to(RegatronFsmState::CHARGE, "enter charge");
                    }
                    break;

                case RegatronFsmState::CHARGE:
                    status.cycle_active = true;
                    status.commanded_current_a = std::fabs(cmd.charge_current_a);
                    status.cycle_elapsed_s = static_cast<std::uint32_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(now - cycle_tp).count());

                    if (!cmd.enable || cmd.stop_requested)
                    {
                        transition_to(RegatronFsmState::OFF, "stop");
                    }
                    else if (status.fault_active)
                    {
                        transition_to(RegatronFsmState::ERROR, "regatron fault");
                    }
                    else if (battery_valid && (battery_alarm || battery_protection || battery_protected_status))
                    {
                        transition_to(RegatronFsmState::OFF, "battery alarm/protection during charge");
                    }
                    else if (battery_valid && battery_voltage_v >= cmd.voltage_limit_max_v)
                    {
                        transition_to(RegatronFsmState::DISCHARGE, "battery upper voltage reached");
                    }
                    else if (battery_valid && battery_soc_pct >= 0.0F && battery_soc_pct >= cmd.soc_max_pct)
                    {
                        transition_to(RegatronFsmState::DISCHARGE, "soc max reached");
                    }
                    else if (status.cycle_elapsed_s >= static_cast<std::uint32_t>(std::max(1.0F, cmd.cycle_time_s)))
                    {
                        transition_to(RegatronFsmState::DISCHARGE, "cycle swap");
                    }
                    break;

                case RegatronFsmState::DISCHARGE:
                    status.cycle_active = true;
                    status.commanded_current_a = -std::fabs(cmd.discharge_current_a);
                    status.cycle_elapsed_s = static_cast<std::uint32_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(now - cycle_tp).count());

                    if (!cmd.enable || cmd.stop_requested)
                    {
                        transition_to(RegatronFsmState::OFF, "stop");
                    }
                    else if (status.fault_active)
                    {
                        transition_to(RegatronFsmState::ERROR, "regatron fault");
                    }
                    else if (battery_valid && (battery_alarm || battery_protection || battery_protected_status))
                    {
                        transition_to(RegatronFsmState::OFF, "battery alarm/protection during discharge");
                    }
                    else if (battery_valid && battery_voltage_v <= cmd.voltage_limit_min_v)
                    {
                        transition_to(RegatronFsmState::CHARGE, "battery lower voltage reached");
                    }
                    else if (battery_valid && battery_soc_pct >= 0.0F && battery_soc_pct <= cmd.soc_min_pct)
                    {
                        transition_to(RegatronFsmState::CHARGE, "soc min reached");
                    }
                    else if (status.cycle_elapsed_s >= static_cast<std::uint32_t>(std::max(1.0F, cmd.cycle_time_s)))
                    {
                        transition_to(RegatronFsmState::CHARGE, "cycle swap");
                    }
                    break;

                case RegatronFsmState::ERROR:
                    status.cycle_active = false;
                    status.commanded_current_a = 0.0F;
                    if (!cmd.enable)
                    {
                        transition_to(RegatronFsmState::OFF, "disabled");
                    }
                    else if (cmd.clear_fault_requested && !status.fault_active)
                    {
                        transition_to(RegatronFsmState::OFF, "fault cleared");
                    }
                    break;
                }

                // Fast path: SET_U_I and SET_SWITCH_CTRL every 10 ms
                {
                    auto payload = make_empty8_();
                    put_s32_le_(payload, 0, static_cast<std::int32_t>(std::lround(status.commanded_voltage_v)));
                    put_s32_le_(payload, 4, static_cast<std::int32_t>(std::lround(status.commanded_current_a)));
                    send_frame_(fd, ID_SET_U_I, payload, 8);
                }

                {
                    auto payload = make_empty8_();

                    std::uint8_t set_switch = 0U;
                    std::uint8_t set_ctrl = static_cast<std::uint8_t>(RegatronControlMode::CURRENT);

                    switch (status.fsm_state)
                    {
                    case RegatronFsmState::OFF:
                    case RegatronFsmState::INIT:
                    case RegatronFsmState::ERROR:
                        set_switch = 0U;
                        set_ctrl = static_cast<std::uint8_t>(RegatronControlMode::CURRENT);
                        break;

                    case RegatronFsmState::WAIT:
                    case RegatronFsmState::STANDBY1:
                        set_switch = 2U;
                        set_ctrl = static_cast<std::uint8_t>(RegatronControlMode::VOLTAGE);
                        break;

                    case RegatronFsmState::STANDBY2:
                    case RegatronFsmState::CHARGE:
                    case RegatronFsmState::DISCHARGE:
                        set_switch = 2U;
                        set_ctrl = static_cast<std::uint8_t>(RegatronControlMode::CURRENT);
                        break;
                    }

                    payload[0] =
                        static_cast<std::uint8_t>((set_switch & 0x07U) |
                                                  ((set_ctrl & 0x07U) << 3) |
                                                  ((pulse_rst_stop ? 1U : 0U) << 0)); // simple pulse carrier via SET_VIRTUAL_IN below

                    send_frame_(fd, ID_SET_SWITCH_CTRL, payload, 1);
                }

                // 10 Hz group
                if (++tick_100ms >= 10U)
                {
                    tick_100ms = 0U;

                    {
                        auto payload = make_empty8_();
                        put_u16_le_(payload, 0, clamp_u16_scaled_0p1(cmd.voltage_limit_max_v));
                        put_s24_le_(payload, 2, clamp_s24_scaled_0p1(cmd.current_limit_max_a));
                        put_s24_le_(payload, 5, clamp_s24_scaled_0p1(cfg_.default_supervision_p_max_kw));
                        send_frame_(fd, ID_SET_LIM_MX, payload, 8);
                    }

                    {
                        auto payload = make_empty8_();
                        put_u16_le_(payload, 0, clamp_u16_scaled_0p1(cmd.voltage_limit_min_v));
                        put_s24_le_(payload, 2, clamp_s24_scaled_0p1(cmd.current_limit_min_a));
                        put_s24_le_(payload, 5, clamp_s24_scaled_0p1(cfg_.default_supervision_p_min_kw));
                        send_frame_(fd, ID_SET_LIM_MN, payload, 8);
                    }

                    {
                        auto payload = make_empty8_();
                        put_u16_le_(payload, 0, clamp_u16_scaled_0p1(cfg_.default_supervision_u_max_v));
                        put_s24_le_(payload, 2, clamp_s24_scaled_0p1(cfg_.default_supervision_i_max_a));
                        put_s24_le_(payload, 5, clamp_s24_scaled_0p1(cfg_.default_supervision_p_max_kw));
                        send_frame_(fd, ID_SET_SUPV_MX, payload, 8);
                    }

                    {
                        auto payload = make_empty8_();
                        put_u16_le_(payload, 0, clamp_u16_scaled_0p1(cfg_.default_supervision_u_min_v));
                        put_s24_le_(payload, 2, clamp_s24_scaled_0p1(cfg_.default_supervision_i_min_a));
                        put_s24_le_(payload, 5, clamp_s24_scaled_0p1(cfg_.default_supervision_p_min_kw));
                        send_frame_(fd, ID_SET_SUPV_MN, payload, 8);
                    }

                    {
                        auto payload = make_empty8_();
                        put_u16_le_(payload, 0, clamp_u16_scaled_0p1(cfg_.default_u_slope_v_per_s));
                        put_u16_le_(payload, 2, clamp_u16_scaled_0p1(cfg_.default_i_slope_a_per_s));
                        put_u16_le_(payload, 4, clamp_u16_scaled_0p1(cfg_.default_p_slope_kw_per_s));
                        send_frame_(fd, ID_SET_SLOPE, payload, 6);
                    }

                    {
                        auto payload = make_empty8_();
                        payload[0] = 1U;
                        send_frame_(fd, ID_CLEARANCE, payload, 1);
                    }

                    if (pulse_rst_stop)
                    {
                        auto payload = make_empty8_();
                        payload[0] = 1U;
                        send_frame_(fd, ID_SET_VIRTUAL_IN, payload, 1);
                    }
                }

                // 1 Hz group
                if (++tick_1s >= 100U)
                {
                    tick_1s = 0U;

                    auto payload = make_empty8_();
                    payload[0] = 10U; // SET_DEM_CYCLE = 10 ms
                    payload[1] = 10U; // SET_MSRD_CYCLE = 10 ms
                    send_frame_(fd, ID_SET_CYCLE_TIME, payload, 2);
                }

                status.timestamp = std::chrono::system_clock::now();
                latest_state_.update(status);

                boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(cfg_.base_period_ms));
            }

            ::close(fd);
            std::cout << "[REGATRON] Task stopped." << std::endl;
            return;

        reconnect:
            boost::this_thread::sleep_for(
                boost::chrono::milliseconds(cfg_.connect_retry_delay_ms));
        }
    }

} // namespace bms