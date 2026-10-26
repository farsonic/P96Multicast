#!/bin/sh
# P96Multicast - MulticastPrefs on AROS m68k in FS-UAE: installing from the
# package without P96's tools, the window, and a second boot from what it
# set up. No network needed.
#
# Boot 1, on an AROS without the cards: from a package laid out like the
# LhA (Work:pkg/MulticastPrefs, Work:pkg/Libs/Picasso96/*.card),
#   MulticastPrefs SET ...      settings into ENVARC: and ENV:
#   MulticastPrefs INSTALL      cards to LIBS:Picasso96, DEVS:Monitors/Multicast
#                               and its icon
# Boot 2: Multicast.card must come up from that alone (mctest STATUS), and
# MulticastPrefs STATUS must say so. With FSUAE_PCAP (Riptide's FS-UAE) and
# root, boot 2 also starts Riptide, opens a public screen on the card and the
# window on it, a page at a time, and captures each page from the card's
# stream (gui-N.png): FS-UAE's own window shows nothing under Xvfb.
#
#   emu/prefs_test.sh [TIMEOUT]
#
# Environment: AROSFS (default /tmp/claude-0/arosfs), OUT (default
# build/emu-prefs), FSUAE_PCAP, RIPTIDE.
set -u
cd "$(dirname "$0")/.."
TIMEOUT=${1:-240}
AROSFS=${AROSFS:-/tmp/claude-0/arosfs}
OUT=${OUT:-build/emu-prefs}
RIPTIDE=${RIPTIDE:-/home/user/Riptide/build/amiga/Riptide}
GROUP=239.255.96.1
NET=0
if [ -n "${FSUAE_PCAP:-}" ] && [ -x "$FSUAE_PCAP" ] && [ "$(id -u)" = 0 ] && [ -f "$RIPTIDE" ]; then
	NET=1
fi

for f in build/Multicast.card build/Wrap.card build/MulticastPrefs build/mctest \
         "$AROSFS/Devs/Monitors/P96"; do
	[ -f "$f" ] || { echo "SKIP: $f missing"; exit 0; }
done
command -v fs-uae >/dev/null 2>&1 || { echo "SKIP: fs-uae not installed"; exit 0; }

rm -rf "$OUT"
mkdir -p "$OUT/work/pkg/C" "$OUT/work/pkg/Libs/Picasso96"
work=$(cd "$OUT/work" && pwd)
cp build/MulticastPrefs "$work/pkg/"
cp build/mctest "$work/pkg/C/"
cp build/Multicast.card build/Wrap.card "$work/pkg/Libs/Picasso96/"

if [ $NET = 1 ]; then
	ip link del p96br0 2>/dev/null
	if ip link add p96br0 type veth peer name p96br1; then
		trap 'ip link del p96br0 2>/dev/null' EXIT
		ip link set p96br0 up
		ip link set p96br1 up
		ip addr add 192.168.96.1/24 dev p96br1
		for d in p96br0 p96br1; do ethtool -K $d tso off gso off gro off tx off rx off >/dev/null 2>&1; done
		cp "$RIPTIDE" "$work/pkg/C/Riptide"
		cat > "$work/riptide.conf" <<EOT
hostname p96test
interface eth0 device=uaenet.device unit=2
ifconfig eth0 192.168.96.2/24 up
EOT
	else
		NET=0
	fi
fi
[ -f "$AROSFS/Libs/usergroup.library" ] && \
	mv "$AROSFS/Libs/usergroup.library" "$AROSFS/Libs/usergroup.library.aros"

# ---- an AROS without the cards or their settings
rm -f "$AROSFS/Libs/Picasso96/Multicast.card" "$AROSFS/Libs/Picasso96/Wrap.card" \
      "$AROSFS/Devs/Monitors/Multicast" "$AROSFS/Devs/Monitors/Multicast.info" \
      "$AROSFS/Devs/Monitors/Wrap" "$AROSFS/Devs/Monitors/Wrap.info" \
      "$AROSFS/Prefs/Env-Archive/Multicast.prefs" "$AROSFS/Prefs/Env-Archive/Wrap.prefs"
rm -rf "$AROSFS/Storage/Monitors/Multicast" "$AROSFS/Storage/Monitors/Wrap"
if ! grep -q waitwork "$AROSFS/S/User-Startup" 2>/dev/null; then
	cat >> "$AROSFS/S/User-Startup" <<'EOT'
Lab waitwork
If NOT EXISTS Work:
    Wait 1
    Skip waitwork BACK
EndIf
If EXISTS Work:autorun
    Run >NIL: Execute Work:autorun
EndIf
EOT
fi

cat > "$OUT/test.fs-uae" <<EOF2
[fs-uae]
amiga_model = A4000/040
kickstart_file = $AROSFS/boot/amiga/aros-rom.bin
kickstart_ext_file = $AROSFS/boot/amiga/aros-ext.bin
zorro_iii_memory = 65536
hard_drive_0 = $AROSFS
hard_drive_0_label = AROS
hard_drive_1 = $work
hard_drive_1_label = Work
sound_card = none
floppy_drive_count = 0
automatic_input_grab = 0
video_sync = off
base_dir = $(cd "$OUT" && pwd)/fsuae
EOF2
FSUAE=fs-uae
if [ $NET = 1 ]; then
	FSUAE=$FSUAE_PCAP
	printf 'uae_sana2 = 1\nuaenet_pcap = p96br0\n' >> "$OUT/test.fs-uae"
