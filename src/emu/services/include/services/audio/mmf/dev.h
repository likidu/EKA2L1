/*
 * Copyright (c) 2020 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <services/audio/mmf/common.h>
#include <services/framework.h>

#include <drivers/audio/dsp.h>

#include <mutex>
#include <vector>

namespace eka2l1 {
    namespace kernel {
        class chunk;
    }

    namespace kernel {
        class msg_queue;
    }

    constexpr std::uint32_t make_four_cc(const char c1, const char c2,
        const char c3, const char c4) {
        return (c1 | (c2 << 8) | (c3 << 16) | (c4 << 24));
    }

    constexpr std::uint32_t PCM8_CC = make_four_cc(' ', ' ', 'P', '8');
    constexpr std::uint32_t PCM16_CC = make_four_cc(' ', 'P', '1', '6');
    constexpr std::uint32_t PCMU8_CC = make_four_cc(' ', 'P', 'U', '8');
    constexpr std::uint32_t PCMU16_CC = make_four_cc('P', 'U', '1', '6');
    constexpr std::uint32_t MP3_CC = make_four_cc(' ', 'M', 'P', '3');
    constexpr std::uint32_t AAC_CC = make_four_cc(' ', 'A', 'A', 'C');
    constexpr std::uint32_t WAV_CC = make_four_cc(' ', 'W', 'A', 'V');

    class mmf_dev_server_session : public service::typical_session {
        epoc::mmf_priority_settings pri_;

        std::uint32_t last_buffer_;
        std::vector<std::uint32_t> four_ccs_;

        epoc::mmf_capabilities conf_;
        std::uint32_t input_cc_;

        epoc::mmf_state stream_state_;
        epoc::mmf_state desired_state_;

        kernel::chunk *buffer_chunk_;
        kernel::handle last_buffer_handle_;

        epoc::notify_info finish_info_;
        epoc::notify_info buffer_info_;
        epoc::mmf_dev_hw_buf_v2 *buffer_buf_;

        epoc::mmf_capabilities get_caps();
        void get_supported_input_data_types();

        std::unique_ptr<drivers::dsp_stream> stream_;
        std::mutex dev_access_lock_;

        std::uint32_t volume_;
        std::uint32_t volume_ramp_us_;
        std::int32_t left_balance_;
        std::int32_t right_balance_;

        int underflow_event_;

        kernel::msg_queue *evt_msg_queue_;

    protected:
        kernel::handle do_set_buffer_buf_and_get_return_value();
        void do_set_buffer_to_be_filled();
        void do_submit_buffer_data_receive();
        void do_report_buffer_to_be_filled();
        void do_report_buffer_to_be_emptied();
        void init_stream_through_state();
        void deref_audio_buffer_chunk();
        bool prepare_audio_buffer_chunk();
        void send_event_to_msg_queue(const epoc::mmf_dev_sound_queue_item &item);
        void stop();

        bool is_recording_stream() const {
            return (desired_state_ == epoc::mmf_state_recording);
        }
        
        drivers::dsp_input_stream *recording_stream() {
            return static_cast<drivers::dsp_input_stream*>(stream_.get());
        }

    public:
        explicit mmf_dev_server_session(service::typical_server *serv, kernel::uid client_ss_uid, epoc::version client_version);
        ~mmf_dev_server_session() override;

        void fetch(service::ipc_context *ctx) override;

        void init0(service::ipc_context *ctx);
        void init3(service::ipc_context *ctx);
        void set_volume(service::ipc_context *ctx);
        void volume(service::ipc_context *ctx);
        void max_volume(service::ipc_context *ctx);
        void set_gain(service::ipc_context *ctx);
        void gain(service::ipc_context *ctx);
        void max_gain(service::ipc_context *ctx);
        void set_play_balance(service::ipc_context *ctx);
        void play_balance(service::ipc_context *ctx);
        void set_priority_settings(service::ipc_context *ctx);
        void capabilities(service::ipc_context *ctx);
        void get_supported_input_data_types(service::ipc_context *ctx);
        void copy_fourcc_array(service::ipc_context *ctx);
        void set_config(service::ipc_context *ctx);
        void get_config(service::ipc_context *ctx);
        void samples_played(service::ipc_context *ctx);
        void stop(service::ipc_context *ctx);
        void play_init(service::ipc_context *ctx);
        void record_init(service::ipc_context *ctx);
        void async_command(service::ipc_context *ctx);
        void request_resource_notification(service::ipc_context *ctx);
        void get_buffer(service::ipc_context *ctx);
        void cancel_complete_error(service::ipc_context *ctx);
        void complete_error(service::ipc_context *ctx);
        void cancel_get_buffer(service::ipc_context *ctx);
        void set_volume_ramp(service::ipc_context *ctx);

        // Play sync, when finish complete the status
        void play_data(service::ipc_context *ctx);
        void record_data(service::ipc_context *ctx);

        void complete_play(const std::int32_t code);
        void complete_record(const std::int32_t code);
    };

    class mmf_dev_server : public service::typical_server {
    protected:
        std::uint32_t flags_ = 0;

        enum {
            FLAG_ENABLE_UNDERFLOW_REPORT = 1 << 0           ///< Reports underflow if time ran out. Still experiemental, breaks Bounce
        };

    public:
        explicit mmf_dev_server(eka2l1::system *sys);
        void connect(service::ipc_context &context) override;

        bool report_inactive_underflow() const {
            return flags_ & FLAG_ENABLE_UNDERFLOW_REPORT;
        }
    };
}