/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

#ifdef GRANARY_WHERE_kernel

using namespace granary;

GRANARY_DEFINE_string(attach_to_syscalls, "*",
    "Comma-separated list of specific system call numbers "
    "to which Granary should be attached. The default "
    "value is `*`, which means that Granary should attach to "
    "all system calls.",

    "kernel");

// TODO:
//  1) Make sure all exception tables are sorted.
//      --> might be able to enforce this in AllocModule, but be wary of where
//          the kernel sorts the tables, as it might be during a different
//          notifier state.
//      --> might be able to kmalloc and memcpy my own versions of the kernel's
//          extables. That could be best solution.
//  2) Look into the extable_ex or whatever. There were macros that used a
//     different fixup table which displaced the recovering address by a
//     different amount.
//  3) Work on only instrumenting a set of system calls.
//  4) Figure out why some extable entries point to weird code. Wtf is going
//     on with that?

static TinySet<int, 10> syscalls;
#if 0
class IndirectFromSched : public IndexableMetaData<IndirectFromSched> {
 public:
  IndirectFromSched(void)
      : from_schedule(false) {}

  bool Equals(const IndirectFromSched *that) const {
    return that->from_schedule == from_schedule;
  }

  bool from_schedule;
};

GRANARY_DECLARE_bool(debug_log_fragments);


static std::atomic<int> ici(ATOMIC_VAR_INIT(0));
extern "C" AppPC indirect_calls[100] = {0ULL};
#endif
// Tool that implements several kernel-space special cases for instrumenting
// common binaries.
class KernelSpaceInstrumenter : public InstrumentationTool {
 public:
  bool decode;
  bool go_native;
  int num_cfis;

  KernelSpaceInstrumenter(void)
      : decode(false),
        go_native(false),
        num_cfis(0) {
    //FLAG_debug_log_fragments = false;
  }

  virtual ~KernelSpaceInstrumenter(void) = default;

  void InstrumentSyscall(BlockFactory *factory, CompensationBasicBlock *block,
                         int syscall) {
    if ('*' == FLAG_attach_to_syscalls[0] || syscalls.Contains(syscall)) {
      return;
    }
    for (auto succ : block->Successors()) {
      factory->RequestBlock(succ.block, REQUEST_NATIVE);
    }
  }

  virtual void InstrumentEntryPoint(BlockFactory *factory,
                                    CompensationBasicBlock *block,
                                    EntryPointKind kind, int category) {
    if (ENTRYPOINT_KERNEL_SYSCALL == kind) {
      InstrumentSyscall(factory, block, category);
    }
  }
#if 0

  virtual void Init(InitReason) {
    RegisterMetaData<IndirectFromSched>();
  }
#endif
#if 0
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->Blocks()) {
      for (auto succ : block->Successors()) {
        auto addr = reinterpret_cast<uintptr_t>(succ.cfi->DecodedPC());
        if ((0xffffffff81678760 <= addr && addr <= 0xffffffff81678ef7) ||  // __schedule
            //(0xffffffff81678f00 <= addr && addr <= 0xffffffff81678f6c) ||  // schedule
            //(0xffffffff816808a0 <= addr && addr <= 0xffffffff816808b9) ||  // native_load_gs_index
            //(0xffffffff8167f5a6 <= addr && addr <= 0xffffffff8167f60a)) {  // int_with_check
            false) {
          factory->RequestBlock(succ.block, REQUEST_NATIVE);
        }
      }
    }
  }
#endif
#if 0
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    GRANARY_UNUSED(factory);

    if (InSchedule(cfg->EntryBlock())) {
      for (auto block : cfg->Blocks()) {
        for (auto succ : block->Successors()) {
          if (!succ.cfi->IsFunctionCall()) {
            if (InSchedule(block) && !InSchedule(succ.block)
                && !succ.cfi->HasIndirectTarget()) {
              asm("nop;");
              GRANARY_USED(block);
              GRANARY_USED(succ);
            }
            factory->RequestBlock(succ.block, REQUEST_CHECK_LCFG);
          } else {
            factory->RequestBlock(succ.block, REQUEST_NATIVE);
          }
        }
      }
    }

  }

  static bool InSchedule(BasicBlock *block) {
    if (IsA<InstrumentedBasicBlock *>(block) &&
        !IsA<IndirectBasicBlock *>(block) &&
        !IsA<ReturnBasicBlock *>(block)) {
      auto entry_addr = reinterpret_cast<uintptr_t>(block->StartAppPC());
      return 0xffffffff81678760 <= entry_addr &&
              entry_addr <= 0xffffffff81678ef7;
    }
    return false;
  }
#endif
#if 0
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    GRANARY_UNUSED(factory);

    auto entry = cfg->EntryBlock();
    auto entry_addr = reinterpret_cast<uintptr_t>(entry->StartAppPC());

    if (0xffffffff81678760 <= entry_addr && entry_addr <= 0xffffffff81678ef7) {
      if (!decode) {
        decode = true;
        FLAG_debug_log_fragments = true;
      } else {
        // WORKS: if (++num_cfis > 10) {
        // WORKS: if (++num_cfis > 20) {
        // WORKS: if (++num_cfis > 23) {
        if (++num_cfis > 23) {
        // FAILS: if (++num_cfis > 24) {
        // FAILS: if (++num_cfis > 25) {
        // FAILS: if (++num_cfis > 30) {
          go_native = true;
        }
      }
    }

