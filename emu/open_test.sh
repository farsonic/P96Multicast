#!/bin/sh
# Panorama - opening the desktop on a system set up as users have it:
# AmigaOS (KICK=, OS3SYS=) with Picasso96 (P96=) in FS-UAE, a 68030 with
# 64 MB, the Workbench on uaegfx through Wrap.card (an RTG screen: then
# Picasso96's WaitTOF() takes the shown board's lock), PanoramaPrefs
# INSTALL SETUP=DESKTOP, Riptide sending. The desktop must
# open and the Amiga go on, and the mouse must move at once while a
# program keeps the CPU busy. (Panorama.card 3.2's sender waited for the
# vertical blank with WaitTOF(): the Amiga stopped at Open.) Needs
# FSUAE_PCAP (FS-UAE with Riptide's patch) and root.
#
#   boot 1  PanoramaPrefs INSTALL SETUP=DESKTOP
#   boot 2  the screen modes: the Workbench on one of uaegfx's
#   boot 3  Riptide; PanoramaPrefs OPEN; a busy program; the mouse
#
#   emu/open_test.sh [TIMEOUT]
#   CARD=path emu/open_test.sh   ... with that Panorama.card
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
KICK=${KICK:-/home/user/toolchain/os32/kick.a1200.46.143}
OS3SYS=${OS3SYS:-/home/user/toolchain/os32/sys}
P96=${P96:-/home/user/toolchain/os32/p96/Picasso96Install}
MKICON=${MKICON:-/home/user/Riptide/tools/mkicon.py}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-open}
GROUP=239.255.96.1
AMIGA_IP=192.168.96.2
HOST_IP=192.168.96.1

for f in build/package/Panorama/PanoramaPrefs "$KICK" "$OS3SYS/C/LoadWB" \
         "$P96/Libs/Picasso96/rtg.library" "$MKICON" "$RIPTIDE"; do
	[ -e "$f" ] || { echo "SKIP: $f missing (make package first)"; exit 0; }
done
if [ -z "${FSUAE_PCAP:-}" ] || [ ! -x "$FSUAE_PCAP" ] || [ "$(id -u)" != 0 ]; then
	echo "SKIP: needs FSUAE_PCAP (FS-UAE with Riptide's patch) and root"
	exit 0
fi
command -v xdotool >/dev/null 2>&1 || { echo "SKIP: needs xdotool"; exit 0; }
# an earlier run's viewers or display still there would be photographed instead
if [ -z "${DISPLAY:-}" ] && [ -e /tmp/.X95-lock ] ||
   python3 -c "import socket; socket.create_connection(('127.0.0.1', 8097), 1)" 2>/dev/null; then
	echo "FAIL: display :95 or port 8097 is taken (an earlier run still going?)"
	exit 1
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
# LOSS=5: the viewers lose 5% of what the Amiga sends, as on a busy Wi-Fi
DROP=
[ -n "${LOSS:-}" ] && DROP="--drop $LOSS"

rm -rf "$OUT"
mkdir -p "$OUT/work"
cp -r "$OS3SYS" "$OUT/sys"
sys=$(cd "$OUT/sys" && pwd)
work=$(cd "$OUT/work" && pwd)

# ---- Picasso96 for uaegfx, as emu/p96_test.sh has it
mkdir -p "$sys/Libs/Picasso96"
for f in rtg.library emulation.library uaegfx.card; do
	cp "$P96/Libs/Picasso96/$f" "$sys/Libs/Picasso96/"
done
cp "$P96/Libs/Picasso96API.library" "$sys/Libs/"
cp "$P96/Devs/Picasso96Settings.31" "$sys/Devs/Picasso96Settings"
cp "$P96/Devs/Monitors/Picasso96" "$sys/Devs/Monitors/uaegfx"
python3 "$MKICON" project "$sys/Devs/Monitors/uaegfx.info" --tooltype "BOARDTYPE=uaegfx" >/dev/null
python3 - "$sys/Devs/Monitors/uaegfx.info" <<'EOF'
import sys
p = sys.argv[1]
b = bytearray(open(p, "rb").read())
b[48] = 3   # do_Type: WBTOOL, as monitors are
open(p, "wb").write(b)
EOF
cp -r build/package/Panorama "$work/pkg"
cp "$RIPTIDE" "$work/"
cat > "$sys/S/User-Startup" <<'EOF'
Lab waitwork
If NOT EXISTS Work:
    Wait 1
    Skip waitwork BACK
EndIf
If EXISTS Work:autorun
    Run >NIL: Execute Work:autorun
EndIf
EOF
cat > "$work/riptide.conf" <<EOF
hostname p96test
loglevel 2
interface eth0 device=uaenet.device unit=2
ifconfig eth0 $AMIGA_IP/24 up
route add default $HOST_IP
EOF

cat > "$OUT/test.fs-uae" <<EOF
[fs-uae]
amiga_model = A1200
cpu = 68030
kickstart_file = $KICK
zorro_iii_memory = 65536
graphics_card = uaegfx-z3
graphics_memory = 8192
hard_drive_0 = $sys
hard_drive_0_label = System
hard_drive_1 = $work
hard_drive_1_label = Work
uae_sana2 = 1
uaenet_pcap = p96br0
sound_card = none
floppy_drive_count = 0
automatic_input_grab = 0
video_sync = off
base_dir = $(cd "$OUT" && pwd)/fsuae
EOF
xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :95 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:95
	export DISPLAY
	sleep 1
