# Panorama: notes for an AI agent or a new developer

Read this first. It says what the project is, how to set up the
toolchain from nothing, how to build, test and package, and the rules of
the repository. `README.md` and `dist/ReadMe` describe the product for
users; `docs/COPPER.md` the wire protocol; `docs/WRAP.md` Wrap.card;
`docs/BUILD.md` the toolchain in short.

## What it is

Panorama is a Picasso96 (P96) RTG card driver for the Amiga,
`Panorama.card`, without hardware: its video memory is Fast RAM, and a
background task streams the screen over UDP multicast with a protocol of
drawing orders called Copper. `client/panoview.py` (Tk, with a web page
too) shows it on another machine, and with `INPUT=1` works the Amiga
from there. Around the card:

| Part | Source | Version is in |
|---|---|---|
| `Panorama.card` 3.5 | `src/` (`libstart.s` first, then `card.c`, `sender.c`, `orders.c`, `render.c`, `modes.c`, `sprite.c`, `config.c`) | `src/mcard.h` (`MC_VERSION_STR`, `MC_REVISION`) and `src/libstart.s` (`REVISION`, the `$VER` string): keep the three in step |
| `Wrap.card` 0.6 | `src/wrapstart.s`, `src/wrap.c`, `src/mirror.c` | `src/wrapstart.s` |
| `PanoramaPrefs` 3.5 (MUI, GadTools fallback, Shell) | `prefs/` | `prefs/mcprefs.h` (`MP_VERSION`, `MP_DATE`) |
| `PanoramaDesk` 2.1 (the shared desktop commodity) | `prefs/desktop.c` | `prefs/desktop.c` (`MD_VERSION`), date from `MP_DATE` |
| `panoramapic.datatype` 1.0 (JPEG and PNG decoded by the viewer) | `dt/` | `dt/dtstart.s` |
| `Boing`, `mctest`, `wrapstat`, `dtpic`, `cxlog` | `demos/boing.c`, `tests/*.c` | their `$VER` strings |
| `panoview.py` | `client/panoview.py` | protocol version `VERSION` near the top |

All Amiga code is C89-style C compiled with vbcc for the 68020, plus a
little assembly for library headers. There is no libc startup in the
card: it is linked with `-nostdlib`, and `src/libstart.s` must stay the
first object.

## Toolchain, from nothing

Everything lives under `/home/user/toolchain`; the Makefile and the test
scripts take other places through variables (`VBCC`, `NDK`, `MUI`,
`KICK`, `OS3SYS`, `P96`, `AROSFS`, `RIPTIDE`, `MKICON`, `FSUAE_PCAP`).

### 1. Compilers and the NDK (needed for any build)

```sh
apt-get install -y build-essential wget python3-pip lhasa jlha-utils zip unzip \
    fs-uae xvfb xdotool imagemagick iproute2 ethtool
mkdir -p /home/user/toolchain/{dl,src,vbcc/bin} && cd /home/user/toolchain/dl
wget http://phoenix.owl.de/tags/vbcc0_9h.tar.gz \
     http://phoenix.owl.de/vbcc/current/vbcc_target_m68k-amigaos.lha \
     http://phoenix.owl.de/vbcc/current/vbcc_unix_config.tar.gz \
     http://phoenix.owl.de/tags/vasm.tar.gz \
     http://phoenix.owl.de/tags/vlink.tar.gz \
     https://aminet.net/dev/misc/NDK3.2.lha
cd ../src && for f in vbcc0_9h vasm vlink; do tar xzf ../dl/$f.tar.gz; done
(cd vasm && make CPU=m68k SYNTAX=mot && cp vasmm68k_mot vobjdump ../../vbcc/bin/)
(cd vlink && make && cp vlink ../../vbcc/bin/)
cd vbcc && mkdir -p bin && make TARGET=m68k bin/dtgen
printf 'y\ny\nsigned char\ny\nunsigned char\nn\ny\nsigned short\nn\ny\nunsigned short\nn\ny\nsigned int\nn\ny\nunsigned int\nn\ny\nsigned long long\nn\ny\nunsigned long long\nn\ny\nfloat\nn\ny\ndouble\n' \
  | bin/dtgen machines/m68k/machine.dt machines/m68k/dt.h machines/m68k/dt.c
make TARGET=m68k && cp bin/vbccm68k bin/vc bin/vprof bin/dtgen ../../vbcc/bin/
cd ../../vbcc && lha xq ../dl/vbcc_target_m68k-amigaos.lha \
  && mkdir -p targets && cp -r vbcc_target_m68k-amigaos/targets/m68k-amigaos targets/ \
  && tar xzf ../dl/vbcc_unix_config.tar.gz
mkdir -p ../ndk && cd ../ndk && lha xq ../dl/NDK3.2.lha
pip install amitools machine68k==0.3.0 pillow numpy
```

