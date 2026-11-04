# AI handoff: PSR-9000 firmware mod (KyranOS)

Read this first, then `CLAUDE.md` (firmware addresses and findings), then the doc for the area
you work on. Written 2026-11-04 at the end of a long session; everything below is pushed on
branch `claude/upbeat-curie-hvv1rw`.

## 1. The project in one paragraph

Kyran owns a Yamaha PSR-9000 arranger keyboard (main firmware V3.06, sub 3.01) that he plays
live at church. We patch Yamaha's existing main-CPU firmware (new code in unused flash, hooks
into existing tables/pointers) to give it a new home screen, a graphical Master EQ page and a
KyranOS boot splash, keeping every sound and style as they are. Because he only gets to the
keyboard occasionally, everything is developed and tested in a MAME emulator we wrote for this
keyboard (also built for Windows). This is a personal project: no public release planned yet.

## 2. Working with Kyran

- Not a programmer, but sharp and design-minded. Explain plainly, outcome first, no jargon.
  He tests the Windows emulator and sends screenshots/feedback; he is very particular about the
  look ("Kyran wants the mockup design EXACTLY; do not invent a different layout").
- Design changes: update/create a mockup in `mockups/` (Python/PIL) AND the firmware code so
  they match. Current: `mockups/home_v11.py`, `mockups/eq_v3.py`, `mockups/splash_v2.py`.
- He receives updates as files: usually only `mod\apply.lua` changes (he replaces it in his
  emulator folder); the full Windows package is a zip (see section 6).
- Decisions he made (do not revisit unless he asks): controls VOL/BRT/REV/PAN on LCD 5-8,
  TEMPO on LCD 1, parts on LCD 2-4; side button F = EQ page; other side buttons do nothing on
  the home screen (stock "solo R1/R2/R3" behaviour disabled on purpose, he does NOT want toggle
  buttons there); Yamaha's own bitmap fonts with capital labels; dotted dividers; fader caps;
  no backup needed before the hardware test (no custom voices); no GitHub release / easy
  style-disk tool for now.

## 3. Status (2026-11-04)

Done and verified in the emulator:
- New home screen (status bar, layer cards, TEMPO/parts/controls row, dial edits values,
  reverb on/off, PAN knob), Master EQ page, KyranOS splash, flicker fixes, style name follows
  the current style, stock side-button actions disabled. Details: `docs/HOME_MOD.md`.
- Emulator: boots and runs every page in the manual (`docs/TEST_REPORT.md`), pixel-exact LCD,
  clickable panel, MIDI A/B to the PC, working floppy drive (Kyran's downloaded style set loads
  via Style Manager and plays; notes come out of MIDI OUT). Details: `docs/EMULATOR.md`.

Next real-world step: FIRST HARDWARE TEST, prepared, not yet done (keyboard is at church).
- Procedure: `docs/FIRST_HARDWARE_TEST.md`; safety reasoning: `docs/RECOVERY.md`.
- Kyran's disk plan (8 floppies, 1.44 MB FAT, one file per disk): Original 1-4 (Yamaha files),
  Splash 2 + Splash 4, Full Mod 2 + Full Mod 4. Disks 1 and 3 are identical for all builds.
  Only MAIN PROGRAM is installed; SUB PROGRAM (`p9000s.bin`) is never touched.
- Order: splash-only test -> reinstall originals (proves recovery) -> full mod.
- Test files were sent to him as `PSR9000-mod-test.zip` (built by `make_image.py`, see section 6).
- When he reports back: check what he saw (photos), especially sound changes for VOL/BRT/REV/
  PAN, EQ values vs the stock M.EQ page, flicker, anything from the old screen showing.

## 4. Recovery: what has been tested vs what is only expected

Keep these apart when talking to Kyran: never present an expected recovery path as proven.