fi

xpid=
if [ -z "${DISPLAY:-}" ]; then
	Xvfb :93 -screen 0 1280x1024x24 >/dev/null 2>&1 &
	xpid=$!
	DISPLAY=:93
	export DISPLAY
	sleep 1
fi
status=0

# boot DONEFILE: run FS-UAE until Work:DONEFILE exists; on a gui-N marker,
# capture page N from the card's stream
boot() {
	SDL_AUDIODRIVER=dummy "$FSUAE" "$OUT/test.fs-uae" >"$OUT/fs-uae-$1.log" 2>&1 &
	upid=$!
	start=$(date +%s)
	shot=0
	while [ ! -f "$work/$1" ]; do
		if ! kill -0 $upid 2>/dev/null || [ $(($(date +%s) - start)) -gt "$TIMEOUT" ]; then
			echo "FAIL: timeout waiting for $1"
			status=1
			break
		fi
		if [ -f "$work/gui-$shot" ]; then
			python3 client/p96view.py --group $GROUP --port 9696 --iface 192.168.96.1 \
				--snapshot "$OUT/gui-$shot.png" --duration 7 2>"$OUT/p96view-$shot.log"
			shot=$((shot + 1))
		fi
		sleep 1
	done
	kill $upid 2>/dev/null
	wait $upid 2>/dev/null
}

cat > "$work/autorun" <<'EOF3'
FailAt 21
Work:pkg/MulticastPrefs SET FPS=10 REFRESH=2 MODES=640x480,800x600,1280x960 >Work:set.txt
Work:pkg/MulticastPrefs INSTALL >Work:install.txt
Work:pkg/MulticastPrefs SHOW >Work:show.txt
List DEVS:Monitors >Work:monitors.txt
Echo >Work:boot1.done done
EOF3
boot boot1.done

{
	echo 'FailAt 21'
	echo 'Work:pkg/C/mctest STATUS >Work:status2.txt'
	echo 'Work:pkg/MulticastPrefs STATUS >Work:prefsstatus2.txt'
	if [ $NET = 1 ]; then
		echo 'Run >Work:riptide.log Work:pkg/C/Riptide CONFIG=Work:riptide.conf'
		echo 'Wait 3'
		echo 'Run >Work:pubscreen.log Work:pkg/C/mctest PUBSCREEN=MCPREFS WIDTH=640 HEIGHT=480 DEPTH=8'
		echo 'Wait 3'
		for n in 0 1 2 3; do
			echo "Run >Work:gui-$n.log Work:pkg/MulticastPrefs PUBSCREEN=MCPREFS PAGE=$n TIMEOUT=12"
			echo 'Wait 3'
			echo "Echo >Work:gui-$n x"
			echo 'Wait 10'
		done
	fi
	echo 'Echo >Work:boot2.done done'
} > "$work/autorun"
[ $status -eq 0 ] && boot boot2.done
[ -n "$xpid" ] && kill "$xpid" 2>/dev/null

for f in set.txt install.txt show.txt monitors.txt status2.txt prefsstatus2.txt; do
	echo "---- $f"
	tr -d '\r' < "$work/$f" 2>/dev/null
done

# ---- checks
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; status=1; }
has() {   # FILE TEXT WHAT
	if tr -d '\r' < "$1" 2>/dev/null | grep -q "$2"; then pass "$3"; else fail "$3"; fi
}
has "$AROSFS/Prefs/Env-Archive/Multicast.prefs" "MODES=640x480,800x600,1280x960" "settings saved to ENVARC:"
has "$work/install.txt" "Copied PROGDIR:Libs/Picasso96/Multicast.card" "Multicast.card copied from the package"
has "$work/install.txt" "Made DEVS:Monitors/Multicast from DEVS:Monitors/P96" "monitor made from AROS's P96 monitor"
[ -f "$AROSFS/Libs/Picasso96/Multicast.card" ] && cmp -s build/Multicast.card "$AROSFS/Libs/Picasso96/Multicast.card" \
	&& pass "LIBS:Picasso96/Multicast.card in place" || fail "LIBS:Picasso96/Multicast.card in place"
grep -q "BOARDTYPE=Multicast" "$AROSFS/Devs/Monitors/Multicast.info" 2>/dev/null \
	&& pass "monitor icon says BOARDTYPE=Multicast" || fail "monitor icon says BOARDTYPE=Multicast"
has "$work/status2.txt" "boards set up: 1" "second boot: Multicast.card set up from the installed files"
has "$work/prefsstatus2.txt" "Multicast.card 1\.[0-9]*: 1 board" "MulticastPrefs STATUS sees it"
if [ $NET = 1 ]; then
	for n in 0 1 2 3; do
		[ -s "$OUT/gui-$n.png" ] && pass "window, page $n, from the card: $OUT/gui-$n.png" ||
			fail "window, page $n"
	done
else
	echo "SKIP: the window (needs FSUAE_PCAP and root, to see it through the card)"
fi
exit $status
