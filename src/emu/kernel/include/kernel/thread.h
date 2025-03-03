/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
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

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <cpu/arm_factory.h>

#include <common/linked.h>
#include <common/queue.h>
#include <common/resource.h>

#include <kernel/chunk.h>
#include <kernel/common.h>
#include <kernel/object_ix.h>

#include <mem/ptr.h>
#include <utils/reqsts.h>

namespace eka2l1 {
    class kernel_system;
    class ntimer;
    class memory;
    class gdbstub;

    struct ipc_msg;
    using ipc_msg_ptr = ipc_msg *;

    namespace kernel {
        namespace legacy {
            class sync_object_base;
        }

        class mutex;
        class semaphore;
        class process;
    }

    namespace service {
        class faker;
    }

    using chunk_ptr = kernel::chunk *;
    using mutex_ptr = kernel::mutex *;
    using sema_ptr = kernel::semaphore *;
    using process_ptr = kernel::process *;

    namespace kernel {
        using address = std::uint32_t;
        using thread_stack = common::resource<address>;
        using thread_stack_ptr = std::unique_ptr<thread_stack>;

        class thread_scheduler;

        enum class thread_state {
            create,
            run,
            wait,
            ready,
            stop,
            wait_fast_sema, // Wait for semaphore
            wait_mutex,
            wait_condvar,
            wait_mutex_suspend,
            wait_fast_sema_suspend,
            wait_condvar_suspend,
            hold_mutex_pending,
            wait_dfc, // Unused
            wait_hle // Wait in case an HLE event is taken place - e.g GUI
        };

        enum thread_priority {
            priority_null = -30,
            priority_much_less = -20,
            priority_less = -10,
            priority_normal = 0,
            priority_more = 10,
            priority_much_more = 20,
            priority_real_time = 30,
            priority_absolute_very_low = 100,
            priority_absolute_low = 200,
            priority_absolute_background_normal = 250,
            priority_absolute_background = 300,
            priority_absolute_foreground_normal = 350,
            priorty_absolute_foreground = 400,
            priority_absolute_high = 500
        };

        enum {
            // Symbian default is 20, but we are mixing both CPU code and also HLE stuffs, so we tone
            // it down a bit.
            USER_THREAD_TIMESLICE_IN_MILLISECS = 10
        };

        struct tls_slot {
            std::uint32_t handle = 0xFFFFFFFF;
            std::uint32_t uid = 0xFFFFFFFF;
            ptr<void> pointer;
        };

        /**
         * @brief Struct used to store critical variables that is needed for Symbian
         * C++ thread to work properly (exception-trap, or allocator).
         * 
         * Compare to thread local storage, which that is only available in newer OS versions (mostly since 2009),
         * through CP15 intercept, the data here can only be modified by the OS through system calls,
         * while local thread storage grabbed through the pointer retrieved in CP15 call, can store data freely
         * (Symbian does use some initial space for this same purpose of storing allocator, scheduler, etc...).
         */
        struct thread_local_data {
            ptr<void> heap;
            ptr<void> scheduler;
            ptr<void> trap_handler;
            std::uint32_t thread_id;
            ptr<void> tls_heap_allocator;
            std::uint32_t tls_array_data[40];

            // Hash map is used here, there will hopefully not be thousand of elements 
            // and constant complexity, memory is not a worry.
            std::unordered_map<std::uint32_t, tls_slot> tls_slots;

            explicit thread_local_data(const std::uint32_t uid);
        };

        static constexpr std::size_t NATIVE_THREAD_LOCAL_DATA_COPY_SIZE = offsetof(thread_local_data, tls_slots);

        class thread : public kernel_obj {
        private:
            enum internal_flag {
                FLAG_PROCESS_CRITICAL = 1 << 0,
                FLAG_PROCESS_PERMANENT = 1 << 1,
                FLAG_SYSTEM_CRITICAL = 1 << 2,
                FLAG_SYSTEM_PERMANENT = 1 << 3
            };

            friend class eka2l1::kernel_system;
            friend class eka2l1::gdbstub;

