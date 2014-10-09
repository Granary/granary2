/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_ELF_H_
#define OS_ELF_H_

#ifdef GRANARY_INTERNAL
# include <elf.h>
#endif  // GRANARY_INTERNAL

#include "granary/base/array.h"
#include "granary/base/base.h"

namespace granary {
namespace os {

// Represents and ELF image file.
class ELFImage {
 public:
  GRANARY_INTERNAL_DEFINITION
  ELFImage(const void *image, uint64_t image_size_);

  GRANARY_INTERNAL_DEFINITION
  static ELFImage *Load(const void *image, uint64_t image_size_);

  GRANARY_INTERNAL_DEFINITION
  static void operator delete(void *address);

 private:
  ELFImage(void) = delete;

  // Pointer to the main header of the ELF.
  GRANARY_INTERNAL_DEFINITION Elf64_Ehdr *header;

  // Total size of the ELF image.
  GRANARY_INTERNAL_DEFINITION uint64_t image_size;

  // Array of section headers.
  GRANARY_INTERNAL_DEFINITION Array<const Elf64_Shdr> section_headers;

  // Pointer into the symbol table.
  GRANARY_INTERNAL_DEFINITION const char *symbol_table;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ELFImage);
};

}  // namespace os
}  // namespace granary

#endif  // OS_ELF_H_
