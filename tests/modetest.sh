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
# without MODES=: none, P96's settings are the modes
printf 'VRAM=4\n' > "$DIR/env/Multicast.prefs"
run 0
# MODES=, with sizes that fit in only some depths in 4 MB
printf 'VRAM=4\nMODES=320x240,1920x1200\n' > "$DIR/env/Multicast.prefs"
run 2 1920x1200
# MODES= beyond MAXWIDTH, which it raises
printf 'VRAM=8\nMODES=640x480, 1280x960,2560x1024\n' > "$DIR/env/Multicast.prefs"
run 3 2560x1024
# MODES=NONE: P96's settings
printf 'VRAM=1\nMODES=NONE\n' > "$DIR/env/Multicast.prefs"
run 0
# P96 has a 640x480 of its own in the list already: the card skips that size
printf 'VRAM=4\nMODES=640x480,800x600\n' > "$DIR/env/Multicast.prefs"
run 1 800x600 PRE