| Recovery path | Status |
|---|---|
| PROGRAM INSTALLER (hold START/STOP at power-on) installing **Yamaha's own** update disks | **Tested**: Kyran updated his keyboard to main 3.06 / sub 3.01 this way before this project. |
| Installer still starting and reinstalling the originals **after a modified main program** was installed | **Not tested yet.** Expected, because the installer lives in the protected boot area (main 0x00000-0x1FFFF, never in any update file, our files leave it blank too) and Yamaha's manual says a failed install is fixed by reinstalling. Step 5 of TEST 1 in docs/FIRST_HARDWARE_TEST.md is the first real test. |
| Installer working if our code crashes at boot | **Not tested.** Expected, because the CPU starts in the boot area and checks START/STOP before jumping to the main program at 0x20000 (inferred, the boot code is not available to read). |
| Flash Protect (boot blocks locked) | **Unknown.** Test mode 001 shows the state; not checked on Kyran's keyboard yet. |
| External flash programmer on IC6/IC7 (2x 28F160S5) if the boot area were ever damaged | **Theoretical**, last resort, needs a repair shop or desoldering/clip; never tried. |
| Sound-board (SUB PROGRAM) recovery after a modified `p9000s.bin` | **Not applicable / not tested**: we have never changed the sub program. |

After the hardware test, update this table with what actually happened.

## 5. Repository map

| Path | What |
|---|---|
| `CLAUDE.md` | Goals, firmware address map, verified findings, ground rules. Keep it current. |
| `README.md` | GitHub overview. RULE: update "What works so far" in the same commit as features. |
| `mod/home/hp.c` | The whole mod in C (home screen, EQ page, hooks). |
| `mod/home/tramp.S` | Trampoline to call the original dial handler. |
| `mod/home/build.py` | Compiles, checks SH-2-only instructions, links at 0x3EE400, writes `build/patches.json` + `build/apply.lua` (only apply.lua is committed). |
| `mod/home/make_image.py` | Writes patched `firmware/mod/p9000m.002-.004` with both checksums fixed; `--splash-only` for the minimal test image. |
| `mockups/` | Design mockups (Python). Some read icons/fonts from the firmware files at run time. |
| `mame/ympsr9000.cpp` | Our MAME driver. `make_layout.py` -> `psr9000.lay` (panel layout). |
| `mame/patches/*.diff` | Required MAME core patches (SH7042/43 DMAC+MTU, INTC, upd765 HD63266F). |
| `mame/crawl.lua` | Scripted panel session (press/dial/wait/snap) used for all testing. |
| `mame/tests/` | Manual test lists A-D + runner. |
| `mame/windows/` | Windows start .bat files and README for Kyran's emulator package. |
| `tools/` | `romcheck.py` (Rom Chk1), `res_extract.py` (UI images), `sh2dis.py` (capstone helpers), `make_floppy.py` (FAT12 image with long names). |
| `docs/` | EMULATOR, HOME_MOD, RECOVERY, TEST_REPORT, FIRST_HARDWARE_TEST, `images/` (README screenshots). |
| `firmware/`, `docs/private/` | Yamaha files: git-ignored, NEVER commit (see sections 8 and 9). |

## 6. Environment (rebuild from scratch; a new container has none of this)

Things that are NOT in the repo and must be recreated:

1. **Yamaha files** (Kyran supplies them; ask if missing): `PSR9000_OS306.zip` from Yamaha UK
   support. Layout used everywhere:
   - `firmware/p9000m.001`-`.004`, `firmware/p9000s.bin`, `firmware/9000update.pdf`
   - `firmware/main_prog.bin` = `.002+.003+.004` concatenated (4 MB)
   - `firmware/roms/psr9000/` = the five files again (MAME rompath)
   - `docs/private/PSR9000_manual_webE.pdf` (V3 update manual), `docs/private/Service_Manual.pdf`
   - `firmware/styles/`: Kyran's style zip, unpacked `.sty` in `sty/`, disk images in `disks/`
     (rebuild with `tools/make_floppy.py`, one image per category).
2. **Cross compiler** for the mod: `sh4-linux-gnu-gcc`/binutils (Debian `gcc-sh4-linux-gnu`),
   Python 3 + Pillow, capstone (`pip install capstone`), Inter fonts are no longer needed.
