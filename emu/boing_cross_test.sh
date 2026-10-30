#!/bin/sh
# Panorama - Boing started on the Workbench's side, then the mouse over to
# the desktop: the Amiga must go on. As emu/open_test.sh sets the system
# up (AmigaOS with Picasso96 in FS-UAE, a 68030 with 64 MB, the Workbench
# on uaegfx through Wrap.card, PanoramaPrefs INSTALL SETUP=DESKTOP, Riptide
# sending), then: PanoramaPrefs OPEN, the mouse over the desktop's left
# edge to the Workbench, Boing started there (its ball as sprites on the
# desktop), the mouse over the Workbench's right edge back to the desktop.
# A user's Amiga froze there. Needs FSUAE_PCAP (FS-UAE with Riptide's
# patch) and root.
#
#   emu/boing_cross_test.sh [TIMEOUT]
#   CARD=path emu/boing_cross_test.sh   ... with that Panorama.card
#   LOAD='C:mctest TOF=40' ...          ... that program instead of Boing
#   EXTRA_PREFS='SPRITES=0' ...         ... these lines added to Panorama.prefs
#   INSTANCE=1 ...                      ... on a network, display and group of
#                                           its own: several runs at once
#   NOWRAP=1 WBMODE=UAE ...             ... without Wrap.card at all
#   WBMODE=UAE ...                      ... the Workbench on the UAE board itself, not through Wrap.card
#   SERIAL=1 ...                        ... the Amiga's serial output (a card
#                                           built with make DEBUG=1) to serial-*.log
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
KICK=${KICK:-/home/user/toolchain/os32/kick.a1200.46.143}
OS3SYS=${OS3SYS:-/home/user/toolchain/os32/sys}
P96=${P96:-/home/user/toolchain/os32/p96/Picasso96Install}
MKICON=${MKICON:-/home/user/Riptide/tools/mkicon.py}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
INSTANCE=${INSTANCE:-0}
OUT=${OUT:-build/emu-boingcross$INSTANCE}
LOAD=${LOAD:-Work:pkg/C/Boing SECS=40}
GROUP=239.255.96.1
AMIGA_IP=192.168.$((96 + INSTANCE)).2
HOST_IP=192.168.$((96 + INSTANCE)).1
VETH=p96b${INSTANCE}
XDISP=$((95 + INSTANCE))

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
if [ -z "${DISPLAY:-}" ] && [ -e /tmp/.X$XDISP-lock ]; then
	echo "FAIL: display :$XDISP is taken (an earlier run still going?)"
	exit 1
fi

ip link del ${VETH}0 2>/dev/null
ip link add ${VETH}0 type veth peer name ${VETH}1 || { echo "SKIP: cannot create a veth pair"; exit 0; }
trap 'ip link del ${VETH}0 2>/dev/null' EXIT
ip link set ${VETH}0 up
ip link set ${VETH}1 up
ip addr add $HOST_IP/24 dev ${VETH}1
if command -v ethtool >/dev/null 2>&1; then
	for d in ${VETH}0 ${VETH}1; do ethtool -K $d tso off gso off gro off tx off rx off >/dev/null 2>&1; done
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
uaenet_pcap = ${VETH}0
sound_card = none
floppy_drive_count = 0
automatic_input_grab = 0
video_sync = off
base_dir = $(cd "$OUT" && pwd)/fsuae
EOF
SERPORT=$((1234 + INSTANCE))
[ -n "${SERIAL:-}" ] && echo "serial_port = tcp://127.0.0.1:$SERPORT" >> "$OUT/test.fs-uae"
# the UAE debugger (F11+D, commands on FS-UAE's stdin): on a hang, the CPU's
# state goes to the fs-uae-*.log
echo "console_debugger = 1" >> "$OUT/test.fs-uae"
rm -f "$OUT/dbg.fifo"
mkfifo "$OUT/dbg.fifo"
exec 7<>"$OUT/dbg.fifo"
xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :$XDISP -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:$XDISP
	export DISPLAY
	sleep 1
fi
status=0

# the Amiga stopped: into the UAE debugger (F11+D in the emulator's window),
# the registers, the last instructions and the code around the PC
debugger_dump() {
	wid=$(xdotool search --classname fs-uae 2>/dev/null | head -1)
	[ -z "$wid" ] && wid=$(xdotool search --name "FS-UAE" 2>/dev/null | head -1)
	[ -z "$wid" ] && { echo "no FS-UAE window for the debugger"; return; }
	xdotool windowactivate --sync "$wid" 2>/dev/null
	xdotool mousemove --window "$wid" 100 100 click 1 2>/dev/null
	sleep 1
	xdotool keydown F11 key d keyup F11
	sleep 2
	printf 'r\ne\nT\nd pc-30 30\nH 40\n' >&7
	sleep 3
	printf 'g\n' >&7
	sleep 1
}

