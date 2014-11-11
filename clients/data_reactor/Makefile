# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all generated

include $(GRANARY_SRC_DIR)/Makefile.inc

$(CLIENT_GEN_DIR)/offsets.h:
	@mkdir -p $(@D)
	@-rm $(@) $(GRANARY_DEV_NULL)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENTS_SRC_DIR)/user/find_symbol_offset.py \
		malloc libc >> $(@)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENTS_SRC_DIR)/user/find_symbol_offset.py \
		calloc libc >> $(@)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENTS_SRC_DIR)/user/find_symbol_offset.py \
		realloc libc >> $(@)
	@$(GRANARY_PYTHON) $(GRANARY_CLIENTS_SRC_DIR)/user/find_symbol_offset.py \
		free libc >> $(@)

all: $(CLIENT_OBJ)
generated: $(CLIENT_GEN_DIR)/offsets.h