3. **MAME tree** at `/home/user/mame-build/mame` (upstream mamedev/mame, built at commit
   `b0faca90`; newer should be fine):
   - symlinks: `src/mame/yamaha/ympsr9000.cpp -> repo/mame/ympsr9000.cpp`,
     `src/mame/layout/psr9000.lay -> repo/mame/psr9000.lay`
   - add to `src/mame/mame.lst` after psr2000: `@source:yamaha/ympsr9000.cpp` / `psr9000`
   - `git apply` every file in `mame/patches/`
   - Linux: `make SUBTARGET=psr9000 SOURCES=src/mame/yamaha/ympsr9000.cpp REGENIE=1 NOWERROR=1 USE_QTDEBUG=0 -j4`
     (first build is long; later ones ~1 min). Needs libsdl2-dev, libsdl2-ttf-dev, libfontconfig1-dev.
   - Windows exe (MinGW-w64 cross): `make windows_x64 TARGETOS=windows PTR64=1 MINGW64=/usr CROSS_BUILD=1 OVERRIDE_CC=x86_64-w64-mingw32-gcc OVERRIDE_CXX=x86_64-w64-mingw32-g++ WINDRES=x86_64-w64-mingw32-windres SUBTARGET=psr9000 SOURCES=src/mame/yamaha/ympsr9000.cpp NOWERROR=1 USE_QTDEBUG=0 -j4` (takes a long time; run in the background).
   - When you change a core file, regenerate the matching `mame/patches/*.diff` with `git diff`.
4. **Prepared nvram**: the first boot formats the emulated flash ("Initializing SETUP/STYLE...")
   and takes minutes. We kept a ready copy at `/tmp/emu/nvram` and copied it per run. Recreate
   by booting once for a few emulated minutes with `-nvram_directory /tmp/emu/nvram`.
5. **Wine** (optional, to test the Windows exe): `/usr/lib/wine/wine64`, `xvfb-run`,
   `WINEPREFIX=/tmp/emu/wineprefix`, use `-video gdi` and `import -window root` for screenshots.

Typical commands:
- Build the mod: `python3 mod/home/build.py` (then `python3 mod/home/make_image.py`).
- Headless run with the mod and a script:
  `STEPS=steps.txt CRAWLLOG=log.txt SDL_VIDEODRIVER=dummy ./psr9000 psr9000 -rompath firmware/roms -video soft -sound none -nothrottle -skip_gameinfo -window -seconds_to_run 40 -snapshot_directory snap -snapview native -nvram_directory nv -autoboot_script modcrawl.lua`
  where `modcrawl.lua` = `dofile(".../mod/home/build/apply.lua")` followed by `mame/crawl.lua`.
  Add `-flop disk.img` for a floppy, `-debug -debugger none` + `mame/bp.lua` (BPS=addr,..) for
  breakpoints, `-log` for error.log.
- Kyran's package (scratchpad, rebuilt each time): `PSR9000-emu/` = psr9000.exe, the .bat files
  and README from `mame/windows/`, `mod/apply.lua`, `styles/*.img`, empty `roms/psr9000/` with
  a note. Zip it and send it; never put Yamaha firmware files in it.

## 7. How the mod works (short; details in docs/HOME_MOD.md)

- Code + data linked at 0x3EE400 (free 0xFF area up to 0x3FFFE5); limit 0x3FFF00.
- Hooks (all listed in `build.py`): dial handler 0x6D45C -> hp_dial; UI flush tail 0x24546 ->
  hp_tick; Main Mixer page-1 button tables 0x3B14B0/0x3B153C -> hp_select/hp_step (repeat off);
  16 graphics-library LCD refresh pointers -> hp_refresh (drops stock refreshes while our screen
  is up); fader update callbacks 0x3B1488 -> no-op; home side buttons 0x332C14/24/34/44 ->
  hp_side; mixer default handler 0x3B152C -> hp_default (EXIT closes EQ); splash pointer 0x26010.
- "Our screen is up" = active button table RAM 0x10BA718 == 0x3B152C (stock home page 1).
  The stock home keeps running underneath; we redraw the whole framebuffer 0x10C8AEC.
- No RAM of our own: state lives in unused DMA channel 3 registers (SAR3 checksum, DAR3 flags
  incl. settle counter / EQ band+mode, DMATCR3 saved reverb levels).
- SH-2 constraints: compiled with sh4 gcc in no-FPU mode; no shad/shld, no division, no
  libgcc, no .data/.bss (build.py rejects them). Use the shr/shl/mul/udiv helpers in hp.c.
- Text uses Yamaha's fonts read from the ROM at run time (table 0x357B08); icons from the
  resource pack at 0x4F0000. Nothing of Yamaha's is copied into our code.
- Parameter API: `0x25CE4(category, id, len, data)`; getters per control; EQ = category 12
  (21 Q, 22 FREQ, 23 GAIN, 19 = reset knob); current style number 0x8F400().
