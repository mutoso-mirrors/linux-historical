/* Automatically generated. Do not edit. */
#ifndef __ASM_OFFSETS_H__
#define __ASM_OFFSETS_H__

#include <linux/config.h>

#ifndef CONFIG_SMP

#define AOFF_task_state	0x00000000
#define ASIZ_task_state	0x00000004
#define AOFF_task_flags	0x00000004
#define ASIZ_task_flags	0x00000004
#define AOFF_task_sigpending	0x00000008
#define ASIZ_task_sigpending	0x00000004
#define AOFF_task_addr_limit	0x0000000c
#define ASIZ_task_addr_limit	0x00000004
#define AOFF_task_exec_domain	0x00000010
#define ASIZ_task_exec_domain	0x00000004
#define AOFF_task_need_resched	0x00000014
#define ASIZ_task_need_resched	0x00000004
#define AOFF_task_ptrace	0x00000018
#define ASIZ_task_ptrace	0x00000004
#define AOFF_task_lock_depth	0x0000001c
#define ASIZ_task_lock_depth	0x00000004
#define AOFF_task_counter	0x00000020
#define ASIZ_task_counter	0x00000004
#define AOFF_task_nice	0x00000024
#define ASIZ_task_nice	0x00000004
#define AOFF_task_policy	0x00000028
#define ASIZ_task_policy	0x00000004
#define AOFF_task_mm	0x0000002c
#define ASIZ_task_mm	0x00000004
#define AOFF_task_has_cpu	0x00000030
#define ASIZ_task_has_cpu	0x00000004
#define AOFF_task_processor	0x00000034
#define ASIZ_task_processor	0x00000004
#define AOFF_task_cpus_allowed	0x00000038
#define ASIZ_task_cpus_allowed	0x00000004
#define AOFF_task_run_list	0x0000003c
#define ASIZ_task_run_list	0x00000008
#define AOFF_task_next_task	0x00000044
#define ASIZ_task_next_task	0x00000004
#define AOFF_task_prev_task	0x00000048
#define ASIZ_task_prev_task	0x00000004
#define AOFF_task_active_mm	0x0000004c
#define ASIZ_task_active_mm	0x00000004
#define AOFF_task_binfmt	0x00000050
#define ASIZ_task_binfmt	0x00000004
#define AOFF_task_exit_code	0x00000054
#define ASIZ_task_exit_code	0x00000004
#define AOFF_task_exit_signal	0x00000058
#define ASIZ_task_exit_signal	0x00000004
#define AOFF_task_pdeath_signal	0x0000005c
#define ASIZ_task_pdeath_signal	0x00000004
#define AOFF_task_personality	0x00000060
#define ASIZ_task_personality	0x00000004
#define AOFF_task_pid	0x00000068
#define ASIZ_task_pid	0x00000004
#define AOFF_task_pgrp	0x0000006c
#define ASIZ_task_pgrp	0x00000004
#define AOFF_task_tty_old_pgrp	0x00000070
#define ASIZ_task_tty_old_pgrp	0x00000004
#define AOFF_task_session	0x00000074
#define ASIZ_task_session	0x00000004
#define AOFF_task_leader	0x00000078
#define ASIZ_task_leader	0x00000004
#define AOFF_task_p_opptr	0x0000007c
#define ASIZ_task_p_opptr	0x00000004
#define AOFF_task_p_pptr	0x00000080
#define ASIZ_task_p_pptr	0x00000004
#define AOFF_task_p_cptr	0x00000084
#define ASIZ_task_p_cptr	0x00000004
#define AOFF_task_p_ysptr	0x00000088
#define ASIZ_task_p_ysptr	0x00000004
#define AOFF_task_p_osptr	0x0000008c
#define ASIZ_task_p_osptr	0x00000004
#define AOFF_task_pidhash_next	0x00000090
#define ASIZ_task_pidhash_next	0x00000004
#define AOFF_task_pidhash_pprev	0x00000094
#define ASIZ_task_pidhash_pprev	0x00000004
#define AOFF_task_wait_chldexit	0x00000098
#define ASIZ_task_wait_chldexit	0x00000014
#define AOFF_task_vfork_sem	0x000000ac
#define ASIZ_task_vfork_sem	0x00000004
#define AOFF_task_rt_priority	0x000000b0
#define ASIZ_task_rt_priority	0x00000004
#define AOFF_task_it_real_value	0x000000b4
#define ASIZ_task_it_real_value	0x00000004
#define AOFF_task_it_prof_value	0x000000b8
#define ASIZ_task_it_prof_value	0x00000004
#define AOFF_task_it_virt_value	0x000000bc
#define ASIZ_task_it_virt_value	0x00000004
#define AOFF_task_it_real_incr	0x000000c0
#define ASIZ_task_it_real_incr	0x00000004
#define AOFF_task_it_prof_incr	0x000000c4
#define ASIZ_task_it_prof_incr	0x00000004
#define AOFF_task_it_virt_incr	0x000000c8
#define ASIZ_task_it_virt_incr	0x00000004
#define AOFF_task_real_timer	0x000000cc
#define ASIZ_task_real_timer	0x00000014
#define AOFF_task_times	0x000000e0
#define ASIZ_task_times	0x00000010
#define AOFF_task_start_time	0x000000f0
#define ASIZ_task_start_time	0x00000004
#define AOFF_task_per_cpu_utime	0x000000f4
#define ASIZ_task_per_cpu_utime	0x00000004
#define AOFF_task_min_flt	0x000000fc
#define ASIZ_task_min_flt	0x00000004
#define AOFF_task_maj_flt	0x00000100
#define ASIZ_task_maj_flt	0x00000004
#define AOFF_task_nswap	0x00000104
#define ASIZ_task_nswap	0x00000004
#define AOFF_task_cmin_flt	0x00000108
#define ASIZ_task_cmin_flt	0x00000004
#define AOFF_task_cmaj_flt	0x0000010c
#define ASIZ_task_cmaj_flt	0x00000004
#define AOFF_task_cnswap	0x00000110
#define ASIZ_task_cnswap	0x00000004
#define AOFF_task_uid	0x00000118
#define ASIZ_task_uid	0x00000004
#define AOFF_task_euid	0x0000011c
#define ASIZ_task_euid	0x00000004
#define AOFF_task_suid	0x00000120
#define ASIZ_task_suid	0x00000004
#define AOFF_task_fsuid	0x00000124
#define ASIZ_task_fsuid	0x00000004
#define AOFF_task_gid	0x00000128
#define ASIZ_task_gid	0x00000004
#define AOFF_task_egid	0x0000012c
#define ASIZ_task_egid	0x00000004
#define AOFF_task_sgid	0x00000130
#define ASIZ_task_sgid	0x00000004
#define AOFF_task_fsgid	0x00000134
#define ASIZ_task_fsgid	0x00000004
#define AOFF_task_ngroups	0x00000138
#define ASIZ_task_ngroups	0x00000004
#define AOFF_task_groups	0x0000013c
#define ASIZ_task_groups	0x00000080
#define AOFF_task_cap_effective	0x000001bc
#define ASIZ_task_cap_effective	0x00000004
#define AOFF_task_cap_inheritable	0x000001c0
#define ASIZ_task_cap_inheritable	0x00000004
#define AOFF_task_cap_permitted	0x000001c4
#define ASIZ_task_cap_permitted	0x00000004
#define AOFF_task_user	0x000001cc
#define ASIZ_task_user	0x00000004
#define AOFF_task_rlim	0x000001d0
#define ASIZ_task_rlim	0x00000050
#define AOFF_task_used_math	0x00000220
#define ASIZ_task_used_math	0x00000002
#define AOFF_task_comm	0x00000222
#define ASIZ_task_comm	0x00000010
#define AOFF_task_link_count	0x00000234
#define ASIZ_task_link_count	0x00000004
#define AOFF_task_tty	0x00000238
#define ASIZ_task_tty	0x00000004
#define AOFF_task_semundo	0x0000023c
#define ASIZ_task_semundo	0x00000004
#define AOFF_task_semsleeping	0x00000240
#define ASIZ_task_semsleeping	0x00000004
#define AOFF_task_thread	0x00000248
#define ASIZ_task_thread	0x00000380
#define AOFF_task_fs	0x000005c8
#define ASIZ_task_fs	0x00000004
#define AOFF_task_files	0x000005cc
#define ASIZ_task_files	0x00000004
#define AOFF_task_sigmask_lock	0x000005d0
#define ASIZ_task_sigmask_lock	0x00000004
#define AOFF_task_sig	0x000005d4
#define ASIZ_task_sig	0x00000004
#define AOFF_task_signal	0x000005d8
#define ASIZ_task_signal	0x00000008
#define AOFF_task_blocked	0x000005e0
#define ASIZ_task_blocked	0x00000008
#define AOFF_task_sigqueue	0x000005e8
#define ASIZ_task_sigqueue	0x00000004
#define AOFF_task_sigqueue_tail	0x000005ec
#define ASIZ_task_sigqueue_tail	0x00000004
#define AOFF_task_sas_ss_sp	0x000005f0
#define ASIZ_task_sas_ss_sp	0x00000004
#define AOFF_task_sas_ss_size	0x000005f4
#define ASIZ_task_sas_ss_size	0x00000004
#define AOFF_task_parent_exec_id	0x000005f8
#define ASIZ_task_parent_exec_id	0x00000004
#define AOFF_task_self_exec_id	0x000005fc
#define ASIZ_task_self_exec_id	0x00000004
#define AOFF_task_alloc_lock	0x00000600
#define ASIZ_task_alloc_lock	0x00000004
#define AOFF_mm_mmap	0x00000000
#define ASIZ_mm_mmap	0x00000004
#define AOFF_mm_mmap_avl	0x00000004
#define ASIZ_mm_mmap_avl	0x00000004
#define AOFF_mm_mmap_cache	0x00000008
#define ASIZ_mm_mmap_cache	0x00000004
#define AOFF_mm_pgd	0x0000000c
#define ASIZ_mm_pgd	0x00000004
#define AOFF_mm_mm_users	0x00000010
#define ASIZ_mm_mm_users	0x00000004
#define AOFF_mm_mm_count	0x00000014
#define ASIZ_mm_mm_count	0x00000004
#define AOFF_mm_map_count	0x00000018
#define ASIZ_mm_map_count	0x00000004
#define AOFF_mm_mmap_sem	0x0000001c
#define ASIZ_mm_mmap_sem	0x00000020
#define AOFF_mm_page_table_lock	0x0000003c
#define ASIZ_mm_page_table_lock	0x00000004
#define AOFF_mm_context	0x00000040
#define ASIZ_mm_context	0x00000004
#define AOFF_mm_start_code	0x00000044
#define ASIZ_mm_start_code	0x00000004
#define AOFF_mm_end_code	0x00000048
#define ASIZ_mm_end_code	0x00000004
#define AOFF_mm_start_data	0x0000004c
#define ASIZ_mm_start_data	0x00000004
#define AOFF_mm_end_data	0x00000050
#define ASIZ_mm_end_data	0x00000004
#define AOFF_mm_start_brk	0x00000054
#define ASIZ_mm_start_brk	0x00000004
#define AOFF_mm_brk	0x00000058
#define ASIZ_mm_brk	0x00000004
#define AOFF_mm_start_stack	0x0000005c
#define ASIZ_mm_start_stack	0x00000004
#define AOFF_mm_arg_start	0x00000060
#define ASIZ_mm_arg_start	0x00000004
#define AOFF_mm_arg_end	0x00000064
#define ASIZ_mm_arg_end	0x00000004
#define AOFF_mm_env_start	0x00000068
#define ASIZ_mm_env_start	0x00000004
#define AOFF_mm_env_end	0x0000006c
#define ASIZ_mm_env_end	0x00000004
#define AOFF_mm_rss	0x00000070
#define ASIZ_mm_rss	0x00000004
#define AOFF_mm_total_vm	0x00000074
#define ASIZ_mm_total_vm	0x00000004
#define AOFF_mm_locked_vm	0x00000078
#define ASIZ_mm_locked_vm	0x00000004
#define AOFF_mm_def_flags	0x0000007c
#define ASIZ_mm_def_flags	0x00000004
#define AOFF_mm_cpu_vm_mask	0x00000080
#define ASIZ_mm_cpu_vm_mask	0x00000004
#define AOFF_mm_swap_cnt	0x00000084
#define ASIZ_mm_swap_cnt	0x00000004
#define AOFF_mm_swap_address	0x00000088
#define ASIZ_mm_swap_address	0x00000004
#define AOFF_mm_segments	0x0000008c
#define ASIZ_mm_segments	0x00000004
#define AOFF_thread_uwinmask	0x00000000
#define ASIZ_thread_uwinmask	0x00000004
#define AOFF_thread_kregs	0x00000004
#define ASIZ_thread_kregs	0x00000004
#define AOFF_thread_ksp	0x00000008
#define ASIZ_thread_ksp	0x00000004
#define AOFF_thread_kpc	0x0000000c
#define ASIZ_thread_kpc	0x00000004
#define AOFF_thread_kpsr	0x00000010
#define ASIZ_thread_kpsr	0x00000004
#define AOFF_thread_kwim	0x00000014
#define ASIZ_thread_kwim	0x00000004
#define AOFF_thread_fork_kpsr	0x00000018
#define ASIZ_thread_fork_kpsr	0x00000004
#define AOFF_thread_fork_kwim	0x0000001c
#define ASIZ_thread_fork_kwim	0x00000004
#define AOFF_thread_reg_window	0x00000020
#define ASIZ_thread_reg_window	0x00000200
#define AOFF_thread_rwbuf_stkptrs	0x00000220
#define ASIZ_thread_rwbuf_stkptrs	0x00000020
#define AOFF_thread_w_saved	0x00000240
#define ASIZ_thread_w_saved	0x00000004
#define AOFF_thread_float_regs	0x00000248
#define ASIZ_thread_float_regs	0x00000080
#define AOFF_thread_fsr	0x000002c8
#define ASIZ_thread_fsr	0x00000004
#define AOFF_thread_fpqdepth	0x000002cc
#define ASIZ_thread_fpqdepth	0x00000004
#define AOFF_thread_fpqueue	0x000002d0
#define ASIZ_thread_fpqueue	0x00000080
#define AOFF_thread_flags	0x00000350
#define ASIZ_thread_flags	0x00000004
#define AOFF_thread_current_ds	0x00000354
#define ASIZ_thread_current_ds	0x00000004
#define AOFF_thread_core_exec	0x00000358
#define ASIZ_thread_core_exec	0x00000020
#define AOFF_thread_new_signal	0x00000378
#define ASIZ_thread_new_signal	0x00000004
#define AOFF_thread_refcount	0x0000037c
#define ASIZ_thread_refcount	0x00000004

