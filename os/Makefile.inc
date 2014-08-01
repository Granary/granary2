
include ../Makefile.inc

# Lint all Granary OS code, independent of the OS, and independent of user or
# kernel space.
lint:
	@echo "Linting Granary files."
	@$(GRANARY_LINT) $(shell find $(GRANARY_SRC_DIR)/os -type f -name '*.h' -or -name '*.cc')
