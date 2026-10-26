#!/bin/sh
# P96Multicast - Wrap.card's mirror, end to end over the network, in FS-UAE
# with AROS m68k and Riptide. Like emu/run_test.sh, it needs FSUAE_PCAP (an
# FS-UAE built with Riptide's patch) and root, for the veth bridge.
#
# Wrap.card wraps Multicast.card (its first board, port 9696) with
# MIRROR=Multicast.card (its second board, BOARDS=2, port 9697, which
# rtg.library never sees). mctest draws on the wrapped board in 8, 16 and
# 32 bit; both streams must show the test pattern, and the same picture.
#
#   emu/mirror_test.sh [TIMEOUT]
#
# Environment: AROSFS, RIPTIDE, FSUAE_PCAP, OUT (default build/emu-mirror).
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
AROSFS=${AROSFS:-/tmp/claude-0/arosfs}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-mirror}
GROUP=239.255.96.1
AMIGA_IP=192.168.96.2
HOST_IP=192.168.96.1

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
rm -f "$AROSFS/Devs/Monitors/Multicast" "$AROSFS/Devs/Monitors/Wrap"
cp "$AROSFS/Devs/Monitors/P96" "$AROSFS/Devs/Monitors/Wrap"
printf 'CARD=Multicast.card\nMIRROR=Multicast.card\nMIRRORFPS=25\n' \
	> "$AROSFS/Prefs/Env-Archive/Wrap.prefs"
cat > "$AROSFS/Prefs/Env-Archive/Multicast.prefs" <<EOF
# written by emu/mirror_test.sh
BOARDS=2
DEST=$GROUP
PORT=9696
FPS=25
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
STEPS="8 16 32"
{
	echo 'FailAt 21'
	echo 'Run >Work:riptide.log Work:Riptide CONFIG=Work:riptide.conf'
	echo 'Wait 3'
	echo 'Work:mctest LIST >Work:modes.txt'
	echo 'Work:mctest STATUS >Work:status.txt'
	for d in $STEPS; do
		echo "Run >Work:mctest-$d.log Work:mctest NAME=Multicast WIDTH=640 HEIGHT=480 DEPTH=$d SECS=16"
		echo 'Wait 12'
		echo "Work:wrapstat >Work:wrapstat-$d.txt"
		echo "Echo >Work:shown-$d done"
		echo 'Wait 8'
	done
	echo 'Work:wrapstat >Work:wrapstat.txt' 
	echo 'Work:mctest STATUS >>Work:status.txt'
	echo 'Copy RAM:Multicast.log Work:card.log'
	echo 'Copy RAM:Multicast.log.2 Work:card2.log'
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
uae_sana2 = 1
sound_card = none
floppy_drive_count = 0
automatic_input_grab = 0
video_sync = off
uaenet_pcap = p96br0
base_dir = $(cd "$OUT" && pwd)/fsuae
EOF

# ---- run: two receivers per step, one per board
xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :96 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:96
	export DISPLAY
	sleep 1
fi
status=0
SDL_AUDIODRIVER=dummy "$FSUAE_PCAP" "$OUT/test.fs-uae" >"$OUT/fs-uae.log" 2>&1 &
upid=$!
start=$(date +%s)
for st in $STEPS; do
	python3 client/p96view.py --group $GROUP --port 9696 --iface $HOST_IP \
		--snapshot "$OUT/frame-$st-a.png" --duration 600 2>"$OUT/p96view-$st-a.log" &
	va=$!
	python3 client/p96view.py --group $GROUP --port 9697 --iface $HOST_IP \
		--snapshot "$OUT/frame-$st-b.png" --duration 600 2>"$OUT/p96view-$st-b.log" &
	vb=$!
	while [ ! -f "$work/shown-$st" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for step $st"
			status=1
			break
		fi
		sleep 1
	done
	kill -TERM $va $vb 2>/dev/null
	wait $va $vb 2>/dev/null
	[ $status -ne 0 ] && break
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
echo "---- modes (Multicast, first 3; none of the mirror's)"
tr -d '\r' < "$work/modes.txt" 2>/dev/null | grep "Multicast" | head -3
show status.txt
show card.log
show card2.log

# ---- checks
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; status=1; }
frame() {   # PNG WIDTH HEIGHT WHAT
	if python3 tests/check_frame.py "$1" "$2" "$3"; then pass "$4"; else fail "$4"; fi
}
for d in $STEPS; do
	show "mctest-$d.log"
	tr -d '\r' < "$work/wrapstat-$d.txt" 2>/dev/null | sed -n '/^heads:/,$p'
	frame "$OUT/frame-$d-a.png" 640 480 "depth $d: the wrapped board (port 9696)"
	frame "$OUT/frame-$d-b.png" 640 480 "depth $d: the mirror (port 9697)"
	# the same picture: all but the animated square and bar
	if python3 - "$OUT/frame-$d-a.png" "$OUT/frame-$d-b.png" <<'EOF'
import sys
sys.path.insert(0, "tests")
from check_frame import read_png
a, b = read_png(sys.argv[1]), read_png(sys.argv[2])
if a[:2] != b[:2]:
    print("sizes differ: %s %s" % (a[:2], b[:2]))
    sys.exit(1)
n = a[0] * a[1]
same = sum(1 for i in range(0, n * 3, 3) if a[2][i:i + 3] == b[2][i:i + 3])
print("%.1f%% of the pixels are the same" % (100.0 * same / n))
sys.exit(0 if same >= 0.9 * n else 1)
EOF
	then pass "depth $d: the mirror shows the same picture"
	else fail "depth $d: the mirror shows the same picture"
	fi
done
if tr -d '\r' < "$work/modes.txt" 2>/dev/null | grep -q "Multicast2"; then
	fail "the mirror's board is not offered to rtg.library"
else
	pass "the mirror's board is not offered to rtg.library"
fi
exit $status
