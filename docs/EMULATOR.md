# PSR-9000 emulator (MAME driver)

Goal: run the real V3.06 firmware on a PC so firmware changes can be tested without the
keyboard. Driver source: `mame/ympsr9000.cpp` (ours, BSD-3-Clause like MAME drivers).

## Building

```bash
git clone --depth 1 https://github.com/mamedev/mame.git
cd mame
ln -s /path/to/PSR9000-rewrite/mame/ympsr9000.cpp src/mame/yamaha/ympsr9000.cpp
# add to src/mame/mame.lst, after the psr2000 entry:
#   @source:yamaha/ympsr9000.cpp
#   psr9000
make SUBTARGET=psr9000 SOURCES=src/mame/yamaha/ympsr9000.cpp REGENIE=1 NOWERROR=1 USE_QTDEBUG=0 -j4
```

Needs SDL2 dev packages (`libsdl2-dev libsdl2-ttf-dev libfontconfig1-dev`). The first build
compiles MAME's core and takes a long time; later builds only recompile the driver.

ROM files go in `firmware/roms/psr9000/` (git-ignored): `p9000m.001`-`p9000m.004` from
Yamaha's V3.06 update (and `p9000s.bin` for the sub CPU later).

Run without a display: `mame/run_headless.sh [seconds] [snapshot-interval]` writes LCD
snapshots plus MAME's `error.log` (unmapped accesses) to `firmware/emu_out/`.

## Fake boot ROM

The protected boot area (main 0x000000-0x01FFFF) is not in the update files. The driver copies
the application's vector table from 0x20000 to 0, so the CPU starts at the application entry
0x206E8. Anything the real boot code sets up first (bus controller, DRAM refresh) must be
emulated or faked as we find it.

## Main CPU memory map (being discovered)

| Address | What | Evidence |
|---|---|---|
| 0x000000-0x3FFFFF | Program flash (CS0), main program | update files load here |
| 0x400000-0x5FFFFF | Data flash IC27 | `p9000m.001` loads at 0x4F0000 |
| 0x600000-0x7FFFFF | Style flash IC28 | Rom Chk1 checks 0x600000-0x7FFFFF |
| 0xC80000 / 0xC80001 | SED1335 LCDC data / command | code at 0x10C0AA: cmd 0x46, addr, cmd 0x42 |
| 0xE00000 / 0xE00001 | MSM82C51A USART data / control (TO HOST) | reset sequence `00 00 00 40 5D 14` at 0x246370 |
| 0x1000000-0x1FFFFFF | DRAM | all RAM variables (0x108F358, 0x11A6xxx...) |
| 0xA00000, 0x800000, 0xC00000-2, 0xD00000, 0xE80003 | unknown peripherals | referenced by code; sub-CPU link, FDC, SCSI, panel? |

Not dumped (not in the update): factory content of the style flash and the rest of the data
flash. The firmware may complain about missing styles; we fake what is needed.

## Milestones

1. Firmware runs without crashing; unmapped accesses logged. 
2. Boot screen on the emulated LCD.
3. Panel buttons and the dial (input), so menus can be navigated.
4. Sub CPU and main-sub link; later SWP30 sound (MAME already emulates the SWP30).
