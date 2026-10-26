#!/bin/sh
# P96Multicast - Wrap.card in FS-UAE with AROS m68k: Wrap.card wraps
# Multicast.card, and mctest opens and draws a screen on it in each depth.
#
# Installs build/Wrap.card and build/Multicast.card in LIBS:Picasso96, a copy
# of AROS's P96 monitor driver as Devs:Monitors/Wrap (it opens the card named
# after itself) and no Devs:Monitors/Multicast, and CARD=Multicast.card in
# ENVARC:Wrap.prefs. No network: the checks are what Wrap.card, the card and
# mctest report, written to the Work: disk.
#
#   emu/wrap_test.sh [DEPTHS] [TIMEOUT]      DEPTHS default "8 16 32"
#
# Environment: AROSFS (AROS system directory, default /tmp/claude-0/arosfs),
# OUT (default build/emu-wrap), WRAP_PREFS (more lines for Wrap.prefs).
set -u
cd "$(dirname "$0")/.."
DEPTHS=${1:-"8 16 32"}
TIMEOUT=${2:-240}
AROSFS=${AROSFS:-/tmp/claude-0/arosfs}
OUT=${OUT:-build/emu-wrap}

for f in build/Wrap.card build/Multicast.card build/mctest build/wrapstat \
         "$AROSFS/Devs/Monitors/P96"; do
	[ -f "$f" ] || { echo "SKIP: $f missing"; exit 0; }
done
command -v fs-uae >/dev/null 2>&1 || { echo "SKIP: fs-uae not installed"; exit 0; }

rm -rf "$OUT"
mkdir -p "$OUT/work"
work=$(cd "$OUT/work" && pwd)

# ---- install into AROS
mkdir -p "$AROSFS/Libs/Picasso96" "$AROSFS/Prefs/Env-Archive"
cp build/Wrap.card build/Multicast.card "$AROSFS/Libs/Picasso96/"
cp "$AROSFS/Devs/Monitors/P96" "$AROSFS/Devs/Monitors/Wrap"
rm -f "$AROSFS/Devs/Monitors/Multicast"
cat > "$AROSFS/Prefs/Env-Archive/Wrap.prefs" <<EOF
# written by emu/wrap_test.sh
CARD=Multicast.card
${WRAP_PREFS:-}
EOF
cat > "$AROSFS/Prefs/Env-Archive/Multicast.prefs" <<EOF
# written by emu/wrap_test.sh
LOG=RAM:Multicast.log
EOF
if ! grep -q waitwork "$AROSFS/S/User-Startup" 2>/dev/null; then
	cat >> "$AROSFS/S/User-Startup" <<'EOF'
Lab waitwork
If NOT EXISTS Work:
    Wait 1
    Skip waitwork BACK
EndIf
If EXISTS Work:autorun
    Run >NIL: Execute Work:autorun
EndIf
EOF
fi

# ---- the Work: disk
cp build/mctest build/wrapstat "$work/"
{
	echo 'FailAt 21'
	echo 'Work:wrapstat >Work:wrapstat-boot.txt'
	echo 'Work:mctest STATUS >Work:status.txt'
	echo 'Work:mctest LIST >Work:modes.txt'
	for d in $DEPTHS; do
		echo "Work:mctest WIDTH=640 HEIGHT=480 DEPTH=$d SECS=3 >Work:mctest-$d.log"
	done
	echo 'Work:wrapstat >Work:wrapstat.txt'
	echo 'Work:mctest STATUS >>Work:status.txt'
	echo 'Echo >Work:autorun.done done'
} > "$work/autorun"

cat > "$OUT/test.fs-uae" <<EOF
[fs-uae]
amiga_model = A4000/040
kickstart_file = $AROSFS/boot/amiga/aros-rom.bin
kickstart_ext_file = $AROSFS/boot/amiga/aros-ext.bin
zorro_iii_memory = 65536
hard_drive_0 = $AROSFS
hard_drive_0_label = AROS
hard_drive_1 = $work
hard_drive_1_label = Work
sound_card = none
floppy_drive_count = 0
automatic_input_grab = 0
video_sync = off
base_dir = $(cd "$OUT" && pwd)/fsuae
EOF

# ---- run
xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :97 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:97
	export DISPLAY
	sleep 1
fi
SDL_AUDIODRIVER=dummy fs-uae "$OUT/test.fs-uae" >"$OUT/fs-uae.log" 2>&1 &
upid=$!
start=$(date +%s)
status=0
while [ ! -f "$work/autorun.done" ]; do
	if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
		echo "FAIL: timeout waiting for the test to finish"
		status=1
		break
	fi
	sleep 1
done
import -window root "$OUT/screen.png" 2>/dev/null
kill $upid 2>/dev/null
wait $upid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

show() {
	echo "---- $1"
	tr -d '\r' < "$work/$1" 2>/dev/null
}
show wrapstat-boot.txt
show status.txt
echo "---- modes (Multicast, first 4)"
tr -d '\r' < "$work/modes.txt" 2>/dev/null | grep -i multicast | head -4
for d in $DEPTHS; do
	show "mctest-$d.log"
done
show wrapstat.txt

# ---- checks
ws="$work/wrapstat.txt"
need() {
	if tr -d '\r' < "$2" 2>/dev/null | grep -q "$1"; then
		echo "PASS: $3"
	else
		echo "FAIL: $3"
		status=1
	fi
}
count() {
	tr -d '\r' < "$ws" 2>/dev/null | awk -v f="$1" '$1 == f { print $2 }'
}
need "wrapping Multicast.card" "$ws" "Wrap.card wraps Multicast.card"
need "board 0: set up" "$ws" "the board is set up through Wrap.card"
need "last error: none" "$ws" "no error"
for d in $DEPTHS; do
	need "mctest: done" "$work/mctest-$d.log" "a depth $d screen opens and draws on the card"
done
n=$(echo $DEPTHS | wc -w)
for f in SetGC SetPanning; do
	c=$(count $f)
	if [ -n "$c" ] && [ "$c" -ge "$n" ]; then
		echo "PASS: $f went through Wrap.card ($c calls)"
	else
		echo "FAIL: $f went through Wrap.card (${c:-no} calls, want $n or more)"
		status=1
	fi
done
exit $status
