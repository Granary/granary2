# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all generated

include $(GRANARY_SRC_DIR)/Makefile.inc

FIND_SYM := $(GRANARY_CLIENTS_SRC_DIR)/user/find_symbol_offset.py

$(CLIENT_GEN_DIR)/offsets.h:
	@mkdir -p $(CLIENT_GEN_DIR)
	@-rm $(@) $(GRANARY_DEV_NULL)
	@$(GRANARY_PYTHON) $(FIND_SYM) malloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) calloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) realloc libc >> $(@)
	@$(GRANARY_PYTHON) $(FIND_SYM) free libc >> $(@)

all: $(CLIENT_OBJ)
generated: $(CLIENT_GEN_DIR)/offsets.h
