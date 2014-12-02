/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_DATA_REACTOR_ARCH_X86_64_H_
#define CLIENTS_DATA_REACTOR_ARCH_X86_64_H_

enum : uint32_t {
  DR7_BREAK_ON_EXEC  = 0,
  DR7_BREAK_ON_WRITE = 1,
  DR7_BREAK_ON_RW    = 3,
};

enum : uint32_t {
  DR7_LEN_1 = 0,
  DR7_LEN_2 = 1,
  DR7_LEN_4 = 3,
};

union dr7_t {
  uint64_t value;
  struct {
    uint32_t l0:1;
    uint32_t g0:1;
    uint32_t l1:1;
    uint32_t g1:1;
    uint32_t l2:1;
    uint32_t g2:1;
    uint32_t l3:1;
    uint32_t g3:1;
    uint32_t le:1;
    uint32_t ge:1;
    uint32_t pad1:3;
    uint32_t gd:1;
    uint32_t pad2:2;
    uint32_t rw0:2;
    uint32_t len0:2;
    uint32_t rw1:2;
    uint32_t len1:2;
    uint32_t rw2:2;
    uint32_t len2:2;
    uint32_t rw3:2;
    uint32_t len3:2;
    uint32_t reserved;
  };
} __attribute__((packed));

static_assert(sizeof(dr7_t) == sizeof(uint64_t),
              "Invalid structure packing for `union dr7_t`.");

#endif  // CLIENTS_DATA_REACTOR_ARCH_X86_64_H_
