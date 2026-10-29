#!/bin/sh
# Panorama - the default desktop install on real AmigaOS with Picasso96, in
# FS-UAE: an A1200 with a Kickstart ROM (KICK=), a system made from the
# AmigaOS install disks (OS3SYS=), Picasso96 (P96=, its Picasso96Install
# drawer) with uaegfx, and P96ScreenCx (P96CX=) in WBStartup, as a user
# would have them. None of these come with Panorama: bring your own.
#
#   boot 1  PanoramaPrefs INSTALL SETUP=DESKTOP, as from the package
#   boot 2  the screen modes the Workbench's board (Wrap.card around uaegfx)
#           has; the Workbench's screen mode is set to one of them
#   boot 3  the Workbench on uaegfx; PanoramaPrefs OPEN; the mouse pushed
#           over the Workbench's edge to the desktop and back
#
#           Boing on the desktop: its ball as graphics.library sprites,
#           which Panorama.card takes (SPRITES=1)
#   boot 4  FS-UAE grabbing the mouse: crossing with the Amiga's own mouse
#   boot 5  (with MUI 3.8, MUI38=) PanoramaPrefs on the desktop: ctrl alt s
#           moves it to the Workbench, MOVE=SCREEN back; PanoramaDesk CLOSE
#           moves it to the Workbench and closes the desktop
#
#   emu/p96_test.sh [TIMEOUT]
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
KICK=${KICK:-/home/user/toolchain/os32/kick.a1200.46.143}
OS3SYS=${OS3SYS:-/home/user/toolchain/os32/sys}
P96=${P96:-/home/user/toolchain/os32/p96/Picasso96Install}
P96CX=${P96CX:-/home/user/toolchain/os32/p96cx}
MKICON=${MKICON:-/home/user/Riptide/tools/mkicon.py}
MUI38=${MUI38:-/home/user/toolchain/mui/usr/MUI}
OUT=${OUT:-build/emu-p96}

for f in build/package/Panorama/PanoramaPrefs "$KICK" "$OS3SYS/C/LoadWB" \
         "$P96/Libs/Picasso96/rtg.library" "$P96CX/P96ScreenCx" "$MKICON"; do
	[ -e "$f" ] || { echo "SKIP: $f missing (make package first)"; exit 0; }
done
command -v fs-uae >/dev/null 2>&1 || { echo "SKIP: fs-uae not installed"; exit 0; }
rm -rf "$OUT"
mkdir -p "$OUT/work"
cp -r "$OS3SYS" "$OUT/sys"
sys=$(cd "$OUT/sys" && pwd)
work=$(cd "$OUT/work" && pwd)

# ---- Picasso96, as its installer leaves it for uaegfx
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
# ---- P96ScreenCx where its readme says
cp "$P96CX/P96ScreenCx" "$P96CX/P96ScreenCx.info" "$sys/WBStartup/"

cp -r build/package/Panorama "$work/pkg"
: > "$sys/S/User-Startup"
mui=
if [ -e "$MUI38/Libs/muimaster.library" ]; then
	mui=1
	cp -r "$MUI38" "$sys/MUI"
	cat > "$sys/S/User-Startup" <<'EOF'
Assign MUI: SYS:MUI
Assign LIBS: MUI:Libs ADD
Assign LOCALE: MUI:Locale ADD
Assign IMAGES: MUI:Images
EOF
fi
cat >> "$sys/S/User-Startup" <<'EOF'
Lab waitwork
If NOT EXISTS Work:
    Wait 1
    Skip waitwork BACK
EndIf
If EXISTS Work:autorun
    Run >NIL: Execute Work:autorun
EndIf
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
sound_card = none
floppy_drive_count = 0
automatic_input_grab = 0
video_sync = off
base_dir = $(cd "$OUT" && pwd)/fsuae
EOF
xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :92 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:92
	export DISPLAY
	sleep 1
fi
status=0

