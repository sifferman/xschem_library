# Makefile for xschem_lite - Lightweight xschem .sch parser and SPICE netlister

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2
LDFLAGS =

# Target executable
TARGET = xschem_lite

# Source files
SRCS = main.cpp xschem_lite.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = xschem_lite.h

# PDK configuration (override with environment variables or make arguments)
PDK_ROOT ?= /home/ethan/tools/ciel-pdks
PDK ?= sky130A
XSCHEMRC ?= $(PDK_ROOT)/$(PDK)/libs.tech/xschem/xschemrc

# Test schematic
TEST_SCH = schematics/sky130_fd_sc_hd__dfxtp_1.sch
TEST_OUT = output.spice

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Build as a static library
libxschem_lite.a: xschem_lite.o
	ar rcs $@ $^

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET) libxschem_lite.a $(TEST_OUT)

# Run a test with the sky130 schematic using xschemrc
test: $(TARGET)
	@echo "=== Testing with $(TEST_SCH) ==="
	@echo "Using xschemrc: $(XSCHEMRC)"
	@echo ""
	./$(TARGET) --xschemrc $(XSCHEMRC) $(TEST_SCH) $(TEST_OUT)
	@echo ""
	@echo "=== Generated netlist (first 30 lines) ==="
	@head -30 $(TEST_OUT)

# Run info mode to see schematic details
info: $(TARGET)
	./$(TARGET) --xschemrc $(XSCHEMRC) $(TEST_SCH) --info

# Compare with reference netlist
compare: $(TARGET)
	./$(TARGET) --xschemrc $(XSCHEMRC) $(TEST_SCH) $(TEST_OUT)
	@echo "=== Comparing with reference netlist ==="
	-diff -u nonlibraryflow/netlists/sky130_fd_sc_hd__dfxtp_1.spice $(TEST_OUT) | head -50

# Install (optional)
PREFIX ?= /usr/local
install: $(TARGET)
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/

.PHONY: all clean test info compare install
