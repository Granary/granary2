# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := strace
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o

$(CLIENT_OBJ): $(CLIENT_SRC_DIR)/$(CLIENT_NAME).cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) -Wno-c99-extensions $(GRANARY_CXX_FLAGS) -c $< -o $@

ifeq (user,$(GRANARY_WHERE))
all: $(CLIENT_OBJ)
else
all:
endif
