#!/bin/sh
# P96Multicast - Copper end to end: Multicast.card in FS-UAE with AROS m68k
# and Riptide, mctest WORKLOAD (a console listing directories, windows moved
# and resized, drawing, scrolling, images, closing) in each depth, and
# client/p96view.py receiving. At the end of each workload mctest dumps the
# screen as the card has it; the viewer's picture must be the same, pixel
# for pixel. Also shows what each step sent: orders, pixels, KB. Needs
# FSUAE_PCAP and root, like emu/run_test.sh.
#
#   emu/copper_test.sh [TIMEOUT]
#
# Environment: AROSFS, RIPTIDE, FSUAE_PCAP, OUT (default build/emu-copper),
# DEPTHS (default "8 16 32"), EXTRA_PREFS (more lines for Multicast.prefs).
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-1500}
AROSFS=${AROSFS:-/tmp/claude-0/arosfs}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-copper}
DEPTHS=${DEPTHS:-"8 16 32"}
GROUP=239.255.96.1
AMIGA_IP=192.168.96.2
HOST_IP=192.168.96.1

for f in build/Multicast.card build/mctest "$RIPTIDE" "$AROSFS/Devs/Monitors/P96"; do
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
cp build/Multicast.card "$AROSFS/Libs/Picasso96/"
rm -f "$AROSFS/Devs/Monitors/Wrap"
cp "$AROSFS/Devs/Monitors/P96" "$AROSFS/Devs/Monitors/Multicast"
cat > "$AROSFS/Prefs/Env-Archive/Multicast.prefs" <<EOF
# written by emu/copper_test.sh
DEST=$GROUP
PORT=9696
LOG=RAM:Multicast.log
${EXTRA_PREFS:-}
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
cp build/mctest "$RIPTIDE" "$work/"
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
	for d in $DEPTHS; do
		echo "Work:mctest WORKLOAD WIDTH=640 HEIGHT=480 DEPTH=$d DUMP=Work:screen-$d.raw >Work:workload-$d.txt"
	done
	echo 'Work:mctest STATUS >Work:status.txt'
	echo 'Copy RAM:Multicast.log Work:card.log'
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
# the viewer, keeping its picture in the card's format too
python3 client/p96view.py --group $GROUP --port 9696 --iface $HOST_IP \
	--snapshot "$OUT/last.png" --raw "$OUT/view.raw" --duration "$TIMEOUT" 2>"$OUT/p96view.log" &
vpid=$!
start=$(date +%s)
while [ ! -f "$work/autorun.done" ]; do
	if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
		echo "FAIL: timeout"
		status=1
		break
	fi
	# a dump: give the viewer a moment, then keep what it shows
	for d in $DEPTHS; do
		if [ -f "$work/screen-$d.raw" ] && [ ! -f "$OUT/view-$d.raw" ]; then
			sleep 4
			cp "$OUT/view.raw" "$OUT/view-$d.raw" 2>/dev/null || touch "$OUT/view-$d.raw"
			cp "$OUT/last.png" "$OUT/view-$d.png" 2>/dev/null
		fi
	done
	sleep 1
done
kill $upid $vpid 2>/dev/null
wait $upid $vpid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for d in $DEPTHS; do
	echo "---- depth $d"
	tr -d '\r' < "$work/workload-$d.txt" 2>/dev/null
	tr -d '\r' < "$work/workload-$d.txt" 2>/dev/null | grep -q "mctest: done" || status=1
	if python3 tests/compare_raw.py "$work/screen-$d.raw" "$OUT/view-$d.raw"; then
		echo "PASS: depth $d: the viewer shows the card's screen exactly"
	else
		echo "FAIL: depth $d: the viewer's picture differs"
		status=1
	fi
done
echo "---- status"
tr -d '\r' < "$work/status.txt" 2>/dev/null
echo "---- viewer"
cat "$OUT/p96view.log"
exit $status
