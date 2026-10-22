# P96Multicast - build with vbcc, vasm and vlink and the NDK 3.2 headers.
#
#   make                 build/Multicast.card and build/mctest
#   make check           offset test under vamos, hunk sanity checks
#   make emu-test        boot AROS in FS-UAE with the card and capture a frame
#
# Toolchain locations (see docs/BUILD.md):
VBCC ?= /home/user/toolchain/vbcc
NDK  ?= /home/user/toolchain/ndk
NETINC := $(NDK)/SANA+RoadshowTCP-IP/netinclude

VC    := PATH=$(VBCC)/bin:$$PATH VBCC=$(VBCC) $(VBCC)/bin/vc +aos68k
VASM  := $(VBCC)/bin/vasmm68k_mot
VAMOS ?= vamos -C 68020

BUILD := build
CFLAGS := -c99 -O2 -cpu=68020 -Iinclude -Isrc -I$(NDK)/Include_H -I$(NETINC)

CARD_OBJ := $(BUILD)/libstart.o $(BUILD)/card.o $(BUILD)/config.o $(BUILD)/sender.o
HDRS := $(wildcard include/*.h src/*.h)

all: $(BUILD)/Multicast.card $(BUILD)/mctest

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/libstart.o: src/libstart.s | $(BUILD)
	$(VASM) -quiet -Fhunk -m68020 -o $@ $<

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(VC) $(CFLAGS) -c -o $@ $<

# a library: no startup code, libstart.o must come first
$(BUILD)/Multicast.card: $(CARD_OBJ)
	$(VC) -nostdlib -o $@ $(CARD_OBJ) -lvc -lamiga

$(BUILD)/mctest: tests/mctest.c | $(BUILD)
	$(VC) $(CFLAGS) -o $@ $< -lauto -lamiga

$(BUILD)/offsets: tests/offsets.c include/boardinfo.h | $(BUILD)
	$(VC) $(CFLAGS) -o $@ $<

check: $(BUILD)/offsets $(BUILD)/Multicast.card
	$(VAMOS) $(BUILD)/offsets
	python3 tests/check_card.py $(BUILD)/Multicast.card

emu-test: all
	emu/run_test.sh

clean:
	rm -rf $(BUILD)

.PHONY: all check emu-test clean
