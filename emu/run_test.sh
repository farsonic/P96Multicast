#!/bin/sh
# P96Multicast - end-to-end test on FS-UAE with AROS m68k and Riptide.
#
# Installs build/Multicast.card into the AROS system (Libs:Picasso96, plus a
# copy of AROS's P96 monitor driver as Devs:Monitors/Multicast), boots an
# A4000/040, starts Riptide on uaenet.device, and runs mctest on the card in
# each depth. Meanwhile client/p96view.py on the Linux side records what
# arrives, and tests/check_frame.py checks the test pattern.
#
#   emu/run_test.sh [DEPTHS] [TIMEOUT]      DEPTHS default "8 16 32"
#
# Networking: with FSUAE_PCAP set to an FS-UAE built with Riptide's patch
# (Riptide/tools/fs-uae) and running as root, the Amiga is bridged to a veth
# pair (host end p96br1, 192.168.96.1; Amiga 192.168.96.2) and the card
# sends real multicast to 239.255.96.1, which the receiver joins on the host
# end. Otherwise FS-UAE's slirp is used and the card sends unicast to the
# host (10.0.2.2); Ubuntu's fs-uae package is built without slirp, though.
#
# Environment: AROSFS (AROS system directory, default /tmp/claude-0/arosfs),
# RIPTIDE (the Riptide binary), FSUAE_PCAP, DEST, PORT (default 9696),
# EXTRA_PREFS (more lines for ENV:Multicast.prefs, e.g. VRAM=64).
set -u
cd "$(dirname "$0")/.."
DEPTHS=${1:-"8 16 32"}
TIMEOUT=${2:-300}
AROSFS=${AROSFS:-/tmp/claude-0/arosfs}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
PORT=${PORT:-9696}
OUT=${OUT:-build/emu}

if [ -n "${FSUAE_PCAP:-}" ] && [ -x "$FSUAE_PCAP" ] && [ "$(id -u)" = 0 ]; then
	NET=bridge
	FSUAE=$FSUAE_PCAP
	DEST=${DEST:-239.255.96.1}
	AMIGA_IP=192.168.96.2
	HOST_IP=192.168.96.1
	UNIT=2
else
	NET=slirp
	FSUAE=fs-uae
	DEST=${DEST:-10.0.2.2}
	AMIGA_IP=10.0.2.15
	HOST_IP=10.0.2.2
	UNIT=0
fi

for f in build/Multicast.card build/mctest "$RIPTIDE" "$AROSFS/Devs/Monitors/P96"; do
	[ -f "$f" ] || { echo "SKIP: $f missing"; exit 0; }
done
command -v "$FSUAE" >/dev/null 2>&1 || { echo "SKIP: $FSUAE not installed"; exit 0; }
echo "network: $NET, card sends to $DEST:$PORT"

if [ $NET = bridge ]; then
	ip link del p96br0 2>/dev/null
	ip link add p96br0 type veth peer name p96br1 || { echo "SKIP: cannot create a veth pair"; exit 0; }
	trap 'ip link del p96br0 2>/dev/null' EXIT
	ip link set p96br0 up
	ip link set p96br1 up
	ip addr add $HOST_IP/24 dev p96br1
	# no offloads: the capture must see real frames
	if command -v ethtool >/dev/null 2>&1; then
		for d in p96br0 p96br1; do ethtool -K $d tso off gso off gro off tx off rx off >/dev/null 2>&1; done
	fi
fi

rm -rf "$OUT"
mkdir -p "$OUT/work"
work=$(cd "$OUT/work" && pwd)

# ---- install the card into AROS
mkdir -p "$AROSFS/Libs/Picasso96"
cp build/Multicast.card "$AROSFS/Libs/Picasso96/Multicast.card"
cp "$AROSFS/Devs/Monitors/P96" "$AROSFS/Devs/Monitors/Multicast"
# AROS's multi-user startup loads AROS's own usergroup.library, and Riptide
# (rightly) refuses to start next to another stack's libraries
if [ -f "$AROSFS/Libs/usergroup.library" ]; then
	mv "$AROSFS/Libs/usergroup.library" "$AROSFS/Libs/usergroup.library.aros"
