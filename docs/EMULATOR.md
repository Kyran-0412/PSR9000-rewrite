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

## Windows 11

Plan A: a prebuilt `psr9000.exe` cross-compiled from Linux (to be tried once the driver boots).
Then: put the exe in a folder, create `roms\psr9000\` next to it, copy `p9000m.001`-`.004`
into it, and run `psr9000.exe psr9000 -window`.

Plan B, WSL (Ubuntu inside Windows 11, has graphics and sound):
1. PowerShell as administrator: `wsl --install -d Ubuntu`, reboot, create a user.
2. In the Ubuntu window:
   `sudo apt update && sudo apt install -y git build-essential python3 libsdl2-dev libsdl2-ttf-dev libfontconfig1-dev libpulse-dev libasound2-dev`
3. Clone this repo and MAME, then follow "Building" above (takes 1-2 hours the first time).
4. Copy the update files into `firmware/roms/psr9000/` (Windows drives are under `/mnt/c/`).
5. Run: `./psr9000 psr9000 -rompath ../PSR9000-rewrite/firmware/roms -window`

Plan C, MSYS2 (MAME's official Windows toolchain): follow
https://docs.mamedev.org/initialsetup/compilingmame.html#microsoft-windows-msys2 , then the
same `make SUBTARGET=psr9000 SOURCES=src/mame/yamaha/ympsr9000.cpp ...` line in a MINGW64 shell.

## Fake boot ROM

The protected boot area (main 0x000000-0x01FFFF) is not in the update files. The driver copies
the application's vector table from 0x20000 to 0, so the CPU starts at the application entry
0x206E8. Anything the real boot code sets up first (bus controller, DRAM refresh) must be
emulated or faked as we find it.

## Main CPU memory map (being discovered)

| Address | What | Evidence |
|---|---|---|
| 0x000000-0x3FFFFF | Program flash (CS0), main program | update files load here |
| 0x400000-0x5FFFFF | Data flash IC27, 28F160S5 x16 (emulated, writable) | `p9000m.001` loads at 0x4F0000; firmware writes OTS/MDB/regist here |
| 0x600000-0x7FFFFF | BANKED by port E bit 12: 1 = preset style mask ROM IC29 (MX23C1610, NOT dumped), 0 = style flash IC28 (28F160S5, emulated) | bank switch 0x1277C6; preset style n header at 0x600010+n*0x200, flash style headers at 0x600000+(n-125)*0x200 (0x1361CA) |
| 0x980000-0x98007F | Quad 16550 UART, ch n at +0x20n, reg r at +4r+3 | init at 0x125BFE: DLAB, divisor 20 (=31250 baud MIDI @10 MHz) |
| 0xA00000 | 32-bit data port to/from the SUB CPU ("PIF32"). Status on port A: bit0 = may send, bit1 = 0 when a word is waiting | send 0x2538E0, receive 0x253948. Boot sends AA55AA55, 81000001, BE010000, 00000020, BF200000 + 32 words, then waits for a reply |
| 0xA80000-0xA8004F | byte reads at +0x2C, +0x44 in interrupt handlers (gate array status?) | ISR at 0x20D38 |
| 0xC00000-0xC00002 | HD63266F FDC (msr / fifo / extstat) | poll loop 0x317786, commands via 0x31779A |
| 0xC80000 / 0xC80001 | SED1335 LCDC data / command | code at 0x10C0AA: cmd 0x46, addr, cmd 0x42 |
| 0xD00000-0xD00003 | unknown; read as 6-bit value (`& 0x3F`, debounced) at 0x8BDE0, also polled in ISR | input port? |
| 0xE00000 / 0xE00001 | MSM82C51A USART data / control (TO HOST) | reset sequence `00 00 00 40 5D 14` at 0x246370 |
| 0xE80000-0xE80003 | bit-banged serial lines (clock/data) | 0x1258D8-0x125988 |
| 0x1000000-0x1FFFFFF | DRAM | all RAM variables (0x108F358, 0x11A6xxx...) |

Not dumped (not in the update): factory content of the style flash and the rest of the data
flash. The firmware may complain about missing styles; we fake what is needed.

## Milestones

1. DONE: firmware runs; unmapped accesses logged.
2. DONE: boot screen ("Welcome to PSR-9000 Ver.3") on the emulated LCD. On a fresh flash the
   firmware initialises STYLE, CUSTOM VOICE, ORGAN FLUTES, OTS, MULTI PAD, REGISTRATION and
   saves them (MAME nvram folder); the next boot skips that and stays on the splash screen
   Preset style ROM is not dumped: the driver fills it with 125 empty "No Data" styles so
   style loading works. The boot now stops waiting for the SUB CPU to answer over 0xA00000.
3. NEXT: emulate the sub CPU (second SH7043 running p9000s.bin, TG board: like an MU128 with
   2x SWP30) and the 0xA00000 link, so the boot handshake completes.
4. Panel buttons and the dial (input), so menus can be navigated.
5. Sound (SWP30 is emulated in MAME, but the wave and voice-parameter ROMs are not dumped).