Result: `/home/user/toolchain/vbcc/bin/{vc,vbccm68k,vasmm68k_mot,vlink,...}`
and `/home/user/toolchain/ndk/Include_H`. `vasm` 2.0f and vbcc 0.9h are
known to work. The bsdsocket headers are the NDK's
`SANA+RoadshowTCP-IP/netinclude`. `amitools` gives `vamos`, which
`make check` uses to run Amiga binaries on Linux.

### 2. MUI 3.8 developer includes (needed for PanoramaPrefs)

`prefs/muigui.c` needs `libraries/mui.h`. Get `mui38dev.lha` (Aminet,
`dev/mui/mui38dev.lha`), unpack it, and put its `Developer/C/Include`
tree at `/home/user/toolchain/mui/include` (so that
`/home/user/toolchain/mui/include/libraries/mui.h` exists), or point
`MUI=` at it. `mui38usr.lha` (the user archive) is what the AmigaOS test
systems install; it is not needed to build.

### 3. Riptide (needed for the package and the network tests)

Riptide is the TCP/IP stack the tests run on the Amiga, and its
`tools/mkicon.py` makes the monitor icon in the package. Clone
`https://github.com/farsonic/riptide` to `/home/user/Riptide` and build
it per its own README (`build/amiga/Riptide` is what the tests load).
Riptide is a separate project: **read-only from here**, never patched to
suit Panorama; if something there is wrong, say so instead.

### 4. FS-UAE with multicast (needed for every `emu-*` test)

Ubuntu's `fs-uae` has no network. Build FS-UAE with Riptide's pcap
patch: `Riptide/tools/fs-uae/build-fs-uae.sh` (needs `libpcap-dev`)
gives `/home/user/toolchain/fs-uae-riptide/fs-uae-3.2.35/fs-uae`. The
tests take it as `FSUAE_PCAP=...` and must run **as root**: each creates
a veth pair (`p96br0`/`p96br1`, or `p96bN0`/`p96bN1` for an instance N),
bridges the emulated Amiga to it and runs a viewer on the host end. They
start their own `Xvfb` (display `:95`, or `:95+N`) and drive the emulator
with `xdotool`.

### 5. Operating systems for the tests (your own copies, never committed)

* **AROS m68k** (free): unpack an `amiga-m68k-boot-iso` nightly into
  `/tmp/claude-0/arosfs` or set `AROSFS=`. Used by `emu-test`, `emu-wrap`,
  `emu-mirror`, `emu-span`, `emu-copper`, `emu-prefs` and the desktop test.
* **AmigaOS 3.2 and Picasso96** (licensed): the AmigaOS tests expect
  `KICK=/home/user/toolchain/os32/kick.a1200.46.143` (an A1200 Kickstart
  3.2 ROM image), `OS3SYS=/home/user/toolchain/os32/sys` (an installed
  Workbench 3.2 directory) and `P96=/home/user/toolchain/os32/p96/Picasso96Install`
  (the Picasso96 installation set; `rtg.library` 40.3945 and
  `picture.datatype` 43.41 are what was tested). `emu-os3` wants an
  AmigaOS 3.1.4 system with MUI 3.8 installed the same way. The user of
  this repository supplied these files from their own legitimate copies;
  they are not in the repository and must never be added to it, pushed,
  packaged or distributed.

