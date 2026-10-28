#!/bin/sh
# Panorama - the shared desktop, end to end: Panorama.card with
# SPLIT=2x1 in FS-UAE with AROS m68k and Riptide, PanoramaDesk opening a
# 640x480 desktop of two 320x480 monitors, Boing bouncing over it, and a
# window moved from monitor to monitor with PanoramaDesk's moves. The
# viewer (client/panoview.py --span 2x1) keeps a picture of both monitors;
# at each step the test keeps one, and checks the ball in each half, and
# the window where it was sent. Needs FSUAE_PCAP and root, like
# emu/run_test.sh.
#
#   emu/desktop_test.sh [TIMEOUT]
#
# Environment: AROSFS, RIPTIDE, FSUAE_PCAP, OUT (default build/emu-desktop).
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-600}
AROSFS=${AROSFS:-/tmp/claude-0/arosfs}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-desktop}
GROUP=239.255.96.1
AMIGA_IP=192.168.96.2
HOST_IP=192.168.96.1

for f in build/Panorama.card build/mctest build/PanoramaDesk build/Boing "$RIPTIDE" "$AROSFS/Devs/Monitors/P96"; do
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

# ---- install into AROS: the card on its own
mkdir -p "$AROSFS/Libs/Picasso96" "$AROSFS/Prefs/Env-Archive"
cp build/Panorama.card "$AROSFS/Libs/Picasso96/"
rm -f "$AROSFS/Devs/Monitors/Wrap"
cp "$AROSFS/Devs/Monitors/P96" "$AROSFS/Devs/Monitors/Panorama"
cat > "$AROSFS/Prefs/Env-Archive/Panorama.prefs" <<EOF
# written by emu/desktop_test.sh
DEST=$GROUP
PORT=9696
LOG=RAM:Panorama.log
SPLIT=2x1
MONITOR=320x480
COLOURS=16
PUBSCREEN=PANORAMA
DEFAULTSCREEN=1
INPUT=0
WORKBENCH=LEFT1
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
cp build/mctest build/PanoramaDesk build/Boing "$RIPTIDE" "$work/"
cat > "$work/riptide.conf" <<EOF
hostname p96test
loglevel 2
interface eth0 device=uaenet.device unit=2
ifconfig eth0 $AMIGA_IP/24 up
route add default $HOST_IP
EOF
{
	echo 'FailAt 21'
	echo 'Run >Work:riptide.log Work:Riptide CONFIG=Work:riptide.conf'
	echo 'Wait 5'
	echo 'Run >Work:desktop.log Work:PanoramaDesk'
	echo 'Wait 6'
	echo 'Echo >Work:mark-open x'
	echo 'Wait 4'
	echo 'Run >Work:boing.log Work:Boing SIZE=60 SPEED=6 SECS=40'
	for i in 1 2 3 4 5 6 7 8 9 10; do
		echo 'Wait 3'
		echo "Echo >Work:mark-boing$i x"
	done
	echo 'Wait 12'
	echo 'Run >CON:20/40/200/120/MoveMe/SCREENPANORAMA Wait 60'
	echo 'Wait 5'
	echo "Echo >Work:mark-left x"
	echo 'Wait 4'
	echo 'Work:PanoramaDesk MOVE=RIGHT WINDOW=MoveMe >Work:move.log'
	echo 'Wait 5'
	echo "Echo >Work:mark-right x"
	echo 'Wait 4'
	echo 'Work:PanoramaDesk CENTRE=1 WINDOW=MoveMe >>Work:move.log'
	echo 'Wait 5'
	echo "Echo >Work:mark-centre x"
	echo 'Wait 4'
	echo 'Work:PanoramaDesk INFO >Work:info.log'
	echo 'Run >NIL: Work:PanoramaDesk >>Work:info.log'
	echo 'Wait 50'
	echo 'Work:PanoramaDesk SHELL >>Work:info.log'
	echo 'Wait 5'
	echo "Echo >Work:mark-shell x"
	echo 'Wait 4'
	# INPUT=1, as PanoramaPrefs' Open sets it, while the card runs: then the
	# test types into the Shell from the viewer's side
	echo 'Echo >>ENV:Panorama.prefs "INPUT=1"'
	echo 'Wait 4'
	echo "Echo >Work:mark-input x"
	for i in $(seq 1 40); do
		echo 'If NOT EXISTS Work:typed'
		echo 'Wait 1'
		echo 'EndIf'
	done
	echo 'Wait 2'
	# WORKBENCH=LEFT1: the mouse over the desktop's left edge to the
	# Workbench, and over the Workbench's right edge back
	echo 'Work:PanoramaDesk'
	echo 'Wait 2'
	echo 'Work:mctest WHERE >Work:where0.log'
	# to the edge in one move, then small pushes over it
	echo 'Work:mctest MOUSE=-1000,0,1'
	echo 'Wait 1'
	echo 'Work:mctest MOUSE=-1,0,2'
	echo 'Wait 1'
	echo 'Work:mctest WHERE >Work:where1.log'
	# the Workbench in front: the desktop's picture stays in the viewer
	echo 'Wait 2'
	echo 'Echo >Work:mark-behind x'
	echo 'Wait 4'
	echo 'Work:mctest MOUSE=1000,0,1'
	echo 'Wait 1'
	echo 'Work:mctest MOUSE=1,0,2'
	echo 'Wait 1'
	echo 'Work:mctest WHERE >Work:where2.log'
	echo 'Work:mctest STATUS >Work:status.txt'
	echo 'Copy RAM:Panorama.log Work:card.log'
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

xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :94 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:94
	export DISPLAY
	sleep 1
fi
status=0
SDL_AUDIODRIVER=dummy "$FSUAE_PCAP" "$OUT/test.fs-uae" >"$OUT/fs-uae.log" 2>&1 &
upid=$!
# the viewer: both monitors
python3 client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP --span 2x1 \
	--snapshot "$OUT/last.png" --duration "$TIMEOUT" 2>"$OUT/panoview.log" &
vpid=$!
start=$(date +%s)
while [ ! -f "$work/autorun.done" ]; do
	if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
		echo "FAIL: timeout"
		status=1
		break
	fi
	# each mark: keep what the viewer shows a moment later
	for m in "$work"/mark-*; do
		[ -f "$m" ] || continue
		n=${m##*/mark-}
		if [ ! -f "$OUT/at-$n.png" ]; then
			sleep 1
			cp "$OUT/last.png" "$OUT/at-$n.png" 2>/dev/null
			if [ "$n" = input ]; then
				# on monitor 2: click into the Shell, type, then point somewhere
				python3 tests/send_input.py --group 239.255.96.2 --port 9697 \
					--iface $HOST_IP move:50,240 click:1 \
					'type:echo >Work:typed "Hello from the viewer"\n' move:100,100 \
					>"$OUT/input.log" 2>&1 &
			fi
		fi
	done
	sleep 0.5
done
kill $upid $vpid 2>/dev/null
wait $upid $vpid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

echo "---- PanoramaDesk"
tr -d '\r' < "$work/desktop.log" 2>/dev/null
tr -d '\r' < "$work/move.log" 2>/dev/null
tr -d '\r' < "$work/info.log" 2>/dev/null
echo "---- Boing"
tr -d '\r' < "$work/boing.log" 2>/dev/null
echo "---- input"
cat "$OUT/input.log" 2>/dev/null
typed=$(tr -d '\r' < "$work/typed" 2>/dev/null)
if [ "$typed" = "Hello from the viewer" ]; then
	echo "PASS: INPUT: typed into the Shell from the viewer"
else
	echo "FAIL: INPUT: typed into the Shell from the viewer (Work:typed: '$typed')"
	status=1
fi
ptr=$(grep '^pointer' "$OUT/input.log" 2>/dev/null)
set -- $ptr
if [ $# -ge 3 ] && [ $(( ($2 - 100) * ($2 - 100) + ($3 - 100) * ($3 - 100) )) -le 512 ]; then
	echo "PASS: INPUT: the pointer where the viewer put it ($2,$3 on monitor 2)"
else
	echo "FAIL: INPUT: the pointer where the viewer put it ('$ptr')"
	status=1
fi
echo "---- crossing over (WORKBENCH=LEFT1)"
for n in 0 1 2; do tr -d '\r' < "$work/where$n.log" 2>/dev/null; done
w0=$(tr -d '\r' < "$work/where0.log" 2>/dev/null)
w1=$(tr -d '\r' < "$work/where1.log" 2>/dev/null)
w2=$(tr -d '\r' < "$work/where2.log" 2>/dev/null)
# "front NAME WxH mouse X Y active TITLE"
front() { echo "$1" | sed -n 's/^front \(.*\) [0-9]*x[0-9]* mouse .*/\1/p'; }
width() { echo "$1" | sed -n 's/.* \([0-9]*\)x[0-9]* mouse .*/\1/p'; }
mousex() { echo "$1" | sed -n 's/.* mouse \([0-9-]*\) .*/\1/p'; }
x1=$(mousex "$w1")
if [ "$(front "$w1")" = Workbench ] && [ -n "$x1" ] && [ "$x1" -ge $(( $(width "$w1") - 12 )) ]; then
	echo "PASS: the mouse over the desktop's left edge: the Workbench, at its right edge"
else
	echo "FAIL: the mouse over the desktop's left edge: the Workbench, at its right edge ('$w1')"
	status=1
fi
x2=$(mousex "$w2")
if [ "$(front "$w2")" = "$(front "$w0")" ] && [ -n "$x2" ] && [ "$x2" -le 12 ] &&
   [ "${w2#* active }" = "${w0#* active }" ]; then
	echo "PASS: ... and back: the desktop, at its left edge, the same window active"
else
	echo "FAIL: ... and back: the desktop, at its left edge, the same window active ('$w0' / '$w2')"
	status=1
fi
python3 tests/check_desktop.py "$OUT" || status=1
echo "---- status"
tr -d '\r' < "$work/status.txt" 2>/dev/null
echo "---- viewer"
cat "$OUT/panoview.log"
exit $status