# boot DONEFILE: run FS-UAE until Work:DONEFILE exists; a mark-N: the screen
boot() {
	SDL_AUDIODRIVER=dummy fs-uae "$OUT/test.fs-uae" >"$OUT/fs-uae-$1.log" 2>&1 &
	upid=$!
	start=$(date +%s)
	while [ ! -f "$work/$1" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for $1"
			status=1
			break
		fi
		for m in "$work"/mark-*; do
			[ -f "$m" ] || continue
			n=${m##*/mark-}
			[ -f "$OUT/shot-$n.png" ] || import -window root "$OUT/shot-$n.png" 2>/dev/null
		done
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
Work:pkg/PanoramaPrefs SHOW >Work:show1.txt
List DEVS:Monitors >Work:monitors1.txt
Echo >Work:boot1.done done
EOF
boot boot1.done

cat > "$work/autorun" <<'EOF'
FailAt 21
Wait 8
C:mctest LIST >Work:modes2.txt
C:wrapstat >Work:wrapstat2.txt
Echo >Work:boot2.done done
EOF
[ $status -eq 0 ] && boot boot2.done

# the Workbench on the Workbench's card: a 640x480 mode of the Wrap board
id=$(tr -d '\r' < "$work/modes2.txt" 2>/dev/null | awk '$2 == 640 && $4 == 480 && $6 == 8 && $7 ~ /^(Wrap|UAE):/ { print $1; exit }')
if [ -z "$id" ]; then
	echo "FAIL: no 640x480x8 mode on the Wrap board:"
	tr -d '\r' < "$work/modes2.txt" 2>/dev/null | head -20
	status=1
else
	mkdir -p "$sys/Prefs/Env-Archive/Sys"
	python3 - "$sys/Prefs/Env-Archive/Sys/screenmode.prefs" "$id" <<'EOF'
import struct, sys
prhd = struct.pack(">BBI", 0, 0, 0)
scrm = struct.pack(">4IIHHHH", 0, 0, 0, 0, int(sys.argv[2], 16), 640, 480, 8, 0)
body = b"PREF" + b"PRHD" + struct.pack(">I", len(prhd)) + prhd + \
       b"SCRM" + struct.pack(">I", len(scrm)) + scrm
open(sys.argv[1], "wb").write(b"FORM" + struct.pack(">I", len(body)) + body)
EOF
fi

cat > "$work/autorun" <<'EOF'
FailAt 21
Wait 10
C:mctest WHERE >Work:where-boot.txt
Echo >Work:mark-wb x
Work:pkg/PanoramaPrefs OPEN >Work:open.txt
Wait 10
C:mctest WHERE >Work:where-open.txt
C:mctest MOUSE=3,0,1
Wait 2
C:mctest WHERE >Work:where-local.txt
Echo >Work:mark-local x
C:wrapstat >Work:wrapstat3.txt
C:mctest STATUS >Work:status3.txt
Status >Work:tasks3.txt
Run >NIL: NewShell "CON:40/40/300/100/DeskShell/CLOSE/SCREENPANORAMA"
Wait 4
C:PanoramaDesk
Wait 3
C:mctest WHERE >Work:where0.txt
Echo >Work:mark-desktop x
Wait 2
C:mctest MOUSE=-2,0,3
Wait 3
C:mctest WHERE >Work:where1.txt
Echo >Work:mark-crossed x
Wait 2
C:mctest MOUSE=200,0,2
Wait 1
C:mctest MOUSE=2,0,3
Wait 3
C:mctest WHERE >Work:where2.txt
Echo >Work:mark-back x
Wait 2
Run >NIL: C:Boing >Work:boing.txt SECS=8
Wait 14
C:mctest STATUS >Work:status-boing.txt
Echo >Work:mark-boing x
Echo >Work:boot3.done done
EOF
[ $status -eq 0 ] && boot boot3.done

# ---- boot 4: FS-UAE grabbing the computer's mouse (its default), moved with
# xdotool as a person would: the Amiga's own mouse, crossing to the desktop
# (shown by the viewers) over the Workbench's edge and back. No clicks.
grab=
if command -v xdotool >/dev/null 2>&1; then
	grab=1
	sed 's/^automatic_input_grab = 0/automatic_input_grab = 1/' "$OUT/test.fs-uae" >"$OUT/grab.fs-uae"
	{
		echo 'FailAt 21'
		echo 'Wait 10'
		echo 'Work:pkg/PanoramaPrefs OPEN >NIL:'
		echo 'Wait 8'
		echo 'C:mctest WHERE >Work:grab0.txt'
		for n in 1 2 3; do
			echo "Echo >Work:mark-g$n x"
			echo "Lab w$n"
			echo "If NOT EXISTS Work:g$n"
			echo '  Wait 1'
			echo "  Skip w$n BACK"
			echo 'EndIf'
			echo "C:mctest WHERE >Work:grab$n.txt"
		done
		echo 'Echo >Work:boot4.done done'
	} >"$work/autorun"
	SDL_AUDIODRIVER=dummy fs-uae "$OUT/grab.fs-uae" >"$OUT/fs-uae-boot4.log" 2>&1 &
	upid=$!
	start=$(date +%s)
	n=1
	while [ ! -f "$work/boot4.done" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for boot4.done"
			status=1
			break
		fi
		if [ $n -le 3 ] && [ -f "$work/mark-g$n" ]; then
			case $n in
			1)	# into the window, grabbed with a click; a small move
				w=$(xdotool search --name "FS-UAE" 2>/dev/null | head -1)
				eval $(xdotool getwindowgeometry --shell "$w")
				xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) click 1
				sleep 1
				for i in 1 2 3; do xdotool mousemove_relative -- 10 0; sleep 0.1; done ;;
			2)	# right, over the Workbench's right edge
				for i in $(seq 1 40); do xdotool mousemove_relative -- 30 0; sleep 0.05; done ;;
			3)	# left, back over the desktop's left edge
				for i in $(seq 1 40); do xdotool mousemove_relative -- -30 0; sleep 0.05; done ;;
			esac
			sleep 2
			import -window root "$OUT/grab-$n.png" 2>/dev/null
			touch "$work/g$n"
			n=$((n + 1))
		fi
		sleep 1
	done
	kill $upid 2>/dev/null
	wait $upid 2>/dev/null
