CC = gcc
CFLAGS = -Wall -g -Iinclude
LDFLAGS_RAYLIB = -lraylib -lm
LDFLAGS_TERM = -ltermbox -lm

# Directories
SRC_DIR = src
BIN_DIR = bin

# Core source files
READER_SRC = $(SRC_DIR)/krb_reader.c
RAYLIB_RENDERER_SRC = $(SRC_DIR)/raylib_renderer.c
TERM_RENDERER_SRC = $(SRC_DIR)/term_renderer.c

# Custom components source files
CUSTOM_COMPONENTS_SRC = $(SRC_DIR)/custom_components.c
CUSTOM_TABBAR_SRC = $(SRC_DIR)/custom_tabbar.c

# All custom component sources
CUSTOM_COMPONENTS_ALL = $(CUSTOM_COMPONENTS_SRC) $(CUSTOM_TABBAR_SRC)

# Default renderer
RENDERER ?= raylib

# Define the flag needed to enable the main() in raylib_renderer.c
RAYLIB_STANDALONE_FLAG = -DBUILD_STANDALONE_RENDERER

# Targets
all: $(BIN_DIR)/krb_renderer

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Renderer-specific targets
$(BIN_DIR)/krb_renderer: $(READER_SRC) $(SRC_DIR)/$(RENDERER)_renderer.c $(CUSTOM_COMPONENTS_ALL) | $(BIN_DIR)
ifeq ($(RENDERER),raylib)
	# Add the RAYLIB_STANDALONE_FLAG when compiling raylib with custom components
	@echo "Building Standalone Raylib Renderer with Custom Components..."
	$(CC) $(CFLAGS) $(RAYLIB_STANDALONE_FLAG) -o $@ $^ $(LDFLAGS_RAYLIB)
else ifeq ($(RENDERER),term)
	# Terminal renderer (custom components not needed for term renderer)
	@echo "Building Terminal Renderer..."
	$(CC) $(CFLAGS) -o $@ $(READER_SRC) $(TERM_RENDERER_SRC) $(LDFLAGS_TERM)
else
	@echo "Error: Unknown renderer '$(RENDERER)'. Use 'raylib', or 'term'."
	@exit 1
endif
	@echo "Build successful: $@"

# Debug build with more verbose output
debug: CFLAGS += -DDEBUG -O0
debug: $(BIN_DIR)/krb_renderer
	@echo "Debug build complete"

# Release build with optimizations
release: CFLAGS += -O2 -DNDEBUG
release: $(BIN_DIR)/krb_renderer
	@echo "Release build complete"

# Test build that compiles but doesn't link (for syntax checking)
test-compile: $(READER_SRC) $(RAYLIB_RENDERER_SRC) $(CUSTOM_COMPONENTS_ALL)
	@echo "Testing compilation..."
	$(CC) $(CFLAGS) $(RAYLIB_STANDALONE_FLAG) -c $(READER_SRC) -o /tmp/krb_reader.o
	$(CC) $(CFLAGS) $(RAYLIB_STANDALONE_FLAG) -c $(RAYLIB_RENDERER_SRC) -o /tmp/raylib_renderer.o
	$(CC) $(CFLAGS) $(RAYLIB_STANDALONE_FLAG) -c $(CUSTOM_COMPONENTS_SRC) -o /tmp/custom_components.o
	$(CC) $(CFLAGS) $(RAYLIB_STANDALONE_FLAG) -c $(CUSTOM_TABBAR_SRC) -o /tmp/custom_tabbar.o
	@echo "Compilation test passed"
	@rm -f /tmp/krb_reader.o /tmp/raylib_renderer.o /tmp/custom_components.o /tmp/custom_tabbar.o

# Individual component compilation (for testing)
$(BIN_DIR)/test_custom_components: $(READER_SRC) $(CUSTOM_COMPONENTS_ALL) | $(BIN_DIR)
	@echo "Building custom components test..."
	$(CC) $(CFLAGS) -DTEST_CUSTOM_COMPONENTS -o $@ $^ $(LDFLAGS_RAYLIB)

# Clean
clean:
	@echo "Cleaning build directory..."
	rm -rf $(BIN_DIR)
	@echo "Cleaning temporary files..."
	rm -f /tmp/krb_*.o /tmp/raylib_*.o /tmp/custom_*.o

# Install (copy to system location)
install: $(BIN_DIR)/krb_renderer
	@echo "Installing krb_renderer..."
	sudo cp $(BIN_DIR)/krb_renderer /usr/local/bin/
	@echo "Installation complete"

# Uninstall
uninstall:
	@echo "Uninstalling krb_renderer..."
	sudo rm -f /usr/local/bin/krb_renderer
	@echo "Uninstallation complete"

# Show help
help:
	@echo "KRB Renderer Makefile"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  all                    - Build default renderer (raylib)"
	@echo "  raylib                 - Build raylib renderer"
	@echo "  term                   - Build terminal renderer"
	@echo "  debug                  - Build debug version"
	@echo "  release                - Build optimized release version"
	@echo "  test-compile           - Test compilation without linking"
	@echo "  clean                  - Clean build directory"
	@echo "  install                - Install to system"
	@echo "  uninstall              - Remove from system"
	@echo "  help                   - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  RENDERER={raylib|term} - Choose renderer (default: raylib)"
	@echo "  CC=compiler            - Choose compiler (default: gcc)"
	@echo ""
	@echo "Examples:"
	@echo "  make                   - Build raylib renderer"
	@echo "  make raylib            - Build raylib renderer"
	@echo "  make term              - Build terminal renderer"
	@echo "  make debug             - Build debug version"
	@echo "  make RENDERER=raylib   - Explicitly build raylib renderer"

# Phony targets
.PHONY: all clean raylib term debug release test-compile install uninstall help

# These targets simply re-invoke make with the RENDERER variable set
raylib:
	$(MAKE) RENDERER=raylib

term:
	$(MAKE) RENDERER=term