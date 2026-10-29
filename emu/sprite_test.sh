#!/bin/sh
# Panorama - sprites end to end, on the system Panorama is for: AmigaOS
# (KICK=, OS3SYS=) with Picasso96 (P96=) in FS-UAE, Panorama installed by
# PanoramaPrefs from the package, Riptide on uaenet.device multicasting to
# client/panoview.py on this computer. Boing bounces its ball, made of
# graphics.library sprites, on a Panorama screen. The viewer's window and
# the web view must show the red and white ball, somewhere else a moment
# later, while the screen's own pixels (a --snapshot viewer: the screen
# without what is drawn over it) have no ball in them: the ball is sprites,
# not pixels. Needs FSUAE_PCAP (FS-UAE with Riptide's patch) and root.
#
#   boot 1  PanoramaPrefs INSTALL
#   boot 2  Riptide; a Panorama screen (mctest PUBSCREEN=); Boing on it
#
#   emu/sprite_test.sh [TIMEOUT]
#   LOSS=5 emu/sprite_test.sh     ... the viewers losing 5% of the datagrams
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
KICK=${KICK:-/home/user/toolchain/os32/kick.a1200.46.143}
OS3SYS=${OS3SYS:-/home/user/toolchain/os32/sys}
P96=${P96:-/home/user/toolchain/os32/p96/Picasso96Install}
MKICON=${MKICON:-/home/user/Riptide/tools/mkicon.py}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-sprite}
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
amiga_model = A1200/020
kickstart_file = $KICK
fast_memory = 4096
slow_memory = 1792
graphics_card = uaegfx-z2
graphics_memory = 2048
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
Work:pkg/PanoramaPrefs INSTALL NOREBOOT >Work:install.txt
Echo >Work:boot1.done done
EOF
boot boot1.done

cat > "$work/autorun" <<'EOF'
FailAt 21
Wait 6
Run >Work:riptide.log Work:Riptide CONFIG=Work:riptide.conf
Wait 6
Run >NIL: C:mctest PUBSCREEN=PANORAMA WIDTH=640 HEIGHT=480 DEPTH=16
Wait 6
Run >NIL: C:Boing >Work:boing.txt SIZE=100 SECS=40
; Boing has its output open: the card says when its sprites are there
Lab drawing
Wait 1
C:mctest STATUS >T:status
Search >NIL: T:status " 2 in use" QUIET
If WARN
    Skip drawing BACK
EndIf
Wait 4
Echo >Work:mark-ball x
Wait 6
C:mctest STATUS >Work:status.txt
Wait 20
Echo >Work:boot2.done done
EOF