fi

# ---- boot 5: a MUI program on the desktop (the default public screen) goes
# to the Workbench and back with the hotkey and MOVE=SCREEN, and to the
# Workbench when the desktop closes: Exchange's Hide, then Show
if [ -n "$mui" ] && [ $status -eq 0 ]; then
	cat > "$work/autorun" <<'EOF'
FailAt 21
Wait 10
Work:pkg/PanoramaPrefs OPEN >NIL:
Wait 8
Run >NIL: Work:pkg/PanoramaPrefs
Wait 10
C:mctest WINDOWS >Work:move0.txt
C:mctest BROKERS >Work:brokers0.txt
C:mctest WHERE >Work:where-move0.txt
Echo >Work:mark-move0 x
C:mctest KEY=33
Wait 8
C:mctest WINDOWS >Work:movekey.txt
C:mctest WHERE >Work:where-movekey.txt
Echo >Work:mark-movekey x
C:PanoramaDesk MOVE=SCREEN WINDOW="Panorama Preferences"
Wait 4
C:mctest WINDOWS >Work:movecmd.txt
C:mctest WHERE >Work:where-movecmd.txt
Echo >Work:mark-movecmd x
C:PanoramaDesk CLOSE
Wait 10
C:mctest WINDOWS >Work:move1.txt
C:PanoramaDesk INFO >Work:move1-info.txt
Echo >Work:mark-move1 x
Wait 2
Echo >Work:boot5.done done
EOF
	boot boot5.done
fi
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for f in install.txt show1.txt monitors1.txt wrapstat2.txt where-boot.txt open.txt where-open.txt \
         where-local.txt wrapstat3.txt status3.txt where0.txt where1.txt where2.txt; do
	echo "---- $f"
	tr -d '\r' < "$work/$f" 2>/dev/null
done

# "front TITLE WxH mouse X Y active TITLE". Moves are small: Intuition's
# acceleration overflows a big one (+2000 lands at 0).
front() { tr -d '\r' < "$work/$1" 2>/dev/null | sed -n 's/^front \(.*\) [0-9]*x[0-9]* mouse .*/\1/p'; }
width() { tr -d '\r' < "$work/$1" 2>/dev/null | sed -n 's/.* \([0-9]*\)x[0-9]* mouse .*/\1/p'; }
mousex() { tr -d '\r' < "$work/$1" 2>/dev/null | sed -n 's/.* mouse \([0-9-]*\) .*/\1/p'; }
check() {
	if eval "$1"; then
		echo "PASS: $2"
	else
		echo "FAIL: $2"
		status=1
	fi
}
check '[ "$(front where-boot.txt)" = Workbench ] && [ "$(width where-boot.txt)" = 640 ]' \
	"the Workbench on the uaegfx board, 640 wide"
check '[ "$(front where-open.txt)" = "Panorama desktop" ]' "PanoramaPrefs OPEN: the desktop opens in front"
check '[ "$(front where-local.txt)" = Workbench ]' \
	"... the Amiga's own mouse (the emulator's, in its window) moves: the Workbench in front again"
