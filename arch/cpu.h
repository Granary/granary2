/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_CPU_H_
#define GRANARY_ARCH_CPU_H_

namespace granary {
namespace cpu {

void Relax(void);

void SynchronizePipeline(void);

unsigned long CycleCount(void);

}  // namespace cpu
}  // namespace granary

#endif  // GRANARY_ARCH_CPU_H_