## Build

```sh
make            # everything into build/: Panorama.card, Wrap.card, PanoramaPrefs,
                # PanoramaDesk, Boing, mctest, wrapstat, dtpic, cxlog,
                # panoramapic.datatype and the DataTypes descriptors
make check      # host tests: BoardInfo offsets, library header checks, Wrap.card
                # under vamos, the viewer against the card's renderer (tests/test_copper.py,
                # tests/rendertest.c), the viewer's unit tests (tests/test_client.py)
make package    # dist/mkpackage.sh: the staged tree in build/package/Panorama and
                # the archive build/Panorama.lha
make DEBUG=1 package   # the same with KDEBUG() tracing on the serial port (below);
                       # never ship this build
make clean
```

A clean `make` takes well under a minute. vbcc warnings are errors in
spirit: a build should print nothing. If the package build complains
about `mkicon.py`, Riptide is missing or `RIPTIDE_SRC=` points elsewhere.

### What the package holds

`build/Panorama.lha` unpacks to `Panorama/` with `PanoramaPrefs` (run it,
or `PanoramaPrefs INSTALL`), `Libs/Picasso96/{Panorama,Wrap}.card`,
`Devs/Monitors/Panorama` (made by the installer), `C/` tools,
`Classes/DataTypes/panoramapic.datatype`, `Devs/DataTypes/{JPEG,PNG}`,
`Client/panoview.py`, `Scripts/`, `Docs/` and `Source/` (a copy of the
sources). `dist/ReadMe` is the user documentation shipped in it: update
it with every user-visible change.

## Tests on the emulator

Each script is self-contained, boots the emulator one to three times,
prints `PASS:`/`FAIL:` lines and exits non-zero on a failure. They take
5 to 15 minutes each. Run them from the repository root, as root, with
`FSUAE_PCAP` set:

```sh
export FSUAE_PCAP=/home/user/toolchain/fs-uae-riptide/fs-uae-3.2.35/fs-uae
make emu-test      # AROS: the card end to end over multicast
make emu-copper    # AROS: everyday drawing; the viewer must show exactly the card's screen
make emu-p96       # AmigaOS + P96: the desktop install, the mouse across to the Workbench and back
make emu-open      # AmigaOS + P96: Open on a system as users have it (Workbench on uaegfx)
make emu-sprite    # AmigaOS + P96: Boing's sprites, pacing, a late joiner, packet loss (LOSS=5)
make emu-select    # AmigaOS + P96: clicks from a viewer select windows
make emu-picture   # AmigaOS + P96: JPEG and PNG decoded by the viewer
make emu-boingcross  # AmigaOS + P96: Boing from the Workbench, the mouse over to the desktop
emu/desktop_test.sh  # AROS: PanoramaDesk, hotkeys, crossing, a side changed while it runs
```

Before a release, the set that must be green is: `make check`, `emu-open`,
`emu-sprite`, `emu-select`, `emu-picture`, `emu-p96`, `emu-boingcross`
and `emu/desktop_test.sh`. The scripts use fixed resources (display `:95`,
veth `p96br0`), so run them one after another; only
`emu/boing_cross_test.sh` takes `INSTANCE=N` to run several at once.

Known test quirks:

* The AROS desktop test sometimes hits an AROS "Software Failure" in a
  task called NIPC, long before anything of ours runs, and then fails
  everything after it. Run it again; it is not ours.
* A test killed half way leaves `/tmp/.X95-lock` and the veth pair
  behind, and the next run says the display is taken: `rm /tmp/.X95-lock;
  ip link del p96br0`.
* `pkill -f` with a pattern that also appears in your own shell command
  kills your shell. Match with a character class (`pkill -f "fs-ua[e]"`)
  or kill by PID.

## Debugging an Amiga that stops

