# Copyright 2014 Peter Goodman, all rights reserved.

.PHONY: all

include ../../Makefile.inc

CLIENT_NAME := user
CLIENT_SRC_DIR := $(GRANARY_SRC_DIR)/clients/$(CLIENT_NAME)
CLIENT_BIN_DIR := $(GRANARY_BIN_DIR)/clients/$(CLIENT_NAME)
CLIENT_OBJ := $(GRANARY_BIN_DIR)/$(CLIENT_NAME)_client.o

CLIENT_SRC_FILES = $(wildcard $(CLIENT_SRC_DIR)/*.cc)
CLIENT_OBJ_FILES := $(subst $(CLIENT_SRC_DIR),$(CLIENT_BIN_DIR),$(CLIENT_SRC_FILES:.cc=.o))

$(CLIENT_OBJ): $(CLIENT_OBJ_FILES)
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_LD) -g -r $(CLIENT_OBJ_FILES) -o $(CLIENT_OBJ)

all: $(CLIENT_OBJ)
