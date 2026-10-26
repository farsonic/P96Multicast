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
mkdir -p "$DIR/libs" "$DIR/env" "$DIR/devs"
cp build/Multicast.card "$DIR/libs/multicast.library"
run() {
	$VAMOS -c tests/vamos-gfx.ini -m 11000 -V "p96:$(pwd)" \
		-a LIBS:p96:build/modetest.d/libs -a ENV:p96:build/modetest.d/env \
		-a DEVS:p96:build/modetest.d/devs \
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
# Picasso96Mode saved the card's first DisplayID (0x50ff1000): the card skips it
printf 'junk\120\377\020\000junk' > "$DIR/devs/Picasso96Settings"
printf 'VRAM=4\nMODES=640x480,800x600\n' > "$DIR/env/Multicast.prefs"
out=$(run 2 800x600)
echo "$out"
if echo "$out" | grep -qi "50ff1000"; then
	echo "modetest: FAILED: 50ff1000 is in Picasso96Settings, the card used it anyway"
	exit 1
fi
rm -f "$DIR/devs/Picasso96Settings"
