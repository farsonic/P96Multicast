#!/bin/sh
# Panorama - selecting windows on the desktop from a viewer, the pointer
# going out of the viewer's window and back between clicks (a user: "when I
# move away from the window and back it is often not selectable"). AmigaOS
# (KICK=, OS3SYS=) with Picasso96 (P96=) in FS-UAE, the Workbench on uaegfx
# through Wrap.card, INSTALL SETUP=DESKTOP, Riptide; two Shells on the
# desktop; a real panoview window (a Python with tkinter) worked with
# xdotool: into the window and a click on Shell A, out of the window, back
# and a click on Shell B, and so on. Each click must make its Shell the
# active window. Needs FSUAE_PCAP (FS-UAE with Riptide's patch) and root.
#
#   emu/select_test.sh [TIMEOUT]
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
KICK=${KICK:-/home/user/toolchain/os32/kick.a1200.46.143}
OS3SYS=${OS3SYS:-/home/user/toolchain/os32/sys}
P96=${P96:-/home/user/toolchain/os32/p96/Picasso96Install}
MKICON=${MKICON:-/home/user/Riptide/tools/mkicon.py}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
OUT=${OUT:-build/emu-select}
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
Run >NIL: NewShell "CON:100/100/300/150/ShellA/CLOSE/SCREENPANORAMA"
Run >NIL: NewShell "CON:450/100/300/150/ShellB/CLOSE/SCREENPANORAMA"
Wait 5
C:mctest WHERE >Work:sel0.txt
Echo >Work:mark-s1 x
Lab w1
If NOT EXISTS Work:g1
  Wait 1
  Skip w1 BACK
EndIf
C:mctest WHERE >Work:sel1.txt
Echo >Work:mark-s2 x
Lab w2
If NOT EXISTS Work:g2
  Wait 1
  Skip w2 BACK
EndIf
C:mctest WHERE >Work:sel2.txt
Echo >Work:mark-s3 x
Lab w3
If NOT EXISTS Work:g3
  Wait 1
  Skip w3 BACK
EndIf
C:mctest WHERE >Work:sel3.txt
Echo >Work:mark-s4 x
Lab w4
If NOT EXISTS Work:g4
  Wait 1
  Skip w4 BACK
EndIf
C:mctest WHERE >Work:sel4.txt
Echo >Work:mark-s5 x
Lab w5
If NOT EXISTS Work:g5
  Wait 1
  Skip w5 BACK
EndIf
C:mctest WHERE >Work:sel5.txt
Echo >Work:mark-s6 x
Lab w6
If NOT EXISTS Work:g6
  Wait 1
  Skip w6 BACK
EndIf
C:mctest WHERE >Work:sel6.txt
Echo >Work:mark-s7 x
Lab w7
If NOT EXISTS Work:g7
  Wait 1
  Skip w7 BACK
EndIf
C:mctest WHERE >Work:sel7.txt
Echo >Work:mark-s8 x
Lab w8
If NOT EXISTS Work:g8
  Wait 1
  Skip w8 BACK
EndIf
C:mctest WHERE >Work:sel8.txt
Echo >Work:boot2.done done
EOF

# a real viewer window: a Python with tkinter, worked with xdotool
TKPY=
for p in python3 python3.12 python3.11; do
	if command -v $p >/dev/null 2>&1 && $p -c "import tkinter" 2>/dev/null; then
		TKPY=$p
		break
	fi
done
[ -n "$TKPY" ] || { echo "SKIP: no Python with tkinter"; exit 0; }
$TKPY client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP --span 2x1 \
	2>"$OUT/panoview.log" &
vpid=$!
# Shell A is at 100,100 (300 x 150), Shell B at 450,100, on the desktop's
# first part, which the first viewer window shows pixel for pixel
step() {
	w=$(xdotool search --name "^Panorama screen 1" 2>/dev/null | head -1)
	[ -n "$w" ] || return
	eval $(xdotool getwindowgeometry --shell "$w")
	case $1 in
	1)	xdotool windowraise "$w"; sleep 0.5
		xdotool mousemove $((X + 250)) $((Y + 175)); sleep 0.3; xdotool click 1 ;;
	2|4|6)	xdotool mousemove 20 1000; sleep 1                      # out of every window
		xdotool mousemove $((X + 600)) $((Y + 175)); sleep 0.3; xdotool click 1 ;;
	3|5)	xdotool mousemove 20 1000; sleep 1
		xdotool mousemove $((X + 250)) $((Y + 175)); sleep 0.3; xdotool click 1 ;;
	7)	# a press on Shell B whose release never comes (as a Mac loses the
		# release of the click that gives a window the focus): sent as a
		# viewer would, the real pointer out of the window meanwhile
		xdotool mousemove 20 1000; sleep 1
		python3 tests/send_input.py --group $GROUP --port 9696 --iface $HOST_IP \
			move:600,175 press:1 >"$OUT/press.log" 2>&1
		sleep 1 ;;
	8)	# back in, a plain click on Shell A: the move carries the buttons'
		# true state, the card lets go of the one it thought held
		xdotool mousemove $((X + 250)) $((Y + 175)); sleep 0.3; xdotool click 1 ;;
	esac
	sleep 1
	import -window root "$OUT/step-$1.png" 2>/dev/null
	touch "$work/g$1"
}
during() {
	for n in 1 2 3 4 5 6 7 8; do
		if [ -f "$work/mark-s$n" ] && [ ! -f "$work/g$n" ]; then
			step $n
		fi
	done
}
[ $status -eq 0 ] && boot boot2.done
kill $vpid 2>/dev/null
wait $vpid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for n in 0 1 2 3 4 5 6 7 8; do
	echo "sel$n: $(tr -d '\r' < "$work/sel$n.txt" 2>/dev/null)"
done
cat "$OUT/press.log" 2>/dev/null
cat "$OUT/panoview.log"
check() {
	if eval "$1"; then
		echo "PASS: $2"
	else
		echo "FAIL: $2"
		status=1
	fi
}
active() { tr -d '\r' < "$work/sel$1.txt" 2>/dev/null | sed -n 's/.* active \(.*\)/\1/p'; }
check '[ "$(active 1)" = ShellA ]' "a click in the viewer on Shell A makes it active"
check '[ "$(active 2)" = ShellB ]' "out of the viewer's window and back: a click on Shell B makes it active"
check '[ "$(active 3)" = ShellA ]' "... and again on Shell A"
check '[ "$(active 4)" = ShellB ]' "... and on Shell B"
check '[ "$(active 5)" = ShellA ]' "... and on Shell A"
check '[ "$(active 6)" = ShellB ]' "... and on Shell B"
at() { tr -d '\r' < "$work/sel$1.txt" 2>/dev/null | sed -n 's/.* mouse \([0-9]* [0-9]*\) .*/\1/p'; }
check '[ "$(at 1)" = "250 175" ] && [ "$(at 2)" = "600 175" ]' \
	"the pointer stays where the viewer clicked ($(at 1); $(at 2))"
check '[ "$(active 7)" = ShellB ]' "a press on Shell B whose release is lost selects it"
check '[ "$(active 8)" = ShellA ]' "... and a plain click on Shell A after it still selects (the button let go by the move's state)"
exit $status
