/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_CPU_H_
#define ARCH_CPU_H_

namespace granary {
namespace arch {

void Relax(void);

void SynchronizePipeline(void);

unsigned long CycleCount(void);

bool TryDisableInterrupts(void);
void EnableInterrupts(void);

bool TryDisablePageProtection(void);
void EnablePageProtection(void);

}  // namespace arch
}  // namespace granary

#endif  // ARCH_CPU_H_
