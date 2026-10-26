# P96Multicast

A P96 (Picasso96) graphics card driver for AmigaOS whose "graphics card"
is the network. `Multicast.card` gives rtg.library a board with its video
memory in Fast RAM, and a background process streams the visible screen as
UDP to a multicast group. Any number of machines on the LAN can watch the
Amiga's screen with the viewer in `client/`, or with any receiver that
speaks the protocol in [docs/PROTOCOL.md](docs/PROTOCOL.md).

Status: early. The driver works as a dumb frame buffer in 8, 15/16, 24 and
32 bit modes up to 1920x1200 (settable), tested under AROS m68k's P96 layer
in FS-UAE with the [Riptide](https://github.com/farsonic/Riptide) TCP/IP
stack. There is no hardware sprite and no blitter: rtg.library draws
everything with the CPU, which is what a frame buffer in Fast RAM wants.

## How it works

* `FindCard` allocates the video memory (8 MB by default) from Fast RAM.
* `InitCard` describes the board to rtg.library: RGB formats, maximum
  resolutions, a table of virtual pixel clocks, and the BoardInfo
  functions, and starts the sender process.
* The BoardInfo functions (`SetGC`, `SetDAC`, `SetPanning`,
  `SetColorArray`, `SetDisplay`, ...) only record what is on screen.
* The sender polls: FPS times a second it compares the visible screen with
  a shadow copy in 64 byte × 16 line tiles and sends the tiles that changed
  (single-colour tiles as one pixel), within a byte budget per second. MODE
  and PALETTE are repeated every second and unchanged tiles are refreshed
  slowly, so late joiners and lost packets heal by themselves.
* The sender waits until a TCP/IP stack is running (it never loads
  bsdsocket.library from disk itself), so the card can start at boot
  before the network.

The interface follows the P96 driver documentation at
<https://wiki.icomp.de/wiki/P96_Driver_Development>. The BoardInfo layout
in `include/boardinfo.h` was written from the published offsets (AROS
p96gfx.hidd, WinUAE) and is checked by `tests/offsets.c`.

## Wrap.card

`Wrap.card` is a second driver in this repository: a card driver that
loads a real card driver (`CARD=uaegfx.card` in `ENV:Wrap.prefs`) and sits
between it and rtg.library, putting a thunk in front of every BoardInfo
function. It passes every call through unchanged, counts them and records
what is on display (`wrapstat` shows it), and with `MIRROR=Multicast.card`
it copies the screen onto a second board, so a real board's screen can be
streamed to the network too. See [docs/WRAP.md](docs/WRAP.md).

## Installing

1. Copy `Multicast.card` to `LIBS:Picasso96/`.
2. P96: copy a monitor file in `DEVS:Monitors` and set its `BOARDTYPE`
   ToolType to `Multicast`. AROS: copy `DEVS:Monitors/P96` to
   `DEVS:Monitors/Multicast` (the monitor driver opens the card named after
   itself).
3. Optionally put settings in `ENVARC:Multicast.prefs` (below).
4. Start your TCP/IP stack as usual. The card starts sending as soon as it
   finds the stack.

## Settings

`KEY=VALUE`, one per line, in `ENV:Multicast.prefs` (read at boot and again
when the TCP/IP stack appears), or as ToolTypes of the monitor icon under
P96. `ENV:` wins over ToolTypes.

| Key | Default | |
|---|---|---|
| `DEST` | 239.255.96.1 | destination address, multicast or unicast |
| `PORT` | 9696 | destination UDP port |
| `TTL` | 1 | multicast TTL (1 = this LAN only) |
| `FPS` | 25 | update passes per second (1-50) |
| `RATE` | 1500 | most KB per second to send |
| `REFRESH` | 5 | seconds to resend the whole screen in the background (0 = off) |
| `VRAM` | 8 | video memory in MB (read at boot only) |
| `MAXWIDTH`, `MAXHEIGHT` | 1920, 1200 | largest screen mode offered |
| `BOARDTYPE` | 6 | P96 board type reported to rtg.library (6 = PicassoII) |
| `PRI` | -1 | sender task priority |
| `LOG` | (none) | log file for the sender, e.g. `T:Multicast.log` |
| `QUIET` | 0 | set BIF_QUIET |
| `BOARDS` | 1 | boards to offer (1-4); board n sends to PORT+n and is named `Multicast<n+1>` from the second on. AROS takes one board per card driver, so there only Wrap.card's mirror uses a second |

## Watching

```
python3 client/p96view.py                       # a window (tkinter)
python3 client/p96view.py --iface 192.168.1.10  # join on this interface
python3 client/p96view.py --snapshot amiga.png --duration 30
```

numpy makes the viewer much faster, but is optional.

## Building

vbcc, vasm, vlink and the NDK 3.2; see [docs/BUILD.md](docs/BUILD.md).

```
make            # build/Multicast.card, build/Wrap.card, mctest, wrapstat
make check      # BoardInfo offsets, library header checks, Wrap.card under vamos
python3 tests/test_client.py
make emu-test   # AROS + Riptide in FS-UAE, end to end
make emu-wrap   # AROS in FS-UAE: Wrap.card wrapping Multicast.card
make emu-mirror # ... mirroring it to a second Multicast board, over the network
```

## Layout

| Path | |
|---|---|
| `src/libstart.s` | library header: RomTag, function table |
| `src/card.c` | library init, FindCard, InitCard, the BoardInfo functions |
| `src/sender.c` | the sender process |
| `src/config.c` | settings |
| `src/wrapstart.s`, `src/wrap.c`, `src/mirror.c` | Wrap.card: library header and thunks, the wrapper, the mirror |
| `include/wrap_status.h` | Wrap.card's slot table and status block |
| `include/boardinfo.h` | the P96 BoardInfo structure |
| `include/p96mc_proto.h` | the wire protocol |
| `client/p96view.py` | viewer / receiver |
| `tests/` | offset test, hunk checks, client tests, `mctest` and `wrapstat` (Amiga), `wraptest` with `Fake.card` (vamos) |
| `emu/run_test.sh` | end-to-end test on FS-UAE |
| `emu/wrap_test.sh`, `emu/mirror_test.sh` | Wrap.card on FS-UAE, and its mirror over the network |
