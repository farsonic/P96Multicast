#!/bin/sh
# Panorama - build the LhA package: build/Panorama.lha
#
#   dist/mkpackage.sh            (make package)
#
# Needs a built card and mctest (make), and an lha that can create
# archives (else dist/mklha.py writes one, stored), and Riptide's
# tools/mkicon.py for the monitor icon (RIPTIDE_SRC,
# default /home/user/Riptide).
set -eu
cd "$(dirname "$0")/.."
RIPTIDE_SRC=${RIPTIDE_SRC:-/home/user/Riptide}
STAGE=build/package
PKG=$STAGE/Panorama

for f in build/Panorama.card build/Wrap.card build/mctest build/wrapstat build/PanoramaPrefs \
         build/PanoramaDesk build/Boing; do
	[ -f "$f" ] || { echo "$f missing: run make first"; exit 1; }
done
rm -rf "$STAGE" build/Panorama.lha
mkdir -p "$PKG/Libs/Picasso96" "$PKG/Devs/Monitors" "$PKG/Prefs" "$PKG/C" \
	"$PKG/Client" "$PKG/Docs" "$PKG/Source" "$PKG/Scripts"

cp dist/ReadMe "$PKG/ReadMe"
cp build/Panorama.card build/Wrap.card "$PKG/Libs/Picasso96/"
cp build/mctest build/wrapstat build/PanoramaDesk build/Boing "$PKG/C/"
cp build/PanoramaPrefs "$PKG/"
cp client/panoview.py tests/fake_sender.py "$PKG/Client/"
# scripts: the shared desktop from Workbench (IconX) or a Shell; the viewer on a Mac
cp dist/scripts/DesktopOpen dist/scripts/DesktopShell dist/scripts/DesktopBoing \
	dist/scripts/DesktopClose "$PKG/Scripts/"
cp dist/scripts/desktop.command dist/scripts/desktop-lan.command "$PKG/Client/"
chmod +x "$PKG/Client/"*.command
cp README.md "$PKG/Docs/README.md"
cp docs/COPPER.md docs/BUILD.md docs/WRAP.md "$PKG/Docs/"
git ls-files | grep -v "^docs/mui-guide/" | while read -r f; do
	mkdir -p "$PKG/Source/$(dirname "$f")"
	cp "$f" "$PKG/Source/$f"
done

cat > "$PKG/Prefs/Panorama.prefs" <<'EOF'
# Panorama settings: copy to ENVARC:Panorama.prefs (and ENV:)
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
#LOG=T:Panorama.log
#BOARDS=1
EOF

cat > "$PKG/Prefs/Wrap.prefs" <<'EOF'
# Wrap.card settings: copy to ENVARC:Wrap.prefs (and ENV:)
# CARD= the card driver to wrap, in LIBS:Picasso96/ (or give a path)
CARD=uaegfx.card
# DRAW=0 leaves the drawing functions unwrapped
#DRAW=1
# MIRROR= a card driver to show the same screen on, e.g. Panorama.card
#MIRROR=Panorama.card
# SPAN= one big screen on CxR heads, each HEAD= (or HEAD1= ... HEAD4=)
#SPAN=2x2
#HEAD=Panorama.card
# HEADSHARE=1: heads that can show any memory (Panorama.card) need no copy
#HEADSHARE=0
#MIRRORFPS=25
EOF

# the monitor icon: a TOOL icon whose ToolTypes name the board, like P96's own
if [ -f "$RIPTIDE_SRC/tools/mkicon.py" ]; then
	python3 "$RIPTIDE_SRC/tools/mkicon.py" tool "$PKG/PanoramaPrefs.info" >/dev/null
	for s in DesktopOpen DesktopShell DesktopBoing DesktopClose; do
		python3 "$RIPTIDE_SRC/tools/mkicon.py" project "$PKG/Scripts/$s.info" --tool C:IconX \
			--tooltype "WINDOW=NIL:" >/dev/null
	done
	python3 "$RIPTIDE_SRC/tools/mkicon.py" project "$PKG/Devs/Monitors/Panorama.info" \
		--tooltype "BOARDTYPE=Panorama" --tooltype "(DEST=239.255.96.1)" \
		--tooltype "(PORT=9696)" --tooltype "(FPS=25)" --tooltype "(LOG=T:Panorama.log)" >/dev/null
	# do_Type (offset 48 of the DiskObject): WBPROJECT -> WBTOOL
	python3 - "$PKG/Devs/Monitors/Panorama.info" <<'EOF'
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

# nothing that mustn't ship: the sandbox's test-only TLS certificate, keys
# (the patterns made from pieces, so this script, shipped in Source/, doesn't match)
CERT="BEGIN CERT""IFICATE\|BEGIN .*PRIVATE"" KEY\|sandbox-test""-only"
if grep -rl "$CERT" "$PKG"; then
	echo "mkpackage: a certificate or key in the package (above): not packaged" >&2
	exit 1
fi
if find "$PKG" \( -name '*.crt' -o -name '*.pem' -o -name '*extra-ca*' \) | grep .; then
	echo "mkpackage: certificate files in the package (above): not packaged" >&2
	exit 1
fi

# Amiga line ends are LF already; make the scripts and docs plain files
# an lha that can create archives, or our own writer (stored, level 2)
if (cd "$STAGE" && lha a ../Panorama.lha Panorama >/dev/null 2>&1) && [ -s build/Panorama.lha ]; then
	:
else
	rm -f build/Panorama.lha
	python3 dist/mklha.py build/Panorama.lha "$PKG" Panorama
fi
# and again on the finished archive
if strings build/Panorama.lha | grep -q "$CERT"; then
	echo "mkpackage: a certificate in build/Panorama.lha: removed" >&2
	rm -f build/Panorama.lha
	exit 1
fi
echo "built build/Panorama.lha"
