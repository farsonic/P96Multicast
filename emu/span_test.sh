#!/bin/sh
# P96Multicast - Wrap.card's SPAN= over the network, in FS-UAE with AROS m68k
# and Riptide: one big screen on four monitors, and a window moving between
# them. Like emu/run_test.sh, it needs FSUAE_PCAP (an FS-UAE built with
# Riptide's patch) and root, for the veth bridge.
#
# Wrap.card wraps Multicast.card's first board (the big screen, port 9696)
# with SPAN=2x2 HEAD=Multicast.card: its boards 2 to 5 (BOARDS=5, ports 9697
# to 9700, which rtg.library never sees) are the four heads, left to right
# and top to bottom. mctest SPAN=2x2 opens a 1280x960 screen in a 640x480
# mode with the test pattern in each head's tile, then moves an orange
# window from head 1 to head 4. At each step, every head must show its test
# pattern, and only the head the window is on must show the window.
#
# Five boards sending and four heads copied cost the emulated 68040 a lot,
# so the card and the heads run at 10 passes a second here.
#
#   emu/span_test.sh [TIMEOUT]
#
# Environment: AROSFS, RIPTIDE, FSUAE_PCAP, OUT (default build/emu-span).
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-420}
AROSFS=${AROSFS:-/tmp/claude-0/arosfs}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-span}
GROUP=239.255.96.1
AMIGA_IP=192.168.96.2
HOST_IP=192.168.96.1
HEADS="1 2 3 4"

for f in build/Multicast.card build/Wrap.card build/mctest build/wrapstat "$RIPTIDE" \
         "$AROSFS/Devs/Monitors/P96"; do
	[ -f "$f" ] || { echo "SKIP: $f missing"; exit 0; }
done
if [ -z "${FSUAE_PCAP:-}" ] || [ ! -x "$FSUAE_PCAP" ] || [ "$(id -u)" != 0 ]; then
	echo "SKIP: needs FSUAE_PCAP (FS-UAE with Riptide's patch) and root"
	exit 0
fi

ip link del p96br0 2>/dev/null
ip link add p96br0 type veth peer name p96br1 || { echo "SKIP: cannot create a veth pair"; exit 0; }
trap 'ip link del p96br0 2>/dev/null' EXIT
ip link set p96br0 up
ip link set p96br1 up
ip addr add $HOST_IP/24 dev p96br1
if command -v ethtool >/dev/null 2>&1; then
	for d in p96br0 p96br1; do ethtool -K $d tso off gso off gro off tx off rx off >/dev/null 2>&1; done
fi

rm -rf "$OUT"
mkdir -p "$OUT/work"
work=$(cd "$OUT/work" && pwd)

# ---- install into AROS
mkdir -p "$AROSFS/Libs/Picasso96" "$AROSFS/Prefs/Env-Archive"
cp build/Multicast.card build/Wrap.card "$AROSFS/Libs/Picasso96/"
rm -f "$AROSFS/Devs/Monitors/Multicast"
cp "$AROSFS/Devs/Monitors/P96" "$AROSFS/Devs/Monitors/Wrap"
cat > "$AROSFS/Prefs/Env-Archive/Wrap.prefs" <<EOF
# written by emu/span_test.sh
CARD=Multicast.card
SPAN=2x2
HEAD=Multicast.card
MIRRORFPS=10
EOF
cat > "$AROSFS/Prefs/Env-Archive/Multicast.prefs" <<EOF
# written by emu/span_test.sh
BOARDS=5
DEST=$GROUP
PORT=9696
FPS=10
REFRESH=2
LOG=RAM:Multicast.log
EOF
if [ -f "$AROSFS/Libs/usergroup.library" ]; then
	mv "$AROSFS/Libs/usergroup.library" "$AROSFS/Libs/usergroup.library.aros"
fi
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
cp build/mctest build/wrapstat "$RIPTIDE" "$work/"
cat > "$work/riptide.conf" <<EOF
hostname p96test
loglevel 2
interface eth0 device=uaenet.device unit=2
ifconfig eth0 $AMIGA_IP/24 up
route add default $HOST_IP
EOF
cat > "$work/autorun" <<'EOF'
FailAt 21
Run >Work:riptide.log Work:Riptide CONFIG=Work:riptide.conf
Wait 3
Work:mctest LIST >Work:modes.txt
Run >Work:span.log Work:mctest NAME=Multicast SPAN=2x2 WIDTH=640 HEIGHT=480 DEPTH=16 MARK=Work:span STEP=16
Wait 24
Work:wrapstat >Work:wrapstat.txt
Wait 46
Work:mctest STATUS >Work:status.txt
Copy RAM:Multicast.log#? Work:
Echo >Work:autorun.done done
EOF

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
uae_sana2 = 1
sound_card = none
floppy_drive_count = 0
automatic_input_grab = 0
video_sync = off
uaenet_pcap = p96br0
base_dir = $(cd "$OUT" && pwd)/fsuae
EOF