            friend class thread_scheduler;
            friend class mutex;
            friend class semaphore;
            friend class condvar;
            friend class process;
            friend class codeseg;
            friend class service::faker;
            friend class legacy::sync_object_base;

            thread_state state;
            thread_state backup_state;

            // Thread context to save when suspend the execution
            arm::core::thread_context ctx;

            thread_priority priority;

            int last_priority;
            int real_priority;

            int stack_size;
            int min_heap_size, max_heap_size;

            ptr<void> usrdata;

            memory_system *mem;
            ntimer *timing;

            int time;
            int timeslice;

            chunk_ptr stack_chunk;
            chunk_ptr name_chunk;
            chunk_ptr local_data_chunk;

            thread_local_data *ldata;
            thread_scheduler *scheduler;

            sema_ptr request_sema;
            std::uint32_t flags;
            ipc_msg_ptr sync_msg;

            void reset_thread_ctx(const std::uint32_t entry_point, const std::uint32_t stack_top, const std::uint32_t thr_local_addr,
                const bool initial);
            void create_stack_metadata(std::uint8_t *stack_host_ptr, address stack_ptr, ptr<void> allocator_ptr,
                std::uint32_t name_len, address name_ptr, address epa);

            int leave_depth = -1;

            object_ix thread_handles;

            int wakeup_handle;

            int rendezvous_reason;
            int exit_reason;

            int sleep_level;

            entity_exit_type exit_type;
            std::u16string exit_category;

            std::vector<epoc::notify_info> logon_requests;
            std::vector<epoc::notify_info> rendezvous_requests;

            std::uint64_t create_time;
            std::uint64_t last_run_time;
            std::uint64_t total_real_run_time;

            eka2l1::ptr<epoc::request_status> sleep_nof_sts;

            common::double_link<kernel::thread> scheduler_link;
            common::double_linked_queue_element wait_link;
            common::double_linked_queue_element pending_link;
            common::double_linked_queue_element suspend_link;
            common::double_linked_queue_element process_thread_link;

            common::roundabout closing_libs;

            address exception_handler;
            std::uint32_t exception_mask;

            address trap_stack;

            std::vector<address> cached_detach_eps;
            bool cached_detach;

            int wait_object_timeout_callback_type;
            bool is_in_timeout;

        protected:
            epoc9_std_epoc_thread_create_info *metadata;
            std::queue<std::uint32_t> last_syscalls;

            // Processing panics and decided if we should embrace it
            bool take_on_panic(const std::u16string &category, const std::int32_t code);

        public:
            kernel_obj_ptr get_object(std::uint32_t handle);
            kernel_obj *wait_obj;

            void logon(eka2l1::ptr<epoc::request_status> logon_request, bool rendezvous);
            bool logon_cancel(eka2l1::ptr<epoc::request_status> logon_request, bool rendezvous);

            void rendezvous(int rendezvous_reason);
            void finish_logons();

            /**
             * @brief       Push a new trap frame.
             * 
             * @param       new_trap          Pointer of the new trap to push.
             * @returns     Pointer to trap handler.
             */
            address push_trap_frame(const address new_trap);
            address pop_trap_frame();

            const address get_exception_handler() const {
                return exception_handler;
            }

            void set_exception_handler(const address addr) {
                exception_handler = addr;
            }

            const std::uint32_t get_exception_mask() const {
                return exception_mask;
            }

            void set_exception_mask(const std::uint32_t mask) {
                exception_mask = mask;
            }

            void set_exit_reason(int reason) {
                exit_reason = reason;
            }

            int get_exit_reason() const {
                return exit_reason;
            }

            void set_exit_type(const entity_exit_type new_exit_type) {
                exit_type = new_exit_type;
            }

            entity_exit_type get_exit_type() const {
                return exit_type;
            }

            int get_remaining_screenticks() const {
                return time;
            }

            explicit thread();

            explicit thread(kernel_system *kern, memory_system *mem, ntimer *timing)
                : kernel_obj(kern)
                , mem(mem)
                , timing(timing) {
                obj_type = kernel::object_type::thread;
            }

