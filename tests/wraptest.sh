#!/bin/sh
# P96Multicast - run tests/wraptest.c under vamos: the built Wrap.card wraps
# Fake.card, once with DRAW=1 and once with DRAW=0.
#
# vamos only loads libraries whose file names end in .library, so the cards
# are copied as wrap.library and fake.library, and CARD= gives the latter
# with its path (the library inside is still called Fake.card).
#
#   tests/wraptest.sh [VAMOS]          (make check)
set -eu
cd "$(dirname "$0")/.."
VAMOS=${1:-vamos -C 68020}
DIR=$(pwd)/build/wraptest.d

rm -rf "$DIR"
mkdir -p "$DIR/libs" "$DIR/env"
cp build/Wrap.card "$DIR/libs/wrap.library"
cp build/Fake.card "$DIR/libs/fake.library"
for draw in 1 0; do
	printf 'CARD=LIBS:fake.library\nDRAW=%s\n' $draw > "$DIR/env/Wrap.prefs"
	$VAMOS -V "p96:$(pwd)" -a LIBS:p96:build/wraptest.d/libs -a ENV:p96:build/wraptest.d/env \
		build/wraptest $draw LIBS:wrap.library
done