# ---- run: at each step, one receiver per head
xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :95 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:95
	export DISPLAY
	sleep 1
fi
status=0
SDL_AUDIODRIVER=dummy "$FSUAE_PCAP" "$OUT/test.fs-uae" >"$OUT/fs-uae.log" 2>&1 &
upid=$!
start=$(date +%s)
for step in $HEADS; do
	while [ ! -f "$work/span-$step" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for the window to get to head $step"
			status=1
			break
		fi
		sleep 1
	done
	[ $status -ne 0 ] && break
	pids=
	for h in $HEADS; do
		python3 client/p96view.py --group $GROUP --port $((9696 + h)) --iface $HOST_IP \
			--snapshot "$OUT/step$step-head$h.png" --duration 600 \
			2>"$OUT/p96view-step$step-head$h.log" &
		pids="$pids $!"
	done
	sleep 11
	kill -TERM $pids 2>/dev/null
	wait $pids 2>/dev/null
done
while [ $status -eq 0 ] && [ ! -f "$work/autorun.done" ]; do
	kill -0 $upid 2>/dev/null || break
	[ $(($(date +%s) - start)) -gt "$TIMEOUT" ] && break
	sleep 1
done
kill $upid 2>/dev/null
wait $upid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

show() {
	echo "---- $1"
	tr -d '\r' < "$work/$1" 2>/dev/null
}
show span.log
show wrapstat.txt | sed -n '1,7p;/^heads:/,$p'
echo "---- modes offered (none of the heads')"
tr -d '\r' < "$work/modes.txt" 2>/dev/null | grep -c "Multicast[2-9]" | sed 's/^/head modes: /'

# ---- checks
for step in $HEADS; do
	for h in $HEADS; do
		f="$OUT/step$step-head$h.png"
		if python3 - "$f" "$h" "$step" <<'EOF'
import sys
sys.path.insert(0, "tests")
from check_frame import read_png, BARS, TOLERANCE
path, head, step = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
try:
    w, h, rgb = read_png(path)
except (OSError, ValueError) as e:
    print("%s: %s" % (path, e))
    sys.exit(1)
ok = (w, h) == (640, 480)
if ok:
    for i, want in enumerate(BARS):
        o = ((h // 4) * w + (2 * i + 1) * w // 16) * 3
        exp = ((want >> 16) & 255, (want >> 8) & 255, want & 255)
        if any(abs(a - b) > TOLERANCE for a, b in zip(rgb[o:o + 3], exp)):
            ok = False
    # the middle of the window (mctest: 200x120 at w/2-100, 3h/4-60)
    o = ((h * 3 // 4) * w + w // 2) * 3
    px = tuple(rgb[o:o + 3])
    orange = abs(px[0] - 0xff) <= TOLERANCE and abs(px[1] - 0x80) <= TOLERANCE and px[2] <= TOLERANCE
    if orange != (head == step):
        ok = False
print("step %d head %d: %dx%d, window pixel %02x%02x%02x -> %s" % (
    step, head, w, h, px[0] if ok or (w, h) == (640, 480) else 0,
    px[1] if (w, h) == (640, 480) else 0, px[2] if (w, h) == (640, 480) else 0,
    "ok" if ok else "WRONG"))
sys.exit(0 if ok else 1)
EOF
		then
			[ $h = $step ] && echo "PASS: window on head $step: head $h shows its tile and the window"
			[ $h != $step ] && echo "PASS: window on head $step: head $h shows its tile, no window"
		else
			echo "FAIL: window on head $step: head $h"
			status=1
		fi
	done
done
if tr -d '\r' < "$work/modes.txt" 2>/dev/null | grep -q "Multicast[2-9]"; then
	echo "FAIL: the heads' boards are offered to rtg.library"
	status=1
else
	echo "PASS: the heads' boards are not offered to rtg.library"
fi
exit $status
