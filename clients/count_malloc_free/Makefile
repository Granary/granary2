# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all generated

include $(GRANARY_SRC_DIR)/Makefile.inc

FIND_SYM := $(GRANARY_CLIENTS_SRC_DIR)/user/find_symbol_offset.py

$(CLIENT_GEN_DIR)/offsets.h: Makefile
	@mkdir -p $(CLIENT_GEN_DIR)
	@-rm $(@) $(GRANARY_DEV_NULL)
	@$(GRANARY_PYTHON) $(FIND_SYM) malloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) valloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) pvalloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) aligned_alloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) memalign libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) posix_memalign libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) calloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) realloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) free libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _ZdlPv libstdc++ >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _ZdaPv libstdc++ >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _Znwm libstdc++ >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _Znam libstdc++ >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _ZdlPv libc++ >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _ZdaPv libc++ >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _Znwm libc++ >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) _Znam libc++ >> $(@)

all: $(CLIENT_OBJ)
generated: $(CLIENT_GEN_DIR)/offsets.h
