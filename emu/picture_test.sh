#!/bin/sh
# Panorama - pictures decoded by the viewer (panoramapic.datatype): AmigaOS
# (KICK=, OS3SYS=) with Picasso96 (P96=) in FS-UAE, the Workbench on uaegfx
# through Wrap.card, INSTALL SETUP=DESKTOP (which installs the datatype and
# DEVS:DataTypes/JPEG, PNG), Riptide, panoview on this computer (it decodes).
# On the desktop, dtpic shows a JPEG and Multiview a PNG, both of four
# coloured quarters: the viewer must show the colours, and dtpic's loading
# must be quick (the 68020 decodes nothing). Needs FSUAE_PCAP (FS-UAE with
# Riptide's patch) and root.
#
#   boot 1  PanoramaPrefs INSTALL SETUP=DESKTOP
#   boot 2  the screen modes: the Workbench on one of uaegfx's
#   boot 3  Riptide; the desktop; dtpic test.jpg; Multiview test.png
#
#   emu/picture_test.sh [TIMEOUT]
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
KICK=${KICK:-/home/user/toolchain/os32/kick.a1200.46.143}
OS3SYS=${OS3SYS:-/home/user/toolchain/os32/sys}
P96=${P96:-/home/user/toolchain/os32/p96/Picasso96Install}
MKICON=${MKICON:-/home/user/Riptide/tools/mkicon.py}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-picture}
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
# the pictures: 400 x 300, red, green, blue and yellow quarters
python3 - "$work" <<'PYEOF'
import sys
from PIL import Image
im = Image.new("RGB", (400, 300))
for (x0, y0, c) in ((0, 0, (255, 0, 0)), (200, 0, (0, 255, 0)), (0, 150, (0, 0, 255)), (200, 150, (255, 255, 0))):
    im.paste(c, (x0, y0, x0 + 200, y0 + 150))
im.save(sys.argv[1] + "/test.jpg", quality=95)
im.save(sys.argv[1] + "/test.png")
PYEOF
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
Version >Work:picver.txt FULL datatypes/picture.datatype
Run >NIL: C:dtpic Work:test.jpg PUBSCREEN=PANORAMA SECS=14 >Work:dtpic.txt
Wait 10
C:mctest WINDOWS >Work:windows1.txt
Echo >Work:mark-jpg x
Wait 8
Run >NIL: SYS:Utilities/Multiview Work:test.png PUBSCREEN=PANORAMA
Wait 10
C:mctest WINDOWS >Work:windows2.txt
Echo >Work:mark-png x
Wait 6
Echo >Work:boot2.done done
EOF

# the viewer: it decodes the pictures; a second one writes the screen's pixels
TKPY=
for p in python3 python3.12 python3.11; do
	if command -v $p >/dev/null 2>&1 && $p -c "import tkinter" 2>/dev/null; then
		TKPY=$p
		break
	fi
done
vpid=
if [ -n "$TKPY" ]; then
	$TKPY client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP --span 2x1 \
		2>"$OUT/panoview.log" &
	vpid=$!
fi
python3 client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP --decode-port 0 \
	--snapshot "$OUT/screen.png" --duration "$TIMEOUT" 2>"$OUT/snapshot.log" &
spid=$!
[ -n "$TKPY" ] || python3 client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP \
	--web 8097 --no-browser 2>"$OUT/web.log" &
during() {
	for n in jpg png; do
		if [ -f "$work/mark-$n" ] && [ ! -f "$OUT/screen-$n.png" ]; then
			cp "$OUT/screen.png" "$OUT/screen-$n.png" 2>/dev/null || touch "$OUT/screen-$n.png"
			import -window root "$OUT/root-$n.png" 2>/dev/null
		fi
	done
}
[ $status -eq 0 ] && boot boot2.done
kill $vpid $spid 2>/dev/null
wait $vpid $spid 2>/dev/null
pkill -f "panoview.py --group $GROUP --port 9696 --iface $HOST_IP --web 8097" 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for f in install.txt picver.txt dtpic.txt windows1.txt windows2.txt; do
	echo "---- $f"
	tr -d '\r' < "$work/$f" 2>/dev/null | tail -n 12
done
echo "---- viewers"
cat "$OUT/panoview.log" "$OUT/snapshot.log" 2>/dev/null | grep -v "^$"

# the quarters' colours in a picture: red green blue yellow pixel counts
quarters() {
	python3 - "$1" <<'PYEOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
n = [0, 0, 0, 0]
for r, g, b in im.getdata():
    if r > 200 and g < 60 and b < 60: n[0] += 1
    elif g > 200 and r < 60 and b < 60: n[1] += 1
    elif b > 200 and r < 60 and g < 60: n[2] += 1
    elif r > 200 and g > 200 and b < 60: n[3] += 1
print(*n)
PYEOF
}
check() {
	if eval "$1"; then
		echo "PASS: $2"
	else
		echo "FAIL: $2"
		status=1
	fi
}
check 'tr -d "\r" < "$work/install.txt" | grep -q "decoded by the viewer"' "INSTALL puts panoramapic.datatype and the JPEG and PNG descriptors in"
check 'tr -d "\r" < "$work/dtpic.txt" | grep -q "400x300, loaded in"' \
	"dtpic loads the JPEG through datatypes: 400x300 ($(tr -d '\r' < "$work/dtpic.txt" 2>/dev/null | sed -n 's/.*: 400x300, //p'))"
secs=$(tr -d '\r' < "$work/dtpic.txt" 2>/dev/null | sed -n 's/.*loaded in \([0-9]*\)\..*/\1/p')
check '[ -n "$secs" ] && [ "$secs" -le 5 ]' "... in a few seconds at most (the viewer decoded it): $secs s"
set -- $(quarters "$OUT/screen-jpg.png" 2>/dev/null); r=${1:-0} g=${2:-0} b=${3:-0} y=${4:-0}
check '[ "$r" -gt 20000 ] && [ "$g" -gt 20000 ] && [ "$b" -gt 20000 ] && [ "$y" -gt 20000 ]' \
	"the viewer shows the JPEG's four colours ($r red, $g green, $b blue, $y yellow pixels)"
check 'tr -d "\r" < "$work/windows2.txt" | grep -q "test.png"' "Multiview shows the PNG in a window on the desktop"
set -- $(quarters "$OUT/screen-png.png" 2>/dev/null); r=${1:-0} g=${2:-0} b=${3:-0} y=${4:-0}
check '[ "$r" -gt 20000 ] && [ "$g" -gt 20000 ] && [ "$b" -gt 20000 ] && [ "$y" -gt 20000 ]' \
	"... and the viewer shows its four colours ($r red, $g green, $b blue, $y yellow pixels)"
echo "pictures: $OUT/screen-jpg.png $OUT/screen-png.png"
exit $status
