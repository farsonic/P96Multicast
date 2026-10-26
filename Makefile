# P96Multicast - build with vbcc, vasm and vlink and the NDK 3.2 headers.
#
#   make                 build/Multicast.card, build/Wrap.card, mctest, wrapstat
#   make check           offset test and Wrap.card test under vamos, hunk checks,
#                        the viewer against the card's renderer (Copper)
#   make package         build/P96Multicast.lha
#   make emu-test        boot AROS in FS-UAE with the card and capture a frame
#   make emu-wrap        boot AROS in FS-UAE with Wrap.card wrapping the card
#   make emu-mirror      ... and mirroring it to a second board, over the network
#   make emu-span        ... and spanning one screen over four boards, over the network
#   make emu-copper      the viewer shows exactly what the card has, after a workload
#   make emu-prefs       MulticastPrefs installs on a clean AROS; its window
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

CARD_OBJ := $(BUILD)/libstart.o $(BUILD)/card.o $(BUILD)/config.o $(BUILD)/sender.o $(BUILD)/orders.o $(BUILD)/render.o $(BUILD)/modes.o
WRAP_OBJ := $(BUILD)/wrapstart.o $(BUILD)/wrap.o $(BUILD)/mirror.o
HDRS := $(wildcard include/*.h src/*.h)

PREFS_SRC := prefs/main.c prefs/settings.c prefs/install.c prefs/status.c prefs/gui.c

all: $(BUILD)/Multicast.card $(BUILD)/Wrap.card $(BUILD)/mctest $(BUILD)/wrapstat \
     $(BUILD)/MulticastPrefs

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.s | $(BUILD)
	$(VASM) -quiet -Fhunk -m68020 -o $@ $<

$(BUILD)/%.o: src/%.c $(HDRS) | $(BUILD)
	$(VC) $(CFLAGS) -c -o $@ $<

# a library: no startup code, libstart.o must come first
$(BUILD)/Multicast.card: $(CARD_OBJ)
	$(VC) -nostdlib -o $@ $(CARD_OBJ) -lvc -lamiga

# the same, wrapstart.o first
$(BUILD)/Wrap.card: $(WRAP_OBJ)
	$(VC) -nostdlib -o $@ $(WRAP_OBJ) -lvc -lamiga

$(BUILD)/mctest: tests/mctest.c | $(BUILD)
	$(VC) $(CFLAGS) -o $@ $< -lauto -lamiga

# libraries opened by hand (no -lauto): the command line runs without intuition
$(BUILD)/MulticastPrefs: $(PREFS_SRC) prefs/mcprefs.h $(HDRS) | $(BUILD)
	$(VC) $(CFLAGS) -Iprefs -o $@ $(PREFS_SRC) -lamiga

$(BUILD)/wrapstat: tests/wrapstat.c $(HDRS) | $(BUILD)
	$(VC) $(CFLAGS) -o $@ $< -lauto -lamiga

# test programs for make check: Fake.card is a card for Wrap.card to wrap
$(BUILD)/fakestart.o: tests/fakestart.s | $(BUILD)
	$(VASM) -quiet -Fhunk -m68020 -o $@ $<

$(BUILD)/fakecard.o: tests/fakecard.c $(HDRS) tests/fakecard.h | $(BUILD)
	$(VC) $(CFLAGS) -c -o $@ $<

$(BUILD)/Fake.card: $(BUILD)/fakestart.o $(BUILD)/fakecard.o
	$(VC) -nostdlib -o $@ $^ -lvc -lamiga

$(BUILD)/wraptest_asm.o: tests/wraptest_asm.s | $(BUILD)
	$(VASM) -quiet -Fhunk -m68020 -o $@ $<

$(BUILD)/wraptest: tests/wraptest.c $(BUILD)/wraptest_asm.o $(HDRS) tests/fakecard.h | $(BUILD)
	$(VC) $(CFLAGS) -o $@ tests/wraptest.c $(BUILD)/wraptest_asm.o -lamiga

$(BUILD)/modetest: tests/modetest.c $(HDRS) | $(BUILD)
	$(VC) $(CFLAGS) -o $@ $< -lamiga

$(BUILD)/offsets: tests/offsets.c include/boardinfo.h | $(BUILD)
	$(VC) $(CFLAGS) -o $@ $<

check: $(BUILD)/offsets $(BUILD)/Multicast.card $(BUILD)/Wrap.card $(BUILD)/Fake.card $(BUILD)/wraptest \
       $(BUILD)/modetest $(BUILD)/MulticastPrefs
	$(VAMOS) $(BUILD)/offsets
	python3 tests/check_card.py $(BUILD)/Multicast.card
	python3 tests/check_card.py $(BUILD)/Wrap.card Wrap.card
	tests/wraptest.sh "$(VAMOS)"
	tests/modetest.sh "$(VAMOS)"
	python3 tests/prefstest.py "$(VAMOS)"
	python3 tests/test_client.py
	python3 tests/test_copper.py

package: all
	dist/mkpackage.sh

emu-test: all
	emu/run_test.sh

emu-wrap: all
	emu/wrap_test.sh

emu-mirror: all
	emu/mirror_test.sh

emu-span: all
	emu/span_test.sh

emu-copper: all
	emu/copper_test.sh

emu-prefs: all
	emu/prefs_test.sh

clean:
	rm -rf $(BUILD)

.PHONY: all check package emu-test emu-wrap emu-mirror emu-span emu-copper emu-prefs clean
