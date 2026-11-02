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
3. IN PROGRESS: sub CPU emulated (second SH7043 running p9000s.bin). Findings:
   - main port A bit5 = sub CPU reset, active low (0x127614); sub held in reset at power-on.
   - link = two 32-bit queues: main 0xA00000 <-> sub 0xC00000. Sub gets IRQ0 per incoming
     word (checks ISR bit 7); both sides see "may send" on their port A bit0, main sees
     "word waiting" as port A bit1 low.
   - boot traffic: main AA55AA55, 81000001 -> sub 81000300; then main polls 82000100 and the
     sub answers 83000000 ("not ready") forever.
   - sub sound engine setup checks "VPRM" at the start of the voice parameter ROM (IC33,
     not dumped) and otherwise crashes on its first tick. The driver now plants that 4-byte
     header (experiment): the sub then initialises its 0x900000 chip but still never
     reports ready, probably because the parameter data itself is missing.
   Mask ROMs needed for a faithful emulation (none are in any update file or restore disk):
   IC29 preset styles (2MB), IC33 voice parameters (2MB), wave ROMs (4x 8MB), SWP30 internal
   sine table. Getting them needs a chip dump, or a small dump routine in patched firmware.
   Meanwhile, faking the sub's "ready" answer may be enough to reach the home screen.
   Progress with the stopgaps (fake "ready" 83000001, VPRM header, deep link queues):
   - MAME core fix needed: sh_intc did not implement the ISR flags and mis-cleared edge
     interrupts. Patch: mame/patches/sh_intc_isr.diff (apply to the MAME tree before building).
   - The sub now answers the version query (cmd 85 -> "PSR9000 SUB ROM Ver.3.06 ... Mar 03
     2004") and the main then reads/writes hundreds of tone-generator parameters (cmds 87/89,
     replies 8A). About 1600 messages are exchanged.
   - Current blocker: after ~2 emulated minutes the sub idles with its interrupt mask stuck at
     13, so it stops taking the link interrupt and never answers cmd 84; main waits forever.
     Suspect SR save/restore around task switches (MAME SH-2 accuracy) or missing ROM data.
   - Interrupt map (main): IRQ0 = FDC (0x125580), IRQ2 = quad UART (0x1255FA), IRQ3 = link
     receive (0x1256C8, not raised yet: raising it changes the boot path, see driver comment).
     Sub: IRQ0 = link receive, edge triggered.
   - Command dispatch (sub): table at 0xA401C, descriptor per command {?, collect args,
     execute, ...}. Message word = 0x80|cmd, argc, sub-op, value.
   (Old plan text: emulate the sub CPU (second SH7043 running p9000s.bin, TG board: like an MU128 with
   2x SWP30) and the 0xA00000 link, so the boot handshake completes.
3b. DONE (2026-11): the sub CPU is no longer executed. The driver simulates its link replies
   (HLE, link_main_w/sub_message): 81 hello -> 81000300, 82 op1 "ready?" -> 83000001,
   84 -> 84000000 + 84000000 end marker, 85 op n -> 86 00 n 00 + version string words read
   from p9000s.bin (0xD4A2C/0xD4A64/0xD4C6C/0xD4A6D/0xD4A79), 87/88 parameter writes stored,
   89 read -> 8A 00 00 value, A4 -> A5010000 FFFFFFFF, BE/BF/C0 block transfer ignored.
   No unhandled commands during boot; ~1500 messages; emulation runs ~7x real time.
   Quad UART interrupts wired (0x9800A3 = per-channel interrupt bits, main IRQ2).
3c. More hardware found and wired (2026-11):
   - main CPU is the SH7043A ("A" die): its 2 A/D converters at 0xFFFF8400-13 are now mapped
     (the firmware polls channels 0-4: pedals/wheels; inputs return 0 for now).
   - IRQ0 is shared: port F bit7 = FDC, bit6 = SCSI, port E bit15 = IDE (all active low),
     handler 0x125580. FDC interrupt now connected.
   - 0xA80000 = SCSI controller (SPC7212), register r at +4r+3: r0 interrupt status (bit4 error,
     bit3 ?, bit5 busy), r1 error detail (bit5 selection timeout), r11 low 3 bits bus phase,
     r17-r20 control/target/command (r20 = start, 0x0A = select). Driver code 0x31xxxx,
     interrupt handler 0x31D89C, SCSI lock = OS semaphore 19.
   - OS calls (trapa #63, r0): FEE8 take semaphore, FEE9 release, FEEC wait for message,
     FEF1 sleep, FEF2 ?, FEEE ?. Logged with mame/sys.lua (Lua breakpoints, needs -debug).
   - CURRENT BLOCKER: the boot waits for the SCSI lock, held by a task stuck probing the SCSI
     bus (loop 0x31E14E in 0x315BE8). A fake "selection timeout" interrupt is delivered but
     ignored because the driver is not set up yet at that point; answering "bus phase 1" makes
     it think a device exists. Needs a closer look at the SPC7212 behaviour, or an
     emulator-only shortcut that skips the probe.
   - DONE (shortcut, emulator only): init_psr9000() patches the emulated ROM copy so the SCSI
     driver's "start command" (0x31E096) returns 0 and "fetch result" (0x31E01E) returns
     -0x251 (selection timeout): every SCSI access sees an empty bus. The SCSI tasks no longer
     block. Real SCSI emulation can replace this later.
   - CURRENT BLOCKER (after the SCSI shortcut): the LCD is only written up to ~3.15s. One task
     (stack 0x13F8xxx, message loop 0x2362C -> 0x2AC4E -> 0x2AA82 hook [0x101FFD8] -> 0x29D2A
     callback) sends a message to mailbox 9 at 2.9s (0x234B6, OS call FEEE = wake receiver)
     and then makes no further OS calls. Main also polls tone-generator params 08000100/0200/
     0300 about 42x per second (answered 0 by the sub HLE), possibly waiting for a value.
     Next: identify the mailbox-9 task and what the 08000x00 values mean.
     RESOLVED: the real cause was the LCD copy (see 3d).
3d. DONE (2026-11): HOME SCREEN reached (~4 emulated seconds; MAIN VOLUME mixer, parts,
   style, BPM/TRP/OCT bar all drawn). How the LCD is fed:
   - 0x10BBCA writes CSRW (0x46, address 0) + MWRITE (0x42) to the SED1330, then copies the
     RAM framebuffer to the LCD data port 0xC80000 with DMAC channel 2 (CHCR 0x1404/0x1405:
     auto-request, source increment, byte, interrupt on end), 20 bytes per chunk.
   - Each chunk is started by an MTU0 TGRC input capture (TIOR0L = 9, falling edge on
     TIOC0C, ISR vector 90 -> 0x10BCD4 sets DE); DMA end (DEI2, vector 80 -> 0x10BCF0) queues
     the next chunk and re-enables TGI0C. The TIOC0C signal source is unknown (probably a line
     pulse from the display side); the driver pulses it every 64 us (guess).
   - The firmware enables the DMAC with a LONGWORD write of 1 to the 16-bit DMAOR (0x31F01C,
     in the SCSI init); real hardware evidently takes the low word.
   - MAME core changes (mame/patches/sh_dmac_mtu.diff): DMAC was a register stub; now does
     auto-request transfers (timed, ~4 CPU cycles per unit), sets TE, raises DEI. MTU channels
     get input_capture(); sh7042 gets mtu_input_capture(ch, tgr) and a 32-bit DMAOR handler.
     Noisy register logging silenced (a 12 s run once wrote a 30 GB error.log; run.sh now
     caps files at 2 GB with ulimit -f).
   - Shown values come from fake data (no sub CPU / ROMs): all parts "GrandPno", style
     "Heart Beat", tempo 280.
4. NEXT (now the blocker for interaction): front panel controller (MN101C027, internal mask ROM, so simulate it too). It talks to
   the main CPU on UART channel 3 (31250 baud, 8N2). Main sends 5-byte packets at boot:
   E0 0F 0F 05 08, F0 0F 0F 05 08, then 80..8x 00 00 00 00 (LED/lamp data?). The boot now idles,
   (not needed for boot as far as seen). Protocol from the main CPU's parser (0x8A7FE/0x8A748):
   panel -> main packets are 3 bytes: 80 rr cc = button (row rr&31, column cc&15, cc bit6 =
   pressed; button id = table 0x337514[row*10+col]); D0 hi lo / D8 hi lo = dial +/- (hi*16+lo).
   Service manual: panel CPU MN101C027 scans the switch matrix, drives LEDs, reads the rotary
   encoder (EC201 "TEMPO, DATA") and talks "MIDI" serial to the main board (UART ch3).
5. Sound (SWP30 is emulated in MAME, but the wave and voice-parameter ROMs are not dumped).