            explicit thread(kernel_system *kern, memory_system *mem, ntimer *timing, kernel::process *owner, kernel::access_type access,
                const std::string &name, const address epa, const size_t stack_size,
                const size_t min_heap_size, const size_t max_heap_size,
                bool initial,
                ptr<void> usrdata = 0,
                ptr<void> allocator = 0,
                thread_priority pri = priority_normal);

            ~thread() {}

            void do_cleanup();
            int destroy() override;

            chunk_ptr get_stack_chunk();

            std::optional<tls_slot> get_tls_slot_no_uid(const std::uint32_t handle);
            std::optional<tls_slot> get_tls_slot(const std::uint32_t handle, const std::uint32_t dll_uid);
            bool set_tls_slot(const std::uint32_t handle, const std::uint32_t dll_uid, ptr<void> value);
            void close_tls_slot(const std::uint32_t );

            void update_priority();

            bool suspend();
            bool resume();

            bool is_suspended() const;
            bool set_initial_userdata(const address userdata);

            void wait_for_any_request();
            void signal_request(int count = 1);
            std::int32_t request_count();

            void set_priority(const thread_priority new_pri);

            bool sleep(std::uint32_t ussecs);
            bool sleep_nof(eka2l1::ptr<epoc::request_status> sts, std::uint32_t mssecs);

            void notify_sleep(const int errcode);

            bool stop();
            bool kill(const entity_exit_type exit_type, const std::u16string &category,
                const std::int32_t reason);

            void add_ticks(const int num);

            void real_time_active_begin();
            void real_time_active_end();
            void handle_wait_object_timeout();

            std::uint64_t get_real_active_time() const {
                return total_real_run_time;
            }

            std::uint64_t get_real_time_active_begin() const {
                return last_run_time;
            }

            thread_priority get_priority() const {
                return priority;
            }

            std::uint32_t get_flags() const {
                return flags;
            }

            void set_flags(const std::uint32_t new_flags) {
                flags = new_flags;
            }

            void set_process_permanent(const bool opt) {
                flags &= ~FLAG_PROCESS_PERMANENT;
                if (opt) {
                    flags |= FLAG_PROCESS_PERMANENT;
                }
            }

            const bool is_process_permanent() const {
                return flags & FLAG_PROCESS_PERMANENT;
            }

            const bool is_process_critical() const {
                return flags & FLAG_PROCESS_CRITICAL;
            }

            thread_local_data *get_local_data() {
                return ldata;
            }

            thread_scheduler *get_scheduler() {
                return scheduler;
            }

            kernel::process *owning_process() {
                return reinterpret_cast<kernel::process *>(owner);
            }

            arm::core::thread_context &get_thread_context() {
                return ctx;
            }

            void call_exception_handler(const std::int32_t exec_type);
            void restore_before_exception_state();

            void owning_process(kernel::process *pr);

            void start_timeout(const std::uint32_t us);
            void end_timeout_early();

            std::uint32_t last_syscall() const;
            void add_last_syscall(const std::uint32_t syscall);

            kernel::thread *next_in_process();

            std::vector<std::uint32_t> get_detach_eps();
            std::int32_t get_detach_eps_limit(std::int32_t *count, address *addrs);
            void cleanup_detachs();

            thread_state current_state() const {
                return state;
            }

            int current_real_priority() const {
                return real_priority;
            }

            void current_state(thread_state st) {
                state = st;
            }

            ipc_msg_ptr &get_sync_msg() {
                return sync_msg;
            }

            void increase_leave_depth() {
                leave_depth++;
            }

            void decrease_leave_depth() {
                leave_depth--;
            }

            bool is_invalid_leave() const {
                return leave_depth > 0;
            }

            int get_leave_depth() const {
                return leave_depth;
            }

            std::size_t get_total_open_handles() const {
                return thread_handles.total_open();
            }

            std::uint32_t last_handle() {
                return thread_handles.last_handle();
            }
        };

        using thread_ptr = kernel::thread *;

        struct thread_priority_less_comparator {
            const bool operator()(kernel::thread *lhs, kernel::thread *rhs) const {
                return lhs->current_real_priority() < rhs->current_real_priority();
            }
        };

        using thread_priority_queue = eka2l1::cp_queue<kernel::thread *, thread_priority_less_comparator>;
    }
}