fi
status=0

# boot DONEFILE: run FS-UAE until Work:DONEFILE exists (during: each second)
during() { :; }
boot() {
	SDL_AUDIODRIVER=dummy "$FSUAE_PCAP" "$OUT/test.fs-uae" >"$OUT/fs-uae-$1.log" 2>&1 &
	upid=$!
	start=$(date +%s)
	while [ ! -f "$work/$1" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for $1"
			import -window root "$OUT/root-timeout.png" 2>/dev/null
			status=1
			break
		fi
		during
		sleep 1
	done
	kill $upid 2>/dev/null
	wait $upid 2>/dev/null
}

cat > "$work/autorun" <<'EOF'
FailAt 21
Wait 8
CD Work:pkg
Work:pkg/PanoramaPrefs INSTALL SETUP=DESKTOP NOREBOOT >Work:install.txt
Echo >Work:boot1.done done
EOF
boot boot1.done

# the Workbench on the Workbench's card (uaegfx, through Wrap.card), as the
# user has it: one of its 640x480 modes, as emu/p96_test.sh does
cat > "$work/autorun" <<'EOF'
FailAt 21
Wait 8
C:mctest LIST >Work:modes2.txt
Echo >Work:bootm.done done
EOF
boot bootm.done
id=$(tr -d '\r' < "$work/modes2.txt" 2>/dev/null | awk '$2 == 640 && $4 == 480 && $6 == 8 && $7 ~ /^(Wrap|UAE):/ { print $1; exit }')
echo "Workbench mode: $id"
mkdir -p "$sys/Prefs/Env-Archive/Sys"
python3 - "$sys/Prefs/Env-Archive/Sys/screenmode.prefs" "$id" <<'PYEOF'
import struct, sys
prhd = struct.pack(">BBI", 0, 0, 0)
scrm = struct.pack(">4IIHHHH", 0, 0, 0, 0, int(sys.argv[2], 16), 640, 480, 8, 0)
body = b"PREF" + b"PRHD" + struct.pack(">I", len(prhd)) + prhd + \
       b"SCRM" + struct.pack(">I", len(scrm)) + scrm
open(sys.argv[1], "wb").write(b"FORM" + struct.pack(">I", len(body)) + body)
PYEOF
# the card to try
[ -n "${CARD:-}" ] && cp "$CARD" "$sys/Libs/Picasso96/Panorama.card"
cat > "$work/autorun" <<'EOF'
FailAt 21
Wait 6
Run >Work:riptide.log Work:Riptide CONFIG=Work:riptide.conf
Wait 8
Work:pkg/PanoramaPrefs OPEN >NIL:
Wait 10
C:mctest WHERE >Work:where0.txt
C:mctest WINDOWS >Work:windows0.txt
Echo >Work:mark-w0 x
Wait 3
Run >NIL: C:mctest BUSY=25
Wait 3
Date >Work:t1.txt
C:mctest MOUSE=5,0,10
C:mctest WHERE >Work:where-busy.txt
Date >Work:t2.txt
Wait 30
C:mctest STATUS >Work:status1.txt
Echo >Work:boot2.done done
EOF

# a viewer, for the card's sender to send to
python3 client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP \
	--snapshot "$OUT/screen.png" --duration "$TIMEOUT" 2>"$OUT/snapshot.log" &
spid=$!
during() {
	if [ -f "$work/mark-w0" ] && [ ! -f "$OUT/open.png" ]; then
		import -window root "$OUT/open.png" 2>/dev/null
	fi
}
[ $status -eq 0 ] && boot boot2.done
kill $spid 2>/dev/null
wait $spid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for f in where0.txt windows0.txt t1.txt t2.txt where-busy.txt; do
	echo "---- $f"
	tr -d '\r' < "$work/$f" 2>/dev/null
done
echo "---- status"
tr -d '\r' < "$work/status1.txt" 2>/dev/null | grep -i "error\|Copper\|pass of\|Fast"
cat "$OUT/snapshot.log" 2>/dev/null

check() {
	if eval "$1"; then
		echo "PASS: $2"
	else
		echo "FAIL: $2"
		status=1
	fi
}
secs() { tr -d '\r' < "$work/$1" 2>/dev/null | awk '{ split($3, t, ":"); print t[1] * 3600 + t[2] * 60 + t[3] }'; }
check 'tr -d "\r" < "$work/where0.txt" 2>/dev/null | grep -q "^front Panorama desktop"' \
	"Open: the desktop opens in front, and the Amiga goes on"
t1=$(secs t1.txt) t2=$(secs t2.txt)
check '[ -n "$t1" ] && [ -n "$t2" ] && [ $((t2 - t1)) -le 3 ]' \
	"... the mouse moves at once while a program keeps the CPU busy ($((${t2:-0} - ${t1:-0})) s)"
check 'grep -q "800x600" "$OUT/snapshot.log" 2>/dev/null' "... the viewer has the desktop's first part (800x600)"
echo "screen: $OUT/open.png"
exit $status