fi
mkdir -p "$AROSFS/Prefs/Env-Archive"
cat > "$AROSFS/Prefs/Env-Archive/Multicast.prefs" <<EOF
# written by emu/run_test.sh
DEST=$DEST
PORT=$PORT
FPS=25
REFRESH=2
LOG=RAM:Multicast.log
${EXTRA_PREFS:-}
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
cp build/mctest "$RIPTIDE" "$work/"
rtbin=$(dirname "$RIPTIDE")
for t in ip netstat ping; do
	[ -f "$rtbin/$t" ] && cp "$rtbin/$t" "$work/"
done
cat > "$work/riptide.conf" <<EOF
hostname p96test
loglevel 2
interface eth0 device=uaenet.device unit=$UNIT
ifconfig eth0 $AMIGA_IP/24 up
route add default $HOST_IP
EOF
{
	echo 'FailAt 21'
	echo 'Work:mctest STATUS >Work:status.txt'
	echo 'Run >Work:riptide.log Work:Riptide CONFIG=Work:riptide.conf'
	echo 'Wait 3'
	echo 'Work:mctest LIST >Work:modes.txt'
	# shown-N while the pattern is still up, so the receiver stops in time
	for d in $DEPTHS; do
		echo "Run >Work:mctest-$d.log Work:mctest WIDTH=640 HEIGHT=480 DEPTH=$d SECS=14"
		echo 'Wait 10'
		echo "Echo >Work:shown-$d done"
		echo 'Wait 6'
	done
	echo 'Work:mctest STATUS >>Work:status.txt'
	echo 'Copy RAM:Multicast.log Work:card.log'
	echo "Work:ping -c 2 $HOST_IP >Work:ping.txt"
	echo 'Work:ip -s link >Work:net.txt'
	echo 'Work:netstat -s >>Work:net.txt'
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
EOF
if [ $NET = bridge ]; then
	echo "uaenet_pcap = p96br0" >> "$OUT/test.fs-uae"
	echo "base_dir = $(cd "$OUT" && pwd)/fsuae" >> "$OUT/test.fs-uae"
else
	echo "network_card = a2065" >> "$OUT/test.fs-uae"
fi

# ---- run
xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :98 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:98
	export DISPLAY
	sleep 1
fi

# one receiver per depth, each started when the previous screen closed
status=0
SDL_AUDIODRIVER=dummy "$FSUAE" "$OUT/test.fs-uae" >"$OUT/fs-uae.log" 2>&1 &
upid=$!
start=$(date +%s)
for d in $DEPTHS; do
	python3 client/p96view.py --group "$DEST" --port "$PORT" --iface "$HOST_IP" \
		--snapshot "$OUT/frame-$d.png" --duration 600 2>"$OUT/p96view-$d.log" &
	vpid=$!
	while [ ! -f "$work/shown-$d" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for depth $d"
			status=1
			break
		fi
		sleep 1
	done
	kill -TERM $vpid 2>/dev/null
	wait $vpid 2>/dev/null
	[ $status -ne 0 ] && break
done
while [ $status -eq 0 ] && [ ! -f "$work/autorun.done" ]; do
	kill -0 $upid 2>/dev/null || break
	[ $(($(date +%s) - start)) -gt "$TIMEOUT" ] && break
	sleep 1
done
# what the card shows at the end (the AROS Workbench, if it opened there)
python3 client/p96view.py --group "$DEST" --port "$PORT" --iface "$HOST_IP" \
	--snapshot "$OUT/final.png" --duration 6 2>"$OUT/p96view-final.log"
import -window root "$OUT/screen.png" 2>/dev/null
kill $upid 2>/dev/null
wait $upid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

echo "---- modes (Multicast, first 8)"
tr -d '\r' < "$work/modes.txt" 2>/dev/null | grep -i multicast | head -8
echo "---- riptide.log"
tr -d '\r' < "$work/riptide.log" 2>/dev/null | tail -5
echo "---- status"
tr -d '\r' < "$work/status.txt" 2>/dev/null
echo "---- card.log"
tr -d '\r' < "$work/card.log" 2>/dev/null | tail -12
echo "---- ping"
tr -d '\r' < "$work/ping.txt" 2>/dev/null | tail -3
echo "---- network"
tr -d '\r' < "$work/net.txt" 2>/dev/null | sed -n '/eth0/,/driver stats/p'
for d in $DEPTHS; do
	echo "---- depth $d"
	tr -d '\r' < "$work/mctest-$d.log" 2>/dev/null
	cat "$OUT/p96view-$d.log"
	if python3 tests/check_frame.py "$OUT/frame-$d.png"; then
		echo "PASS: depth $d"
	else
		echo "FAIL: depth $d"
		status=1
	fi
done
exit $status
