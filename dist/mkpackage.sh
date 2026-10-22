#!/bin/sh
# P96Multicast - build the LhA package: build/P96Multicast.lha
#
#   dist/mkpackage.sh            (make package)
#
# Needs a built card and mctest (make), jlha or another lha that can create
# archives, and Riptide's tools/mkicon.py for the monitor icon (RIPTIDE_SRC,
# default /home/user/Riptide).
set -eu
cd "$(dirname "$0")/.."
RIPTIDE_SRC=${RIPTIDE_SRC:-/home/user/Riptide}
STAGE=build/package
PKG=$STAGE/P96Multicast

for f in build/Multicast.card build/mctest; do
	[ -f "$f" ] || { echo "$f missing: run make first"; exit 1; }
done
rm -rf "$STAGE" build/P96Multicast.lha
mkdir -p "$PKG/Libs/Picasso96" "$PKG/Devs/Monitors" "$PKG/Prefs" "$PKG/C" \
	"$PKG/Client" "$PKG/Docs" "$PKG/Source"

cp dist/ReadMe "$PKG/ReadMe"
cp build/Multicast.card "$PKG/Libs/Picasso96/"
cp build/mctest "$PKG/C/"
cp client/p96view.py tests/fake_sender.py "$PKG/Client/"
cp README.md "$PKG/Docs/README.md"
cp docs/PROTOCOL.md docs/BUILD.md "$PKG/Docs/"
git ls-files | while read -r f; do
	mkdir -p "$PKG/Source/$(dirname "$f")"
	cp "$f" "$PKG/Source/$f"
done

cat > "$PKG/Prefs/Multicast.prefs" <<'EOF'
# P96Multicast settings: copy to ENVARC:Multicast.prefs (and ENV:)
# KEY=VALUE, one per line; '#' starts a comment. Defaults shown.
#DEST=239.255.96.1
#PORT=9696
#TTL=1
#FPS=25
#RATE=1500
#REFRESH=5
#VRAM=8
#MAXWIDTH=1920
#MAXHEIGHT=1200
#LOG=T:Multicast.log
EOF

# the monitor icon: a TOOL icon whose ToolTypes name the board, like P96's own
if [ -f "$RIPTIDE_SRC/tools/mkicon.py" ]; then
	python3 "$RIPTIDE_SRC/tools/mkicon.py" project "$PKG/Devs/Monitors/Multicast.info" \
		--tooltype "BOARDTYPE=Multicast" --tooltype "(DEST=239.255.96.1)" \
		--tooltype "(PORT=9696)" --tooltype "(FPS=25)" --tooltype "(LOG=T:Multicast.log)" >/dev/null
	# do_Type (offset 48 of the DiskObject): WBPROJECT -> WBTOOL
	python3 - "$PKG/Devs/Monitors/Multicast.info" <<'EOF'
import sys
p = sys.argv[1]
b = bytearray(open(p, "rb").read())
assert b[0:2] == b"\xe3\x10" and b[48] == 4
b[48] = 3
open(p, "wb").write(b)
EOF
else
	echo "warning: $RIPTIDE_SRC/tools/mkicon.py not found, no monitor icon" >&2
fi

# Amiga line ends are LF already; make the scripts and docs plain files
(cd "$STAGE" && lha a ../P96Multicast.lha P96Multicast >/dev/null)
echo "built build/P96Multicast.lha"