`emu/boing_cross_test.sh` is the harness that found the 3.4 freeze, and
it has the tools for the next one:

* `make DEBUG=1 package` compiles `KDEBUG(("...", (LONG)x))` lines
  (`include/kdebug.h`) into the card, Wrap.card, PanoramaDesk and mctest:
  they go to the serial port through exec's `RawPutChar()`. The test's
  `SERIAL=1` sets FS-UAE's serial port to a TCP socket and keeps the
  output in `build/emu-boingcross*/serial-*.log`.
* `console_debugger = 1` in the FS-UAE config and F11+D in its window
  enter the UAE debugger, which reads commands from FS-UAE's stdin. On a
  timeout the test does this itself and writes the registers, custom
  registers, the code at the PC and, through `emu/uae_tasks.py`, every
  task with the return addresses on its stack named after the library
  function they are in, to `fs-uae-*.log` and `tasks-*.txt`. `WATCH=1`
  sets memory watchpoints instead.
* `LOAD=`, `ORDER=desk`, `EXTRA_PREFS=`, `CARD=`, `NOWRAP=1`, `RTGINT=1`
  and `SIDETEST=1` vary the scenario; the script's header lists them.

What to know about P96 before touching timing: Picasso96 patches
graphics.library (`WaitTOF`, `LoadView`, `WaitBOVP`, the blitter calls,
`GetRGB32` among others) and holds a display lock around its waits;
graphics' `LoadView()` switches the VERTB interrupt off while it runs.
The card's vertical blank therefore comes from a CIA timer, and with
`WAITTOF=1` the card's own `WaitTOF()` sits above Picasso96's. See the
comments in `src/card.c` and the ReadMe's `WAITTOF=` entry before
changing any of that.

## Rules of the repository

* Develop on a branch and push it; the owner merges. Commit messages
  say what changed and why, in plain prose; no model names or tool
  names in commits, code or comments.
* Prose everywhere (comments, docs, the ReadMe, test messages) is plain
  English in the style of the existing files: short sentences, no
  marketing. Keep `README.md`, `dist/ReadMe`, `docs/COPPER.md` and this
  file in step with the code.
* The protocol is versioned: a change to `include/copper.h` needs the
  same change in `client/panoview.py`, `src/render.c` (the reference
  renderer the viewer must match pixel for pixel) and `docs/COPPER.md`,
  and a test in `tests/test_client.py` or `tests/test_copper.py`.
* Settings: add them to `src/config.c` (the card), `prefs/settings.c`
  (PanoramaPrefs and PanoramaDesk), `dist/ReadMe` and `docs/COPPER.md`.
* Nothing licensed goes into the tree: no Kickstart, Workbench, Picasso96,
  MUI or P96ScreenCx files, and no third-party SDK whose licence is not
  settled (the NativeCall SDK the owner has looked at is one such: local
  experiments only, behind a build option, never in a release).
* The Riptide checkout is upstream, read-only.
* Before saying a change works, run the tests that cover it and quote
  the PASS/FAIL lines. Emulator results are from AmigaOS 3.2 unless the
  script says otherwise; the owner runs AmigaOS 3.1 on real hardware,
  where only the owner can confirm a fix.

## Release checklist

1. Bump the versions of what changed (table above), with the date.
2. `make check`, then the emulator set listed under Tests, all green.
3. Update `dist/ReadMe` (user-visible changes) and, if the protocol or
   settings changed, `docs/COPPER.md` and `README.md`.
4. `make package`; the archive is `build/Panorama.lha`.
5. Commit, push the branch, and tag the release: project releases are
   `v0.1`, `v0.2`, ... (v0.1 = Panorama.card 3.5, PanoramaPrefs 3.5,
   PanoramaDesk 2.1, Wrap.card 0.6, panoramapic.datatype 1.0); a
   component tag such as `panorama-3.5` may sit beside it. Attach
   `build/Panorama.lha`, renamed `Panorama-vX.Y.lha`, to the GitHub
   release.