# boot DONEFILE: run FS-UAE until Work:DONEFILE exists (during: each second)
during() { :; }
boot() {
	SDL_AUDIODRIVER=dummy "$FSUAE_PCAP" "$OUT/test.fs-uae" <"$OUT/dbg.fifo" >"$OUT/fs-uae-$1.log" 2>&1 &
	upid=$!
	serpid=
	if [ -n "${SERIAL:-}" ]; then
		python3 - "$SERPORT" "$OUT/serial-$1.log" <<'PYEOF' &
import socket, sys, time
port, out = int(sys.argv[1]), sys.argv[2]
for i in range(60):
    try:
        s = socket.create_connection(("127.0.0.1", port), 2)
        s.settimeout(None)
        break
    except OSError:
        time.sleep(1)
else:
    sys.exit(0)
with open(out, "ab", buffering=0) as f:
    while True:
        d = s.recv(4096)
        if not d:
            break
        f.write(d)
PYEOF
		serpid=$!
	fi
	start=$(date +%s)
	while [ ! -f "$work/$1" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for $1"
			import -window root "$OUT/root-timeout.png" 2>/dev/null
			debugger_dump
			status=1
			break
		fi
		during
		sleep 1
	done
	kill $upid 2>/dev/null
	wait $upid 2>/dev/null
	[ -n "$serpid" ] && { kill $serpid 2>/dev/null; wait $serpid 2>/dev/null; }
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
id=$(tr -d '\r' < "$work/modes2.txt" 2>/dev/null | awk -v pre="^(${WBMODE:-Wrap|UAE}):" '$2 == 640 && $4 == 480 && $6 == 8 && $7 ~ pre { print $1; exit }')
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
# the card to try, and the settings
[ -n "${CARD:-}" ] && cp "$CARD" "$sys/Libs/Picasso96/Panorama.card"
# NOWRAP=1: the Workbench on its board directly, Wrap.card gone
if [ -n "${NOWRAP:-}" ]; then
	rm -f "$sys/Libs/Picasso96/Wrap.card" "$sys/Devs/Monitors/Wrap" "$sys/Devs/Monitors/Wrap.info"
fi
if [ -n "${EXTRA_PREFS:-}" ]; then
	printf '%s\n' "$EXTRA_PREFS" >> "$sys/Prefs/Env-Archive/Panorama.prefs"
fi
cat > "$work/autorun" <<EOF
FailAt 21
Wait 6
Run >Work:riptide.log Work:Riptide CONFIG=Work:riptide.conf
Wait 8
Work:pkg/PanoramaPrefs OPEN >NIL:
Wait 10
C:mctest WHERE >Work:where0.txt
Echo >Work:mark-w0 x
C:mctest MOUSE=-1000,0,1
Wait 1
C:mctest MOUSE=-1,0,2
Wait 2
C:mctest WHERE >Work:where1.txt
Run >Work:boing.txt $LOAD
Wait 4
C:mctest STATUS >Work:status0.txt
Date >Work:t1.txt
C:mctest MOUSE=1000,0,1
Wait 1
C:mctest MOUSE=1,0,2
Wait 2
Date >Work:t2.txt
C:mctest WHERE >Work:where2.txt
Echo >Work:mark-w2 x
Wait 5
Date >Work:t3.txt
C:mctest MOUSE=5,0,10
C:mctest WHERE >Work:where3.txt
Date >Work:t4.txt
Wait 30
C:mctest STATUS >Work:status1.txt
Echo >Work:boot2.done done
EOF

# a viewer, for the card's sender to send to
python3 client/panoview.py --group $GROUP --port 9696 --iface $HOST_IP \
	--snapshot "$OUT/screen.png" --duration "$TIMEOUT" --decode-port $((9700 + INSTANCE)) 2>"$OUT/snapshot.log" &
spid=$!
during() {
	if [ -f "$work/mark-w0" ] && [ ! -f "$OUT/open.png" ]; then
		import -window root "$OUT/open.png" 2>/dev/null
	fi
	if [ -f "$work/mark-w2" ] && [ ! -f "$OUT/crossed.png" ]; then
		import -window root "$OUT/crossed.png" 2>/dev/null
	fi
}
[ $status -eq 0 ] && boot boot2.done
kill $spid 2>/dev/null
wait $spid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for f in where0.txt where1.txt boing.txt t1.txt t2.txt where2.txt t3.txt t4.txt where3.txt; do
	echo "---- $f"
	tr -d '\r' < "$work/$f" 2>/dev/null
done
echo "---- status"
tr -d '\r' < "$work/status1.txt" 2>/dev/null | grep -i "error\|Copper\|pass of\|sprite"
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
	"Open: the desktop opens in front"
check 'tr -d "\r" < "$work/where1.txt" 2>/dev/null | grep -q "^front Workbench"' \
	"... over the left edge: the Workbench in front"
check 'tr -d "\r" < "$work/boing.txt" 2>/dev/null | grep -q "as sprites"' \
	"... Boing started there bounces as sprites on the desktop"
check 'tr -d "\r" < "$work/where2.txt" 2>/dev/null | grep -q "^front Panorama desktop"' \
	"... over the right edge back: the desktop in front, and the Amiga goes on"
t1=$(secs t1.txt) t2=$(secs t2.txt) t3=$(secs t3.txt) t4=$(secs t4.txt)
check '[ -n "$t1" ] && [ -n "$t2" ] && [ $((t2 - t1)) -le 6 ]' \
	"... the crossing took no longer than it should ($((${t2:-0} - ${t1:-0})) s)"
check '[ -n "$t3" ] && [ -n "$t4" ] && [ $((t4 - t3)) -le 3 ]' \
	"... the mouse moves at once while Boing bounces ($((${t4:-0} - ${t3:-0})) s)"
check 'tr -d "\r" < "$work/boing.txt" 2>/dev/null | grep -q "frames in"' \
	"... Boing ran to its end"
echo "screens: $OUT/open.png $OUT/crossed.png"
exit $status
