#!/bin/sh
# Panorama - PanoramaPrefs on real AmigaOS with MUI 3.8, in FS-UAE: an
# A1200 with a Kickstart ROM (KICK=) booting a system directory made from the
# AmigaOS install disks (OS3SYS=), MUI 3.8's user archive (MUI38=) added and
# assigned as its installer does. PanoramaPrefs opens its window with
# TRACE=; the screen is photographed (OUT/shot-N.png) while it is open, and
# the trace shows how far it got if it hangs. Neither the ROM nor the OS
# comes with Panorama: bring your own.
#
#   emu/os3_test.sh [TIMEOUT] [PAGE...]     PAGE 0-4; 0d ... in the dark theme
#                                           (dark ones last: the choice is kept);
#                                           0:200 ... then command 200 (prefs/ids.h)
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-300}
shift 2>/dev/null
PAGES=${*:-0}
KICK=${KICK:-/home/user/toolchain/os32/kick.a1200.46.143}
OS3SYS=${OS3SYS:-/home/user/toolchain/os32/sys}
MUI38=${MUI38:-/home/user/toolchain/mui/usr/MUI}
OUT=${OUT:-build/emu-os3}

for f in build/PanoramaPrefs "$KICK" "$OS3SYS/C/LoadWB" "$MUI38/Libs/muimaster.library"; do
	[ -e "$f" ] || { echo "SKIP: $f missing"; exit 0; }
done
command -v fs-uae >/dev/null 2>&1 || { echo "SKIP: fs-uae not installed"; exit 0; }
rm -rf "$OUT"
mkdir -p "$OUT/work"
cp -r "$OS3SYS" "$OUT/sys"
sys=$(cd "$OUT/sys" && pwd)
work=$(cd "$OUT/work" && pwd)
cp -r "$MUI38" "$sys/MUI"
cp build/PanoramaPrefs "$work/"
[ -f build/muitest ] && cp build/muitest "$work/"
cat > "$sys/S/User-Startup" <<'EOF'
Assign MUI: SYS:MUI
Assign LIBS: MUI:Libs ADD
Assign LOCALE: MUI:Locale ADD
Assign IMAGES: MUI:Images
Lab waitwork
If NOT EXISTS Work:
    Wait 1
    Skip waitwork BACK
EndIf
If EXISTS Work:autorun
    Run >NIL: Execute Work:autorun
EndIf
EOF
{
	echo 'FailAt 21'
	echo 'Wait 10'
	echo 'Version muimaster.library >Work:mui.txt'
	echo 'If EXISTS Work:muitest'
	echo '  Work:muitest >>Work:mui.txt'
	echo 'EndIf'
	for n in $PAGES; do
		# 0d: page 0 in the dark theme (its menu command, CMD=420); 0:200: page 0, then
		# command 200 (prefs/ids.h)
		page=${n%%[d:]*}
		extra=
		case $n in
		*d) extra="CMD=420" ;;
		*:*) extra="CMD=${n#*:}" ;;
		esac
		echo "Run >Work:prefs-$n.log Work:PanoramaPrefs PAGE=$page $extra TIMEOUT=14 TRACE=Work:trace-$n.txt"
		echo 'Wait 8'
		echo "Echo >Work:shot-$n x"
		echo 'Wait 10'
		echo "Echo >Work:page-$n.done x"
	done
	echo 'Echo >Work:autorun.done done'
} > "$work/autorun"

cat > "$OUT/test.fs-uae" <<EOF
[fs-uae]
amiga_model = A1200/020
kickstart_file = $KICK
fast_memory = 8192
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
	Xvfb :91 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:91
	export DISPLAY
	sleep 1
fi
SDL_AUDIODRIVER=dummy fs-uae "$OUT/test.fs-uae" >"$OUT/fs-uae.log" 2>&1 &
upid=$!
start=$(date +%s)
status=0
while [ ! -f "$work/autorun.done" ]; do
	if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
		echo "FAIL: timeout"
		status=1
		break
	fi
	for m in "$work"/shot-*; do
		[ -f "$m" ] || continue
		n=${m##*/shot-}
		[ -f "$OUT/shot-$n.png" ] || import -window root "$OUT/shot-$n.png" 2>/dev/null
	done
	sleep 1
done
kill $upid 2>/dev/null
wait $upid 2>/dev/null
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null
tr -d '\r' < "$work/mui.txt" 2>/dev/null
for n in $PAGES; do
	echo "---- page $n"
	tr -d '\r' < "$work/trace-$n.txt" 2>/dev/null | tail -5
	if [ -f "$work/page-$n.done" ] && grep -q "window open" "$work/trace-$n.txt" 2>/dev/null; then
		echo "PASS: page $n opens on AmigaOS with MUI 3.8: $OUT/shot-$n.png"
	else
		echo "FAIL: page $n on AmigaOS with MUI 3.8"
		status=1
	fi
done
exit $status
