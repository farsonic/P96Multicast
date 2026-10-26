#!/bin/sh
# P96Multicast - run tests/modetest.c under vamos: the screen modes the built
# Multicast.card makes, with the built-in list and with MODES=. vamos loads
# only files named *.library, and has no graphics.library (the card opens it
# for WaitTOF), so a fake one stands in.
#
#   tests/modetest.sh [VAMOS]          (make check)
set -eu
cd "$(dirname "$0")/.."
VAMOS=${1:-vamos -C 68020}
DIR=$(pwd)/build/modetest.d

rm -rf "$DIR"
mkdir -p "$DIR/libs" "$DIR/env"
cp build/Multicast.card "$DIR/libs/multicast.library"
run() {
	$VAMOS -c tests/vamos-gfx.ini -m 11000 -V "p96:$(pwd)" \
		-a LIBS:p96:build/modetest.d/libs -a ENV:p96:build/modetest.d/env \
		build/modetest LIBS:multicast.library "$@"
}
# built-in list up to 1920x1200, 4 MB: 1920x1200 fits only in 8 and 16 bit
printf 'VRAM=4\n' > "$DIR/env/Multicast.prefs"
run 11 1920x1200
# MAXWIDTH/MAXHEIGHT trim the built-in list
printf 'VRAM=4\nMAXWIDTH=1024\nMAXHEIGHT=768\n' > "$DIR/env/Multicast.prefs"
run 4 1024x768
# MODES=, with a size P96 has no number for
printf 'VRAM=8\nMODES=640x480, 1280x960,2560x1024\n' > "$DIR/env/Multicast.prefs"
run 3 2560x1024
# MODES=NONE: P96's settings
printf 'VRAM=1\nMODES=NONE\n' > "$DIR/env/Multicast.prefs"
run 0
# P96 has modes of its own for the board already (Picasso96Mode settings):
# without MODES= the card adds none; with it, none P96 has (640x480, 0x5003)
printf 'VRAM=4\n' > "$DIR/env/Multicast.prefs"
run 0 - PRE
printf 'VRAM=4\nMODES=640x480,800x600\n' > "$DIR/env/Multicast.prefs"
run 1 800x600 PRE
