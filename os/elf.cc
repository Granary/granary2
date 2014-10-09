/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cstring.h"
#include "granary/base/new.h"

#include "os/elf.h"

namespace granary {
namespace os {
namespace {

// Returns an array of the section headers from an ELF.
static Array<const Elf64_Shdr> SectionHeaders(const Elf64_Ehdr *header) {
  auto num_sections = static_cast<unsigned>(header->e_shnum);
  auto image = reinterpret_cast<uintptr_t>(header);
  auto section_headers = reinterpret_cast<const Elf64_Shdr *>(
      image + header->e_shoff);
  return {section_headers, num_sections};
}

// Returns a pointer to the symbol table of the ELF. This finds the section
// header that represents the string table, then uses the section's file
// offset to find the symbol table base.
static const char *SymbolTable(const Elf64_Ehdr *header,
                               const Array<const Elf64_Shdr> &section_headers) {
  auto symtab_offset = section_headers[header->e_shstrndx].sh_offset;
  auto image = reinterpret_cast<uintptr_t>(header);
  return reinterpret_cast<const char *>(image + symtab_offset);
}

}  // namespace

ELFImage::ELFImage(const void *image, uint64_t image_size_)
    : header(reinterpret_cast<const Elf64_Ehdr *>(image)),
      image_size(image_size_),
      section_headers(SectionHeaders(header)),
      symbol_table(SymbolTable(header, section_headers)) {}

// Allocates an `ELFImage` object. Returns `nullptr` if the object doesn't
// look like an ELF.
ELFImage *ELFImage::Load(const void *image, uint64_t image_size_) {
  if (memcmp(ELFMAG, str, SELFMAG)) return nullptr;
  auto addr = OperatorNewAllocator<ELFImage>::Allocate();
  return new (addr) ELFImage(image, image_size_);
}

// Delete an `ELFImage` object.
void ELFImage::operator delete(void *address) {
  OperatorNewAllocator<ELFImage>::Free(address);
}

}  // namespace os
}  // namespace granary
