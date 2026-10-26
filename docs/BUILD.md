# Building and testing

The driver is built on Linux with vbcc and the AmigaOS 3.2 NDK, the same
setup as Riptide. Everything goes under `/home/user/toolchain`; override
with `make VBCC=... NDK=...`.

## Toolchain

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
pip install amitools machine68k==0.3.0
```

The bsdsocket headers come from the NDK's `SANA+RoadshowTCP-IP/netinclude`.
The NDK has no CyberGraphX headers; `tests/mctest.c` declares the one call
it needs, and vbcc's own target includes have the inlines.

## Notes on the library build

* The card is linked with `-nostdlib`: no startup code. `src/libstart.s`
  must be the first object, so the library starts with `moveq #-1,d0; rts`.
* vbcc does not use small data unless asked (`-sd`), which matters: a
  library cannot rely on a4.
* BoardInfo functions take register arguments (`__reg("a0")` and so on).
  `SetMemoryMode`, `SetWriteMask`, `SetClearMask` and `SetReadPlane` must
  preserve all registers, so they point at an `rts` in `libstart.s`.

## Wrap.card tests

`make check` also runs `tests/wraptest.sh`: under vamos, `build/wraptest`
plays rtg.library, loading the built `Wrap.card`, which wraps
`build/Fake.card`. vamos only loads files named `*.library`, so the script
copies both cards under such names.

`emu/wrap_test.sh` (`make emu-wrap`) boots AROS in FS-UAE, like the test
below, with `Wrap.card` wrapping `Multicast.card`. It needs neither Riptide
nor networking.

## End-to-end test

`emu/run_test.sh` (`make emu-test`) boots AROS m68k in FS-UAE as an
A4000/040, with the card installed as `LIBS:Picasso96/Multicast.card` and
`DEVS:Monitors/Multicast` (a copy of AROS's P96 monitor driver, which
loads real P96 card drivers). Riptide runs on `uaenet.device`; `mctest`
opens a screen on the card in 8, 16 and 32 bit and draws a test pattern,
and `client/p96view.py` on the Linux side must receive it.

AROS: unpack an AROS `amiga-m68k-boot-iso` nightly into
`/tmp/claude-0/arosfs` (or set `AROSFS`). The test moves AROS's own
`Libs/usergroup.library` aside: AROS's multi-user startup loads it, and
Riptide refuses to run beside another stack's libraries.

Network: Ubuntu's `fs-uae` package is built without slirp, so
`uaenet.device` reaches nothing. Build FS-UAE with Riptide's patch
(`Riptide/tools/fs-uae/build-fs-uae.sh`, needs `libpcap-dev`) and run the
test as root with `FSUAE_PCAP` pointing at it: the Amiga is then bridged to
a veth pair and sends real multicast, which the viewer joins on the host
end.

```sh
FSUAE_PCAP=/home/user/toolchain/fs-uae-riptide/fs-uae-3.2.35/fs-uae make emu-test
```