# red pixels of the ball: how many, and around where
ball() {
	python3 - "$1" <<'PYEOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
px = im.load()
n, sx, sy = 0, 0, 0
for y in range(h):
    for x in range(w):
        r, g, b = px[x, y]
        if r > 200 and g < 60 and b < 60:
            n += 1
            sx += x
            sy += y
print(n, sx // n if n else -1, sy // n if n else -1)
PYEOF
}
# three viewers: in a window (a Python with tkinter), in the web view, and
# one writing the screen's own pixels
TKPY=
for p in python3 python3.12 python3.11; do
	if command -v $p >/dev/null 2>&1 && $p -c "import tkinter" 2>/dev/null; then
		TKPY=$p
		break
	fi
done
vpid=
if [ -n "$TKPY" ]; then
	$TKPY client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP $DROP 2>"$OUT/panoview.log" &
	vpid=$!
fi
python3 client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP --web 8097 --no-browser $DROP \
	2>"$OUT/web.log" &
wpid=$!
python3 client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP \
	--snapshot "$OUT/screen.png" --duration "$TIMEOUT" 2>"$OUT/snapshot.log" &
spid=$!
[ -n "${DUMP:-}" ] && python3 "$DUMP" $GROUP 9696 $HOST_IP "$OUT/dump.pkl" "$TIMEOUT" &
shots=
during() {
	if [ -f "$work/mark-ball" ] && [ -z "$shots" ]; then
		# the viewer's window and the web view's picture, twice, half a
		# second apart
		shots=1
		# a viewer joining now, which never asks for the whole screen
		python3 tests/sprite_join.py $GROUP 9696 $HOST_IP 8 >"$OUT/join.txt" 2>&1 &
		w=$(xdotool search --name "^Panorama" 2>/dev/null | head -1)
		[ -n "$w" ] && xdotool windowraise "$w" 2>/dev/null && sleep 0.5
		top -b -n 1 -w 200 2>/dev/null | head -20 >"$OUT/top.txt"
		# the window, until it shows the ball (how long, in tries a second apart)
		tries=0
		while [ -n "$w" ] && [ $tries -lt 15 ]; do
			tries=$((tries + 1))
			import -window "$w" "$OUT/view-1.png" 2>/dev/null
			set -- $(ball "$OUT/view-1.png" 2>/dev/null)
			[ "${1:-0}" -gt 1500 ] && break
			sleep 1
		done
		echo "$tries" >"$OUT/view-tries.txt"
		sleep 0.5
		[ -n "$w" ] && import -window "$w" "$OUT/view-2.png" 2>/dev/null
		# the web view in a browser (headless Chromium), or else its
		# picture with the sprites drawn in
		if ! node tests/web_shot.js http://127.0.0.1:8097/ "$OUT/web-1.png" "$OUT/web-2.png" \
		     >"$OUT/web_shot.log" 2>&1; then
			for n in 1 2; do
				curl -s -o "$OUT/web-$n.png" "http://127.0.0.1:8097/frame.png?sprites=1"
				sleep 0.5
			done
		fi
		cp "$OUT/screen.png" "$OUT/screen-ball.png" 2>/dev/null
		import -window root "$OUT/root-ball.png" 2>/dev/null
	fi
}
[ $status -eq 0 ] && boot boot2.done
kill $vpid $wpid $spid 2>/dev/null
wait $vpid $wpid $spid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for f in install.txt riptide.log boing.txt; do
	echo "---- $f"
	tr -d '\r' < "$work/$f" 2>/dev/null | tail -n 8
done
echo "---- status"
tr -d '\r' < "$work/status.txt" 2>/dev/null | grep -i "sprite\|Copper\|viewers\|pixels"
echo "---- viewers"
cat "$OUT/panoview.log" "$OUT/web.log" "$OUT/snapshot.log" 2>/dev/null

check() {
	if eval "$1"; then
		echo "PASS: $2"
	else
		echo "FAIL: $2"
		status=1
	fi
}
check 'tr -d "\r" < "$work/boing.txt" | grep -q "as sprites"' "Boing on a Panorama screen: the ball as sprites"
moves() {
	what=$1 label=$2
	set -- $(ball "$OUT/$what-1.png" 2>/dev/null) ; n1=${1:-0} x1=${2:--1} y1=${3:--1}
	set -- $(ball "$OUT/$what-2.png" 2>/dev/null) ; n2=${1:-0} x2=${2:--1} y2=${3:--1}
	check '[ "$n1" -gt 1500 ]' "$label: the red of the ball ($n1 pixels, around $x1,$y1)"
	check '[ "$n2" -gt 1500 ] && [ "$x1,$y1" != "$x2,$y2" ]' \
		"... and half a second later somewhere else (around $x2,$y2)"
}
[ -n "$TKPY" ] && moves view "the viewer's window (in $(cat "$OUT/view-tries.txt" 2>/dev/null) tries)"
moves web "the web view"
set -- $(cat "$OUT/join.txt" 2>/dev/null) ; js=${2:-0}
check '[ "$js" -ge 2 ]' \
	"a viewer joining late, never asking for the whole screen, has the sprites ($(cat "$OUT/join.txt" 2>/dev/null))"
set -- $(ball "$OUT/screen-ball.png" 2>/dev/null) ; ns=${1:-0}
check '[ -f "$OUT/screen-ball.png" ] && [ "$ns" -lt 50 ]' \
	"... drawn over the screen: none of it in the screen's own pixels ($ns red)"
echo "pictures: $OUT/view-1.png $OUT/view-2.png $OUT/web-1.png $OUT/root-ball.png"
exit $status
