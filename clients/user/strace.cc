/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifdef GRANARY_WHERE_user

#include <granary.h>

GRANARY_DECLARE_bool(hook_syscalls);

#include "clients/user/syscall.h"

using namespace granary;

namespace {

static const char *syscall_names[] = {
  [__NR_read] = "read",  // 0
  [__NR_write] = "write",  // 1
  [__NR_open] = "open",  // 2
  [__NR_close] = "close",  // 3
  [__NR_stat] = "stat",  // 4
  [__NR_fstat] = "fstat",  // 5
  [__NR_lstat] = "lstat",  // 6
  [__NR_poll] = "poll",  // 7
  [__NR_lseek] = "lseek",  // 8
  [__NR_mmap] = "mmap",  // 9
  [__NR_mprotect] = "mprotect",  // 10
  [__NR_munmap] = "munmap",  // 11
  [__NR_brk] = "brk",  // 12
  [__NR_rt_sigaction] = "rt_sigaction",  // 13
  [__NR_rt_sigprocmask] = "rt_sigprocmask",  // 14
  [__NR_rt_sigreturn] = "rt_sigreturn",  // 15
  [__NR_ioctl] = "ioctl",  // 16
  [__NR_pread64] = "pread64",  // 17
  [__NR_pwrite64] = "pwrite64",  // 18
  [__NR_readv] = "readv",  // 19
  [__NR_writev] = "writev",  // 20
  [__NR_access] = "access",  // 21
  [__NR_pipe] = "pipe",  // 22
  [__NR_select] = "select",  // 23
  [__NR_sched_yield] = "sched_yield",  // 24
  [__NR_mremap] = "mremap",  // 25
  [__NR_msync] = "msync",  // 26
  [__NR_mincore] = "mincore",  // 27
  [__NR_madvise] = "madvise",  // 28
  [__NR_shmget] = "shmget",  // 29
  [__NR_shmat] = "shmat",  // 30
  [__NR_shmctl] = "shmctl",  // 31
  [__NR_dup] = "dup",  // 32
  [__NR_dup2] = "dup2",  // 33
  [__NR_pause] = "pause",  // 34
  [__NR_nanosleep] = "nanosleep",  // 35
  [__NR_getitimer] = "getitimer",  // 36
  [__NR_alarm] = "alarm",  // 37
  [__NR_setitimer] = "setitimer",  // 38
  [__NR_getpid] = "getpid",  // 39
  [__NR_sendfile] = "sendfile",  // 40
  [__NR_socket] = "socket",  // 41
  [__NR_connect] = "connect",  // 42
  [__NR_accept] = "accept",  // 43
  [__NR_sendto] = "sendto",  // 44
  [__NR_recvfrom] = "recvfrom",  // 45
  [__NR_sendmsg] = "sendmsg",  // 46
  [__NR_recvmsg] = "recvmsg",  // 47
  [__NR_shutdown] = "shutdown",  // 48
  [__NR_bind] = "bind",  // 49
  [__NR_listen] = "listen",  // 50
  [__NR_getsockname] = "getsockname",  // 51
  [__NR_getpeername] = "getpeername",  // 52
  [__NR_socketpair] = "socketpair",  // 53
  [__NR_setsockopt] = "setsockopt",  // 54
  [__NR_getsockopt] = "getsockopt",  // 55
  [__NR_clone] = "clone",  // 56
  [__NR_fork] = "fork",  // 57
  [__NR_vfork] = "vfork",  // 58
  [__NR_execve] = "execve",  // 59
  [__NR_exit] = "exit",  // 60
  [__NR_wait4] = "wait4",  // 61
  [__NR_kill] = "kill",  // 62
  [__NR_uname] = "uname",  // 63
  [__NR_semget] = "semget",  // 64
  [__NR_semop] = "semop",  // 65
  [__NR_semctl] = "semctl",  // 66
  [__NR_shmdt] = "shmdt",  // 67
  [__NR_msgget] = "msgget",  // 68
  [__NR_msgsnd] = "msgsnd",  // 69
  [__NR_msgrcv] = "msgrcv",  // 70
  [__NR_msgctl] = "msgctl",  // 71
  [__NR_fcntl] = "fcntl",  // 72
  [__NR_flock] = "flock",  // 73
  [__NR_fsync] = "fsync",  // 74
  [__NR_fdatasync] = "fdatasync",  // 75
  [__NR_truncate] = "truncate",  // 76
  [__NR_ftruncate] = "ftruncate",  // 77
  [__NR_getdents] = "getdents",  // 78
  [__NR_getcwd] = "getcwd",  // 79
  [__NR_chdir] = "chdir",  // 80
  [__NR_fchdir] = "fchdir",  // 81
  [__NR_rename] = "rename",  // 82
  [__NR_mkdir] = "mkdir",  // 83
  [__NR_rmdir] = "rmdir",  // 84
  [__NR_creat] = "creat",  // 85
  [__NR_link] = "link",  // 86
  [__NR_unlink] = "unlink",  // 87
  [__NR_symlink] = "symlink",  // 88
  [__NR_readlink] = "readlink",  // 89
  [__NR_chmod] = "chmod",  // 90
  [__NR_fchmod] = "fchmod",  // 91
  [__NR_chown] = "chown",  // 92
  [__NR_fchown] = "fchown",  // 93
  [__NR_lchown] = "lchown",  // 94
  [__NR_umask] = "umask",  // 95
  [__NR_gettimeofday] = "gettimeofday",  // 96
  [__NR_getrlimit] = "getrlimit",  // 97
  [__NR_getrusage] = "getrusage",  // 98
  [__NR_sysinfo] = "sysinfo",  // 99
  [__NR_times] = "times",  // 100
  [__NR_ptrace] = "ptrace",  // 101
  [__NR_getuid] = "getuid",  // 102
  [__NR_syslog] = "syslog",  // 103
  [__NR_getgid] = "getgid",  // 104
  [__NR_setuid] = "setuid",  // 105
  [__NR_setgid] = "setgid",  // 106
  [__NR_geteuid] = "geteuid",  // 107
  [__NR_getegid] = "getegid",  // 108
  [__NR_setpgid] = "setpgid",  // 109
  [__NR_getppid] = "getppid",  // 110
  [__NR_getpgrp] = "getpgrp",  // 111
  [__NR_setsid] = "setsid",  // 112
  [__NR_setreuid] = "setreuid",  // 113
  [__NR_setregid] = "setregid",  // 114
  [__NR_getgroups] = "getgroups",  // 115
  [__NR_setgroups] = "setgroups",  // 116
  [__NR_setresuid] = "setresuid",  // 117
  [__NR_getresuid] = "getresuid",  // 118
  [__NR_setresgid] = "setresgid",  // 119
  [__NR_getresgid] = "getresgid",  // 120
  [__NR_getpgid] = "getpgid",  // 121
  [__NR_setfsuid] = "setfsuid",  // 122
  [__NR_setfsgid] = "setfsgid",  // 123
  [__NR_getsid] = "getsid",  // 124
  [__NR_capget] = "capget",  // 125
  [__NR_capset] = "capset",  // 126
  [__NR_rt_sigpending] = "rt_sigpending",  // 127
  [__NR_rt_sigtimedwait] = "rt_sigtimedwait",  // 128
  [__NR_rt_sigqueueinfo] = "rt_sigqueueinfo",  // 129
  [__NR_rt_sigsuspend] = "rt_sigsuspend",  // 130
  [__NR_sigaltstack] = "sigaltstack",  // 131
  [__NR_utime] = "utime",  // 132
  [__NR_mknod] = "mknod",  // 133
  [__NR_uselib] = "uselib",  // 134
  [__NR_personality] = "personality",  // 135
  [__NR_ustat] = "ustat",  // 136
  [__NR_statfs] = "statfs",  // 137
  [__NR_fstatfs] = "fstatfs",  // 138
  [__NR_sysfs] = "sysfs",  // 139
  [__NR_getpriority] = "getpriority",  // 140
  [__NR_setpriority] = "setpriority",  // 141
  [__NR_sched_setparam] = "sched_setparam",  // 142
  [__NR_sched_getparam] = "sched_getparam",  // 143
  [__NR_sched_setscheduler] = "sched_setscheduler",  // 144
  [__NR_sched_getscheduler] = "sched_getscheduler",  // 145
  [__NR_sched_get_priority_max] = "sched_get_priority_max",  // 146
  [__NR_sched_get_priority_min] = "sched_get_priority_min",  // 147
  [__NR_sched_rr_get_interval] = "sched_rr_get_interval",  // 148
  [__NR_mlock] = "mlock",  // 149
  [__NR_munlock] = "munlock",  // 150
  [__NR_mlockall] = "mlockall",  // 151
  [__NR_munlockall] = "munlockall",  // 152
  [__NR_vhangup] = "vhangup",  // 153
  [__NR_modify_ldt] = "modify_ldt",  // 154
  [__NR_pivot_root] = "pivot_root",  // 155
  [__NR__sysctl] = "_sysctl",  // 156
  [__NR_prctl] = "prctl",  // 157
  [__NR_arch_prctl] = "arch_prctl",  // 158
  [__NR_adjtimex] = "adjtimex",  // 159
  [__NR_setrlimit] = "setrlimit",  // 160
  [__NR_chroot] = "chroot",  // 161
  [__NR_sync] = "sync",  // 162
  [__NR_acct] = "acct",  // 163
  [__NR_settimeofday] = "settimeofday",  // 164
  [__NR_mount] = "mount",  // 165
  [__NR_umount2] = "umount2",  // 166
  [__NR_swapon] = "swapon",  // 167
  [__NR_swapoff] = "swapoff",  // 168
  [__NR_reboot] = "reboot",  // 169
  [__NR_sethostname] = "sethostname",  // 170
  [__NR_setdomainname] = "setdomainname",  // 171
  [__NR_iopl] = "iopl",  // 172
  [__NR_ioperm] = "ioperm",  // 173
  [__NR_create_module] = "create_module",  // 174
  [__NR_init_module] = "init_module",  // 175
  [__NR_delete_module] = "delete_module",  // 176
  [__NR_get_kernel_syms] = "get_kernel_syms",  // 177
  [__NR_query_module] = "query_module",  // 178
  [__NR_quotactl] = "quotactl",  // 179
  [__NR_nfsservctl] = "nfsservctl",  // 180
  [__NR_getpmsg] = "getpmsg",  // 181
  [__NR_putpmsg] = "putpmsg",  // 182
  [__NR_afs_syscall] = "afs_syscall",  // 183
  [__NR_tuxcall] = "tuxcall",  // 184
  [__NR_security] = "security",  // 185
  [__NR_gettid] = "gettid",  // 186
  [__NR_readahead] = "readahead",  // 187
  [__NR_setxattr] = "setxattr",  // 188
  [__NR_lsetxattr] = "lsetxattr",  // 189
  [__NR_fsetxattr] = "fsetxattr",  // 190
  [__NR_getxattr] = "getxattr",  // 191
  [__NR_lgetxattr] = "lgetxattr",  // 192
  [__NR_fgetxattr] = "fgetxattr",  // 193
  [__NR_listxattr] = "listxattr",  // 194
  [__NR_llistxattr] = "llistxattr",  // 195
  [__NR_flistxattr] = "flistxattr",  // 196
  [__NR_removexattr] = "removexattr",  // 197
  [__NR_lremovexattr] = "lremovexattr",  // 198
  [__NR_fremovexattr] = "fremovexattr",  // 199
  [__NR_tkill] = "tkill",  // 200
  [__NR_time] = "time",  // 201
  [__NR_futex] = "futex",  // 202
  [__NR_sched_setaffinity] = "sched_setaffinity",  // 203
  [__NR_sched_getaffinity] = "sched_getaffinity",  // 204
  [__NR_set_thread_area] = "set_thread_area",  // 205
  [__NR_io_setup] = "io_setup",  // 206
  [__NR_io_destroy] = "io_destroy",  // 207
  [__NR_io_getevents] = "io_getevents",  // 208
  [__NR_io_submit] = "io_submit",  // 209
  [__NR_io_cancel] = "io_cancel",  // 210
  [__NR_get_thread_area] = "get_thread_area",  // 211
  [__NR_lookup_dcookie] = "lookup_dcookie",  // 212
  [__NR_epoll_create] = "epoll_create",  // 213
  [__NR_epoll_ctl_old] = "epoll_ctl_old",  // 214
  [__NR_epoll_wait_old] = "epoll_wait_old",  // 215
  [__NR_remap_file_pages] = "remap_file_pages",  // 216
  [__NR_getdents64] = "getdents64",  // 217
  [__NR_set_tid_address] = "set_tid_address",  // 218
  [__NR_restart_syscall] = "restart_syscall",  // 219
  [__NR_semtimedop] = "semtimedop",  // 220
  [__NR_fadvise64] = "fadvise64",  // 221
  [__NR_timer_create] = "timer_create",  // 222
  [__NR_timer_settime] = "timer_settime",  // 223
  [__NR_timer_gettime] = "timer_gettime",  // 224
  [__NR_timer_getoverrun] = "timer_getoverrun",  // 225
  [__NR_timer_delete] = "timer_delete",  // 226
  [__NR_clock_settime] = "clock_settime",  // 227
  [__NR_clock_gettime] = "clock_gettime",  // 228
  [__NR_clock_getres] = "clock_getres",  // 229
  [__NR_clock_nanosleep] = "clock_nanosleep",  // 230
  [__NR_exit_group] = "exit_group",  // 231
  [__NR_epoll_wait] = "epoll_wait",  // 232
  [__NR_epoll_ctl] = "epoll_ctl",  // 233
  [__NR_tgkill] = "tgkill",  // 234
  [__NR_utimes] = "utimes",  // 235
  [__NR_vserver] = "vserver",  // 236
  [__NR_mbind] = "mbind",  // 237
  [__NR_set_mempolicy] = "set_mempolicy",  // 238
  [__NR_get_mempolicy] = "get_mempolicy",  // 239
  [__NR_mq_open] = "mq_open",  // 240
  [__NR_mq_unlink] = "mq_unlink",  // 241
  [__NR_mq_timedsend] = "mq_timedsend",  // 242
  [__NR_mq_timedreceive] = "mq_timedreceive",  // 243
  [__NR_mq_notify] = "mq_notify",  // 244
  [__NR_mq_getsetattr] = "mq_getsetattr",  // 245
  [__NR_kexec_load] = "kexec_load",  // 246
  [__NR_waitid] = "waitid",  // 247
  [__NR_add_key] = "add_key",  // 248
  [__NR_request_key] = "request_key",  // 249
  [__NR_keyctl] = "keyctl",  // 250
  [__NR_ioprio_set] = "ioprio_set",  // 251
  [__NR_ioprio_get] = "ioprio_get",  // 252
  [__NR_inotify_init] = "inotify_init",  // 253
  [__NR_inotify_add_watch] = "inotify_add_watch",  // 254
  [__NR_inotify_rm_watch] = "inotify_rm_watch",  // 255
  [__NR_migrate_pages] = "migrate_pages",  // 256
  [__NR_openat] = "openat",  // 257
  [__NR_mkdirat] = "mkdirat",  // 258
  [__NR_mknodat] = "mknodat",  // 259
  [__NR_fchownat] = "fchownat",  // 260
  [__NR_futimesat] = "futimesat",  // 261
  [__NR_newfstatat] = "newfstatat",  // 262
  [__NR_unlinkat] = "unlinkat",  // 263
  [__NR_renameat] = "renameat",  // 264
  [__NR_linkat] = "linkat",  // 265
  [__NR_symlinkat] = "symlinkat",  // 266
  [__NR_readlinkat] = "readlinkat",  // 267
  [__NR_fchmodat] = "fchmodat",  // 268
  [__NR_faccessat] = "faccessat",  // 269
  [__NR_pselect6] = "pselect6",  // 270
  [__NR_ppoll] = "ppoll",  // 271
  [__NR_unshare] = "unshare",  // 272
  [__NR_set_robust_list] = "set_robust_list",  // 273
  [__NR_get_robust_list] = "get_robust_list",  // 274
  [__NR_splice] = "splice",  // 275
  [__NR_tee] = "tee",  // 276
  [__NR_sync_file_range] = "sync_file_range",  // 277
  [__NR_vmsplice] = "vmsplice",  // 278
  [__NR_move_pages] = "move_pages",  // 279
  [__NR_utimensat] = "utimensat",  // 280
  [__NR_epoll_pwait] = "epoll_pwait",  // 281
  [__NR_signalfd] = "signalfd",  // 282
  [__NR_timerfd_create] = "timerfd_create",  // 283
  [__NR_eventfd] = "eventfd",  // 284
  [__NR_fallocate] = "fallocate",  // 285
  [__NR_timerfd_settime] = "timerfd_settime",  // 286
  [__NR_timerfd_gettime] = "timerfd_gettime",  // 287
  [__NR_accept4] = "accept4",  // 288
  [__NR_signalfd4] = "signalfd4",  // 289
  [__NR_eventfd2] = "eventfd2",  // 290
  [__NR_epoll_create1] = "epoll_create1",  // 291
  [__NR_dup3] = "dup3",  // 292
  [__NR_pipe2] = "pipe2",  // 293
  [__NR_inotify_init1] = "inotify_init1",  // 294
  [__NR_preadv] = "preadv",  // 295
  [__NR_pwritev] = "pwritev",  // 296
  [__NR_rt_tgsigqueueinfo] = "rt_tgsigqueueinfo",  // 297
  [__NR_perf_event_open] = "perf_event_open",  // 298
  [__NR_recvmmsg] = "recvmmsg",  // 299
  [__NR_fanotify_init] = "fanotify_init",  // 300
  [__NR_fanotify_mark] = "fanotify_mark",  // 301
  [__NR_prlimit64] = "prlimit64",  // 302
  [__NR_name_to_handle_at] = "name_to_handle_at",  // 303
  [__NR_open_by_handle_at] = "open_by_handle_at",  // 304
  [__NR_clock_adjtime] = "clock_adjtime",  // 305
  [__NR_syncfs] = "syncfs",  // 306
  [__NR_sendmmsg] = "sendmmsg",  // 307
  [__NR_setns] = "setns",  // 308
  [__NR_getcpu] = "getcpu",  // 309
  [__NR_process_vm_readv] = "process_vm_readv",  // 310
  [__NR_process_vm_writev] = "process_vm_writev",  // 311
  [__NR_kcmp] = "kcmp",  // 312
  [__NR_finit_module] = "finit_module",  // 313
  [__NR_sched_setattr] = "sched_setattr",  // 314
  [__NR_sched_getattr] = "sched_getattr",  // 315
  [__NR_renameat2] = "renameat2"  // 316
};

static __thread uint64_t syscall_number = 0;
static __thread uint64_t syscall_arg0 = 0;
static __thread uint64_t syscall_arg1 = 0;
static __thread uint64_t syscall_arg2 = 0;
static __thread uint64_t syscall_arg3 = 0;
static __thread uint64_t syscall_arg4 = 0;
static __thread uint64_t syscall_arg5 = 0;
static __thread char syscall_buffer[256];

static void TraceSyscallEntry(void *, SystemCallContext ctx) {
  syscall_number = ctx.Number();
  syscall_arg0 = ctx.Arg0();
  syscall_arg1 = ctx.Arg1();
  syscall_arg2 = ctx.Arg2();
  syscall_arg3 = ctx.Arg3();
  syscall_arg4 = ctx.Arg4();
  syscall_arg5 = ctx.Arg5();
}

static void TraceSyscallExit(void *, SystemCallContext ctx) {
  Format(syscall_buffer, sizeof syscall_buffer,
         "%s\t%lx\t%lx\t%lx\t%lx\t%lx\t%lx\t= %lx\n",
         syscall_names[syscall_number], syscall_arg0, syscall_arg1,
         syscall_arg2, syscall_arg3, syscall_arg4, syscall_arg5,
         ctx.ReturnValue());
  os::Log(os::LogDebug, "%s", syscall_buffer);
  syscall_number = 0;
  syscall_arg0 = 0;
  syscall_arg1 = 0;
  syscall_arg2 = 0;
  syscall_arg3 = 0;
  syscall_arg4 = 0;
  syscall_arg5 = 0;
}

}  // namespace

// Tool that helps user-space instrumentation work.
class SystemCallTracer : public InstrumentationTool {
 public:
  virtual ~SystemCallTracer(void) = default;
  virtual void Init(InitReason) {
    AddSystemCallEntryFunction(TraceSyscallEntry);
    AddSystemCallExitFunction(TraceSyscallExit);
  }
};

// Initialize the `strace` tool.
GRANARY_CLIENT_INIT({
  if (FLAG_hook_syscalls) {
    RegisterInstrumentationTool<SystemCallTracer>("strace");
  }
})

#endif  // GRANARY_WHERE_user