- Colours: a SET framebuffer bit is LIGHT (yellow) on the real LCD, clear = blue.

## 8. Ground rules (hard)

- Never commit Yamaha material: firmware files, manuals, style files, disk images, dumps,
  patched firmware images. The patch list `apply.lua` (our bytes only) is fine.
- Never suggest installing firmware without the recovery plan; never change main and sub
  together; never touch flash-writing code; keep both checksums fixed (make_image.py does).
- Commits: clear messages, trailer `Co-Authored-By` + `Claude-Session` lines as given by the
  session; push to the designated branch with `git push -u origin <branch>`; no PRs unless asked;
  no model names in commits or files.
- Keep `README.md`, `CLAUDE.md` and the relevant `docs/` file updated with each change.

## 9. File access and privacy

- Yamaha's firmware, manuals, style files and anything derived from them (patched images, disk
  images, dumps) stay private: in `firmware/` / `docs/private/` (git-ignored) and in zips sent
  directly to Kyran. Never commit or publish them.
- If a needed file is missing (firmware files, manuals, style files, Kyran's disks), **ask Kyran
  for it**. Do not try to recreate it, guess its contents, or download it from unofficial
  sources (ROM sites, forums, mirrors). The only download source used so far is Yamaha's own
  support site for `PSR9000_OS306.zip`, and Kyran supplied that file himself; the style set
  also came from him.
- ROM chips that are not in any update file (preset style IC29, voice IC33, wave ROMs) are not
  available at all. The only legitimate way to get them is dumping Kyran's own keyboard (see
  open items), and those dumps are private too.
- Kyran's personal details (e.g. his email) are only for git authorship, never for other uses.

## 10. Lessons / pitfalls (cost us time)

- MAME skips its red warning screen only with `-seconds_to_run` <= 300; long tests run in chunks.
- Lua `manager.machine.time.seconds` is whole seconds; use `time:as_double()`.
  `register_periodic` runs per frame-ish; for sub-frame timing use breakpoints.
- crawl.lua: after each press it waits 0.6 s, so `dial N` takes ~0.6 s per click.
  Side buttons A-J sit at LCD y = 27 + 39*i (dialog YES is usually G, not F).
- Don't trust `pkill -f`/relative `rm` patterns; use absolute paths. Watch disk space
  (MAME traces with `noloop` can be ~1 GB; error.log once filled the disk -> `ulimit -f`).
- Write/read taps on internal registers can miss; log inside the C++ device if unsure.
- The Full Panel layout (1970 px) is wider than a 1920 monitor -> MAME shrinks it and drops
  pixel rows. Start files use Centre Panel + `-nounevenstretch` for a pixel-exact LCD.
- Floppy: the "Unformatted disk" bug was the HD63266F register at +2 (bit set/reset register
  the firmware self-tests), plus ready line, data rate, DMA DREQ. All fixed; see EMULATOR.md.
- The old style-name pointer (0x108F098) only held the boot preset; use 0x8F400 + name tables.

## 11. Open items / ideas (prioritise with Kyran)

1. Hardware test results (above). Fix whatever he finds.
2. Unverified on hardware: EQ value ranges (standard XG ranges assumed), whether BRT changes
   sound (emulator reads brightness back from the faked sound board, so it shows 0-1 there).
3. Unresolved emulator report: once the LCD froze after a voice page + LCD B (could not
   reproduce; START_PSR9000_DEBUG.bat writes error.log if it happens again).
4. Free for new functions: side buttons A-E and G-J on the home screen (all LCD 1-8 are used).
   A sub-menu for the other volumes (SONG/ACMP/M.PAD/LEFT/MIC) was an original goal, not done.
5. Emulator sound / sound dump (discussed, not started): emulate the sub CPU (`p9000s.bin`) +
   SWP30s with fake ROM contents, then a sub-firmware dumper (reuse the WaveRomChk routine) to
   get the 32 MB wave + 2 MB voice ROMs out via MIDI (~3 h) or SCSI; style ROM IC29 via main.
   Needs a SUB PROGRAM change on hardware later, with the same caution.
6. Deferred by Kyran: GitHub release of the emulator, a no-Python MAKE_STYLE_DISKS.bat,
   720 KB disk support, custom multi-sample voices (long-term goal).
