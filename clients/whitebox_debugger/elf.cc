/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

#include "clients/whitebox_debugger/probe.h"

namespace wdb {

// Looks for Granary probes within an ELF file.
static ProbeList FindGranaryProbes(const Elf64_Ehdr *header,
                                   uint64_t obj_size) {
  GRANARY_ASSERT(header->e_shoff < obj_size);
  GRANARY_ASSERT(sizeof(Elf64_Shdr) == header->e_shentsize);
  GRANARY_ASSERT((header->e_ehsize + header->e_shentsize * header->e_shnum) <
                 obj_size);

  auto num_sections = static_cast<uint64_t>(header->e_shnum);
  auto section_headers = ELF_OFFSET(header->e_shoff, Elf64_Shdr);

  GRANARY_ASSERT(section_headers[header->e_shstrndx].sh_offset < obj_size);
  auto header_names = ELF_OFFSET(section_headers[header->e_shstrndx].sh_offset,
                                 const char);

  for (auto i = 0UL; i < num_sections; ++i) {
    auto section_name = header_names + section_headers[i].sh_name;
    if (!StringsMatch(".granary_probes", section_name)) continue;
    AddProbes(reinterpret_cast<Probe *>(section_headers[i].sh_addr),
              section_headers[i].sh_size / sizeof(Probe));
    break;
  }
}

}  // namespace wdb