check '[ "$(front where0.txt)" = "Panorama desktop" ]' "PanoramaDesk brings the desktop to the front"
active() { tr -d '\r' < "$work/$1" 2>/dev/null | sed -n 's/.* active \(.*\)/\1/p'; }
check '[ "$(active where0.txt)" = DeskShell ]' "a Shell on the desktop, active"
check '[ -f "$OUT/shot-desktop.png" ] && [ "$(convert "$OUT/shot-desktop.png" -colorspace gray -format "%[fx:mean>0.1]" info:)" = 1 ]' \
	"the desktop in front: the Workbench still shown (KEEPDISPLAY)"
x1=$(mousex where1.txt)
check '[ "$(front where1.txt)" = Workbench ] && [ -n "$x1" ] && [ "$x1" -ge $(( $(width where1.txt) - 16 )) ]' \
	"the mouse over the desktop's left edge: the Workbench, at its right edge"
check '[ "$(active where1.txt)" != DeskShell ] && [ -n "$(active where1.txt)" ]' \
	"... the keys go with it: a Workbench window active ($(active where1.txt))"
x2=$(mousex where2.txt)
check '[ "$(front where2.txt)" = "Panorama desktop" ] && [ -n "$x2" ] && [ "$x2" -le 12 ]' \
	"... and over the Workbench's right edge: the desktop, at its left edge"
check '[ "$(active where2.txt)" = DeskShell ]' "... the keys go with it: the desktop's Shell active again"
for f in boing.txt status-boing.txt; do echo "---- $f"; tr -d '\r' < "$work/$f" 2>/dev/null; done
sline() { tr -d '\r' < "$work/status-boing.txt" 2>/dev/null | sed -n "s/.*$1.*/&/p"; }
check 'tr -d "\r" < "$work/boing.txt" | grep -q "as sprites"' "Boing on the desktop: the ball as sprites"
check 'sline "sprites: graphics.library" | grep -q "[1-9][0-9]* moves"' \
	"... Panorama.card has them: images and moves ($(sline "sprites: graphics" | sed "s/.*use, //"))"
check 'sline "last sprite image" | grep -q "64x100, [1-9][0-9]* + [1-9][0-9]* + 0 pixels"' \
	"... a sprite of the ball, 64 x 100, red and white ($(sline "last sprite image" | sed "s/.*image: //"))"
if [ -n "$grab" ]; then
	for n in 0 1 2 3; do tr -d '\r' < "$work/grab$n.txt" 2>/dev/null; done
	check '[ "$(front grab0.txt)" = "Panorama desktop" ]' "FS-UAE grabbing the mouse: after Open, the desktop in front"
	check '[ "$(front grab1.txt)" = Workbench ]' "... its mouse moves: the Workbench in front"
	check '[ "$(front grab2.txt)" = "Panorama desktop" ]' "... pushed right, over the Workbench's edge: the desktop"
	check '[ "$(front grab3.txt)" = Workbench ]' "... pushed left, over the desktop's edge: the Workbench"
fi
if [ -n "$mui" ]; then
	for f in move0.txt brokers0.txt where-move0.txt movekey.txt where-movekey.txt movecmd.txt \
	         where-movecmd.txt move1.txt move1-info.txt; do echo "---- $f"; tr -d '\r' < "$work/$f" 2>/dev/null; done
	check 'tr -d "\r" < "$work/move0.txt" | grep -q "^screen Panorama desktop:.*Panorama Preferences"' \
		"PanoramaPrefs opens on the desktop (the default public screen)"
	check 'tr -d "\r" < "$work/movekey.txt" | grep -q "^screen Workbench:.*Panorama Preferences" &&
	       [ "$(front where-movekey.txt)" = Workbench ] && [ "$(active where-movekey.txt)" = "Panorama Preferences" ]' \
		"ctrl alt s: PanoramaPrefs to the Workbench, in front and active"
	check 'tr -d "\r" < "$work/movecmd.txt" | grep -q "^screen Panorama desktop:.*Panorama Preferences" &&
	       [ "$(front where-movecmd.txt)" = "Panorama desktop" ]' \
		"PanoramaDesk MOVE=SCREEN: back to the desktop, in front"
	check 'tr -d "\r" < "$work/move1.txt" | grep -q "^screen Workbench:.*Panorama Preferences" &&
	       ! tr -d "\r" < "$work/move1.txt" | grep -q "^screen Panorama desktop"' \
		"PanoramaDesk CLOSE: PanoramaPrefs moved to the Workbench, the desktop closed"
fi
echo "screens: $OUT/shot-*.png"
exit $status