#else /* CONFIG_SMP */

#define AOFF_task_state	0x00000000
#define ASIZ_task_state	0x00000004
#define AOFF_task_flags	0x00000004
#define ASIZ_task_flags	0x00000004
#define AOFF_task_sigpending	0x00000008
#define ASIZ_task_sigpending	0x00000004
#define AOFF_task_addr_limit	0x0000000c
#define ASIZ_task_addr_limit	0x00000004
#define AOFF_task_exec_domain	0x00000010
#define ASIZ_task_exec_domain	0x00000004
#define AOFF_task_need_resched	0x00000014
#define ASIZ_task_need_resched	0x00000004
#define AOFF_task_ptrace	0x00000018
#define ASIZ_task_ptrace	0x00000004
#define AOFF_task_lock_depth	0x0000001c
#define ASIZ_task_lock_depth	0x00000004
#define AOFF_task_counter	0x00000020
#define ASIZ_task_counter	0x00000004
#define AOFF_task_nice	0x00000024
#define ASIZ_task_nice	0x00000004
#define AOFF_task_policy	0x00000028
#define ASIZ_task_policy	0x00000004
#define AOFF_task_mm	0x0000002c
#define ASIZ_task_mm	0x00000004
#define AOFF_task_has_cpu	0x00000030
#define ASIZ_task_has_cpu	0x00000004
#define AOFF_task_processor	0x00000034
#define ASIZ_task_processor	0x00000004
#define AOFF_task_cpus_allowed	0x00000038
#define ASIZ_task_cpus_allowed	0x00000004
#define AOFF_task_run_list	0x0000003c
#define ASIZ_task_run_list	0x00000008
#define AOFF_task_next_task	0x00000044
#define ASIZ_task_next_task	0x00000004
#define AOFF_task_prev_task	0x00000048
#define ASIZ_task_prev_task	0x00000004
#define AOFF_task_active_mm	0x0000004c
#define ASIZ_task_active_mm	0x00000004
#define AOFF_task_binfmt	0x00000050
#define ASIZ_task_binfmt	0x00000004
#define AOFF_task_exit_code	0x00000054
#define ASIZ_task_exit_code	0x00000004
#define AOFF_task_exit_signal	0x00000058
#define ASIZ_task_exit_signal	0x00000004
#define AOFF_task_pdeath_signal	0x0000005c
#define ASIZ_task_pdeath_signal	0x00000004
#define AOFF_task_personality	0x00000060
#define ASIZ_task_personality	0x00000004
#define AOFF_task_pid	0x00000068
#define ASIZ_task_pid	0x00000004
#define AOFF_task_pgrp	0x0000006c
#define ASIZ_task_pgrp	0x00000004
#define AOFF_task_tty_old_pgrp	0x00000070
#define ASIZ_task_tty_old_pgrp	0x00000004
#define AOFF_task_session	0x00000074
#define ASIZ_task_session	0x00000004
#define AOFF_task_leader	0x00000078
#define ASIZ_task_leader	0x00000004
#define AOFF_task_p_opptr	0x0000007c
#define ASIZ_task_p_opptr	0x00000004
#define AOFF_task_p_pptr	0x00000080
#define ASIZ_task_p_pptr	0x00000004
#define AOFF_task_p_cptr	0x00000084
#define ASIZ_task_p_cptr	0x00000004
#define AOFF_task_p_ysptr	0x00000088
#define ASIZ_task_p_ysptr	0x00000004
#define AOFF_task_p_osptr	0x0000008c
#define ASIZ_task_p_osptr	0x00000004
#define AOFF_task_pidhash_next	0x00000090
#define ASIZ_task_pidhash_next	0x00000004
#define AOFF_task_pidhash_pprev	0x00000094
#define ASIZ_task_pidhash_pprev	0x00000004
#define AOFF_task_wait_chldexit	0x00000098
#define ASIZ_task_wait_chldexit	0x00000018
#define AOFF_task_vfork_sem	0x000000b0
#define ASIZ_task_vfork_sem	0x00000004
#define AOFF_task_rt_priority	0x000000b4
#define ASIZ_task_rt_priority	0x00000004
#define AOFF_task_it_real_value	0x000000b8
#define ASIZ_task_it_real_value	0x00000004
#define AOFF_task_it_prof_value	0x000000bc
#define ASIZ_task_it_prof_value	0x00000004
#define AOFF_task_it_virt_value	0x000000c0
#define ASIZ_task_it_virt_value	0x00000004
#define AOFF_task_it_real_incr	0x000000c4
#define ASIZ_task_it_real_incr	0x00000004
#define AOFF_task_it_prof_incr	0x000000c8
#define ASIZ_task_it_prof_incr	0x00000004
#define AOFF_task_it_virt_incr	0x000000cc
#define ASIZ_task_it_virt_incr	0x00000004
#define AOFF_task_real_timer	0x000000d0
#define ASIZ_task_real_timer	0x00000014
#define AOFF_task_times	0x000000e4
#define ASIZ_task_times	0x00000010
#define AOFF_task_start_time	0x000000f4
#define ASIZ_task_start_time	0x00000004
#define AOFF_task_per_cpu_utime	0x000000f8
#define ASIZ_task_per_cpu_utime	0x00000080
#define AOFF_task_min_flt	0x000001f8
#define ASIZ_task_min_flt	0x00000004
#define AOFF_task_maj_flt	0x000001fc
#define ASIZ_task_maj_flt	0x00000004
#define AOFF_task_nswap	0x00000200
#define ASIZ_task_nswap	0x00000004
#define AOFF_task_cmin_flt	0x00000204
#define ASIZ_task_cmin_flt	0x00000004
#define AOFF_task_cmaj_flt	0x00000208
#define ASIZ_task_cmaj_flt	0x00000004
#define AOFF_task_cnswap	0x0000020c
#define ASIZ_task_cnswap	0x00000004
#define AOFF_task_uid	0x00000214
#define ASIZ_task_uid	0x00000004
#define AOFF_task_euid	0x00000218
#define ASIZ_task_euid	0x00000004
#define AOFF_task_suid	0x0000021c
#define ASIZ_task_suid	0x00000004
#define AOFF_task_fsuid	0x00000220
#define ASIZ_task_fsuid	0x00000004
#define AOFF_task_gid	0x00000224
#define ASIZ_task_gid	0x00000004
#define AOFF_task_egid	0x00000228
#define ASIZ_task_egid	0x00000004
#define AOFF_task_sgid	0x0000022c
#define ASIZ_task_sgid	0x00000004
#define AOFF_task_fsgid	0x00000230
#define ASIZ_task_fsgid	0x00000004
#define AOFF_task_ngroups	0x00000234
#define ASIZ_task_ngroups	0x00000004
#define AOFF_task_groups	0x00000238
#define ASIZ_task_groups	0x00000080
#define AOFF_task_cap_effective	0x000002b8
#define ASIZ_task_cap_effective	0x00000004
#define AOFF_task_cap_inheritable	0x000002bc
#define ASIZ_task_cap_inheritable	0x00000004
#define AOFF_task_cap_permitted	0x000002c0
#define ASIZ_task_cap_permitted	0x00000004
#define AOFF_task_user	0x000002c8
#define ASIZ_task_user	0x00000004
#define AOFF_task_rlim	0x000002cc
#define ASIZ_task_rlim	0x00000050
#define AOFF_task_used_math	0x0000031c
#define ASIZ_task_used_math	0x00000002
#define AOFF_task_comm	0x0000031e
#define ASIZ_task_comm	0x00000010
#define AOFF_task_link_count	0x00000330
#define ASIZ_task_link_count	0x00000004
#define AOFF_task_tty	0x00000334
#define ASIZ_task_tty	0x00000004
#define AOFF_task_semundo	0x00000338
#define ASIZ_task_semundo	0x00000004
#define AOFF_task_semsleeping	0x0000033c
#define ASIZ_task_semsleeping	0x00000004
#define AOFF_task_thread	0x00000340
#define ASIZ_task_thread	0x00000380
#define AOFF_task_fs	0x000006c0
#define ASIZ_task_fs	0x00000004
#define AOFF_task_files	0x000006c4
#define ASIZ_task_files	0x00000004
#define AOFF_task_sigmask_lock	0x000006c8
#define ASIZ_task_sigmask_lock	0x00000008
#define AOFF_task_sig	0x000006d0
#define ASIZ_task_sig	0x00000004
#define AOFF_task_signal	0x000006d4
#define ASIZ_task_signal	0x00000008
#define AOFF_task_blocked	0x000006dc
#define ASIZ_task_blocked	0x00000008
#define AOFF_task_sigqueue	0x000006e4
#define ASIZ_task_sigqueue	0x00000004
#define AOFF_task_sigqueue_tail	0x000006e8
#define ASIZ_task_sigqueue_tail	0x00000004
#define AOFF_task_sas_ss_sp	0x000006ec
#define ASIZ_task_sas_ss_sp	0x00000004
#define AOFF_task_sas_ss_size	0x000006f0
#define ASIZ_task_sas_ss_size	0x00000004
#define AOFF_task_parent_exec_id	0x000006f4
#define ASIZ_task_parent_exec_id	0x00000004
#define AOFF_task_self_exec_id	0x000006f8
#define ASIZ_task_self_exec_id	0x00000004
#define AOFF_task_alloc_lock	0x000006fc
#define ASIZ_task_alloc_lock	0x00000008
#define AOFF_mm_mmap	0x00000000
#define ASIZ_mm_mmap	0x00000004
#define AOFF_mm_mmap_avl	0x00000004
#define ASIZ_mm_mmap_avl	0x00000004
#define AOFF_mm_mmap_cache	0x00000008
#define ASIZ_mm_mmap_cache	0x00000004
#define AOFF_mm_pgd	0x0000000c
#define ASIZ_mm_pgd	0x00000004
#define AOFF_mm_mm_users	0x00000010
#define ASIZ_mm_mm_users	0x00000004
#define AOFF_mm_mm_count	0x00000014
#define ASIZ_mm_mm_count	0x00000004
#define AOFF_mm_map_count	0x00000018
#define ASIZ_mm_map_count	0x00000004
#define AOFF_mm_mmap_sem	0x0000001c
#define ASIZ_mm_mmap_sem	0x00000024
#define AOFF_mm_page_table_lock	0x00000040
#define ASIZ_mm_page_table_lock	0x00000008
#define AOFF_mm_context	0x00000048
#define ASIZ_mm_context	0x00000004
#define AOFF_mm_start_code	0x0000004c
#define ASIZ_mm_start_code	0x00000004
#define AOFF_mm_end_code	0x00000050
#define ASIZ_mm_end_code	0x00000004
#define AOFF_mm_start_data	0x00000054
#define ASIZ_mm_start_data	0x00000004
#define AOFF_mm_end_data	0x00000058
#define ASIZ_mm_end_data	0x00000004
#define AOFF_mm_start_brk	0x0000005c
#define ASIZ_mm_start_brk	0x00000004
#define AOFF_mm_brk	0x00000060
#define ASIZ_mm_brk	0x00000004
#define AOFF_mm_start_stack	0x00000064
#define ASIZ_mm_start_stack	0x00000004
#define AOFF_mm_arg_start	0x00000068
#define ASIZ_mm_arg_start	0x00000004
#define AOFF_mm_arg_end	0x0000006c
#define ASIZ_mm_arg_end	0x00000004
#define AOFF_mm_env_start	0x00000070
#define ASIZ_mm_env_start	0x00000004
#define AOFF_mm_env_end	0x00000074
#define ASIZ_mm_env_end	0x00000004
#define AOFF_mm_rss	0x00000078
#define ASIZ_mm_rss	0x00000004
#define AOFF_mm_total_vm	0x0000007c
#define ASIZ_mm_total_vm	0x00000004
#define AOFF_mm_locked_vm	0x00000080
#define ASIZ_mm_locked_vm	0x00000004
#define AOFF_mm_def_flags	0x00000084
#define ASIZ_mm_def_flags	0x00000004
#define AOFF_mm_cpu_vm_mask	0x00000088
#define ASIZ_mm_cpu_vm_mask	0x00000004
#define AOFF_mm_swap_cnt	0x0000008c
#define ASIZ_mm_swap_cnt	0x00000004
#define AOFF_mm_swap_address	0x00000090
#define ASIZ_mm_swap_address	0x00000004
#define AOFF_mm_segments	0x00000094
#define ASIZ_mm_segments	0x00000004
#define AOFF_thread_uwinmask	0x00000000
#define ASIZ_thread_uwinmask	0x00000004
#define AOFF_thread_kregs	0x00000004
#define ASIZ_thread_kregs	0x00000004
#define AOFF_thread_ksp	0x00000008
#define ASIZ_thread_ksp	0x00000004
#define AOFF_thread_kpc	0x0000000c
#define ASIZ_thread_kpc	0x00000004
#define AOFF_thread_kpsr	0x00000010
#define ASIZ_thread_kpsr	0x00000004
#define AOFF_thread_kwim	0x00000014
#define ASIZ_thread_kwim	0x00000004
#define AOFF_thread_fork_kpsr	0x00000018
#define ASIZ_thread_fork_kpsr	0x00000004
#define AOFF_thread_fork_kwim	0x0000001c
#define ASIZ_thread_fork_kwim	0x00000004
#define AOFF_thread_reg_window	0x00000020
#define ASIZ_thread_reg_window	0x00000200
#define AOFF_thread_rwbuf_stkptrs	0x00000220
#define ASIZ_thread_rwbuf_stkptrs	0x00000020
#define AOFF_thread_w_saved	0x00000240
#define ASIZ_thread_w_saved	0x00000004
#define AOFF_thread_float_regs	0x00000248
#define ASIZ_thread_float_regs	0x00000080
#define AOFF_thread_fsr	0x000002c8
#define ASIZ_thread_fsr	0x00000004
#define AOFF_thread_fpqdepth	0x000002cc
#define ASIZ_thread_fpqdepth	0x00000004
#define AOFF_thread_fpqueue	0x000002d0
#define ASIZ_thread_fpqueue	0x00000080
#define AOFF_thread_flags	0x00000350
#define ASIZ_thread_flags	0x00000004
#define AOFF_thread_current_ds	0x00000354
#define ASIZ_thread_current_ds	0x00000004
#define AOFF_thread_core_exec	0x00000358
#define ASIZ_thread_core_exec	0x00000020
#define AOFF_thread_new_signal	0x00000378
#define ASIZ_thread_new_signal	0x00000004
#define AOFF_thread_refcount	0x0000037c
#define ASIZ_thread_refcount	0x00000004

#endif /* CONFIG_SMP */

#endif /* __ASM_OFFSETS_H__ */
