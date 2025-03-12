###############################################################################
# Makefile for Debug Builds of SDL3 C Code (macOS) using SDL3.xcframework
#
# This Makefile hardcodes the list of source files from the "SDL3Skeleton Shared"
# directory (ensuring that main.c is compiled first) and compiles them into an
# executable named "debug_app". It uses the macOS version of SDL3.xcframework and
# links against Cocoa so that SDL's callback-based entry point works correctly.
#
# Usage:
#   make       - Build the executable "debug_app"
#   make clean - Remove the generated object files and executable.
###############################################################################

# -----------------------------------------------------------------------------
# Compiler and Linker Settings
# -----------------------------------------------------------------------------
CC = clang

# Compiler flags:
#   -Wall, -Wextra: Enable common warnings.
#   -g: Include debugging symbols.
#   -I: Include the SDL3 headers from the macOS framework.
CFLAGS = -Wall -Wextra -g -I./SDL3.xcframework/macos-arm64_x86_64/SDL3.framework/Headers

# Linker flags:
#   -F: Specify the directory for the SDL3 framework.
#   -framework SDL3: Link against the SDL3 framework.
#   -framework Cocoa: Link against Cocoa (needed for SDL_main startup).
#   -rpath: Set the runtime search path for the framework.
LDFLAGS = -F./SDL3.xcframework/macos-arm64_x86_64 -framework SDL3 -framework Cocoa -rpath ./SDL3.xcframework/macos-arm64_x86_64

# -----------------------------------------------------------------------------
# Source Files (Hardcoded in Order)
# -----------------------------------------------------------------------------
# Set SRC_DIR to the directory containing your source files.
# Note: The space in the directory name is escaped with a backslash.
SRC_DIR = SDL3Skeleton\ Shared

# Hardcode the list of source files, ensuring main.c is first.
SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/example.c $(SRC_DIR)/game.c $(SRC_DIR)/input.c $(SRC_DIR)/platform.c $(SRC_DIR)/render.c

# -----------------------------------------------------------------------------
# Object Files
# -----------------------------------------------------------------------------
# We convert the source file names into object file names.
# This strips the directory so that the object files (e.g. main.o) are created
# in the current directory.
OBJS = main.o example.o game.o input.o platform.o render.o

# -----------------------------------------------------------------------------
# Final Executable Name
# -----------------------------------------------------------------------------
TARGET = debug_app

# -----------------------------------------------------------------------------
# Build Rules
# -----------------------------------------------------------------------------

# The default target builds the executable.
all: $(TARGET)
	@echo "Build complete! Run './$(TARGET)' to execute."

# Link all object files to create the final executable.
$(TARGET): $(OBJS)
	@echo "Linking object files..."
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)

# Pattern rule: compile each .c file (from SRC_DIR) into a .o file.
%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c "$<" -o "$@"

# Clean rule: remove object files and the final executable.
clean:
	@echo "Cleaning up..."
	rm -f $(OBJS) $(TARGET)

# Declare these targets as phony.
.PHONY: all clean