#if 0
    auto entry_meta = GetMetaData<IndirectFromSched>(entry);
    if (entry_meta->from_schedule) {
      auto target_pc = entry->StartAppPC();
      indirect_calls[ici.fetch_add(1)] = target_pc;
    }
#endif
    for (auto block : cfg->NewBlocks()) {
#if 0
      for (auto succ : block->Successors()) {
        auto addr = reinterpret_cast<uintptr_t>(succ.cfi->DecodedPC());
        if (0xffffffff81678760 <= addr && addr <= 0xffffffff81678ef7 &&
            succ.cfi->IsFunctionCall() && succ.cfi->HasIndirectTarget()) {
          auto target_meta = GetMetaData<IndirectFromSched>(succ.block);
          target_meta->from_schedule = true;
        }
      }
#else
      if (auto direct_block = DynamicCast<DirectBasicBlock *>(block)) {
        auto addr = reinterpret_cast<uintptr_t>(direct_block->StartAppPC());
        (void) addr;

        if (0xffffffff810ba6c0 == addr /* rcu_note_context_switch> */ ||
            0xffffffff8167de90 == addr /* _raw_spin_lock_irq> */ ||
            0xffffffff81086560 == addr /* perf_event_task_sched_out> */ ||
            0xffffffff810a4470 == addr /* lock_release> */ ||
            0xffffffff8100b480 == addr /* __switch_to> */ ||
            0xffffffff81086b60 == addr /* finish_task_switch> */ ||
            0xffffffff81085990 == addr /* update_rq_clock> */ ||
            0xffffffff8167e610 == addr /* _raw_spin_lock_irqsave> */ ||
            0xffffffff8167e000 == addr /* _raw_spin_unlock_irqrestore> */ ||
            0xffffffff810805f0 == addr /* hrtimer_cancel> */ ||
            0xffffffff81087f00 == addr /* deactivate_task> */ ||
            0xffffffff81076e90 == addr /* wq_worker_sleeping> */ ||
            0xffffffff8167dd60 == addr /* _raw_spin_trylock> */ ||
            0xffffffff81088080 == addr /* ttwu_do_wakeup> */ ||
            0xffffffff81086dc0 == addr /* ttwu_stat> */ ||
            0xffffffff8167dfd0 == addr /* _raw_spin_unlock> */ ||
            0xffffffff8167dfd0 == addr /* _raw_spin_unlock> */ ||
            0xffffffff8167de40 == addr /* _raw_spin_lock> */ ||
            0xffffffff8167de40 == addr /* _raw_spin_lock> */ ||
            0xffffffff810a23d0 == addr /* lock_is_held> */ ||
            0xffffffff8105bf70 == addr /* warn_slowpath_null> */ ||
            0xffffffff81087ee0 == addr /* activate_task> */ ||
            0xffffffff81076e30 == addr /* wq_worker_waking_up> */ ||
            0xffffffff8167215c == addr /* pick_next_task> */ ||
            0xffffffff816720f4 == addr /* __schedule_bug> */ ||
            0xffffffff8105bf70 == addr /* warn_slowpath_null> */ ||
            0xffffffff8105bf70 == addr /* warn_slowpath_null> */ ||
            0xffffffff81111c10 == addr /* __context_tracking_task_switch> */ ||
            0xffffffff810980d0 == addr /* idle_balance> */ ||
            0xffffffff81084f80 == addr /* cpumask_set_cpu> */ ||
            0xffffffff81084a20 == addr /* load_cr3> */ ||
            0xffffffff8167e040 == addr /* _raw_spin_unlock_irq> */ ||
            0xffffffff81672162 == addr /* switch_mm> */ ||

            0xffffffff81094e10 == addr /* put_prev_task_fair */ ||
            0xffffffff8108e900 == addr /* put_prev_task_idle */ ||
            0xffffffff81098f30 == addr /* pick_next_task_rt */ ||
            0xffffffff8108e8e0 == addr /* pick_next_task_idle */ ||
            0xffffffff8108e970 == addr /* pre_schedule_idle */ ||
            0xffffffff81099210 == addr /* pre_schedule_rt */ ||
            0xffffffff8108e960 == addr /* post_schedule_idle */ ||
            0xffffffff8109aa20 == addr /* post_schedule_rt */ ||
            0xffffffff810917c0 == addr /* pick_next_task_fair */ ||
            0xffffffff8109ac70 == addr /* pick_next_task_stop */ ||
            0xffffffff81046830 == addr /* 0xffffffff81046830 */ ||

            0xffffffff81678aee == addr) {
          factory->RequestBlock(direct_block, REQUEST_NATIVE);

        } else if (decode &&
                   0xffffffff81678760 <= addr && addr <= 0xffffffff81678ef7) {
          factory->RequestBlock(direct_block,
                                go_native ? REQUEST_NATIVE : REQUEST_CHECK_LCFG);
          FLAG_debug_log_fragments = true;
        }
      }
#endif
    }
  }
#endif
};

// Initialize the `kernel` tool.
GRANARY_CLIENT_INIT({

  // TODO(pag): Distinguish between client load and tool init.
  if (HAS_FLAG_attach_to_syscalls) {
    ForEachCommaSeparatedString<4>(
        FLAG_attach_to_syscalls,
        [] (const char *syscall_str) {
          int syscall_num(-1);
          if (1 == DeFormat(syscall_str, "%d", &syscall_num) &&
              0 <= syscall_num) {
            syscalls.Add(syscall_num);
          }
        });
  }
  RegisterInstrumentationTool<KernelSpaceInstrumenter>("kernel");
})

#endif  // GRANARY_WHERE_kernel
