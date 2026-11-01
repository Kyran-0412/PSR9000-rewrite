// license:BSD-3-Clause
// copyright-holders:PSR9000-rewrite project
/*************************************************************************************

    Yamaha PSR-9000 arranger keyboard (1999), work-in-progress driver.

    Main CPU: SH7043 (DM board), 4MB program flash (2x 28F160S5).
    Sub CPU:  SH7043 (TG board) driving 2x SWP30.  Not emulated yet.
    LCD:      SED1335 (DP board), 320x240 1-bit.

    The protected boot area (main 0x000000-0x01ffff) is not in the official update
    files, so this driver fakes it: the application's vector table at 0x20000 is
    copied to 0 and execution starts at the application entry point.

    ROM files: the Yamaha V3.06 update files p9000m.001-004 (+ p9000s.bin for the
    sub CPU later). They are copyrighted and not distributed with this source.

    Memory map: being discovered, see docs/EMULATOR.md in the project repo.

**************************************************************************************/

#include "emu.h"

#include "cpu/sh/sh7042.h"
#include "imagedev/floppy.h"
#include "machine/ins8250.h"
#include "machine/intelfsh.h"
#include "machine/upd765.h"
#include "video/sed1330.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"

namespace {

class psr9000_state : public driver_device
{
public:
	psr9000_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_lcdc(*this, "lcdc")
		, m_fdc(*this, "fdc")
		, m_dataflash(*this, "data")
		, m_styleflash(*this, "style")
		, m_uart(*this, "uart%u", 0U)
		, m_rom(*this, "maincpu")
	{ }

	void psr9000(machine_config &config) ATTR_COLD;
	void init_psr9000() ATTR_COLD;

private:
	required_device<sh7043_device> m_maincpu;
	required_device<sed1330_device> m_lcdc;
	required_device<hd63266f_device> m_fdc;
	required_device<intelfsh16_device> m_dataflash, m_styleflash;
	required_device_array<ns16550_device, 4> m_uart;
	required_region_ptr<u32> m_rom;

	void map(address_map &map) ATTR_COLD;
	void lcdc_map(address_map &map) ATTR_COLD;
	void palette_init(palette_device &palette) ATTR_COLD;
};

void psr9000_state::init_psr9000()
{
	// Fake boot ROM: copy the application vector table (0x20000) to address 0.
	for(int i = 0; i < 0x400/4; i++)
		m_rom[i] = m_rom[0x20000/4 + i];
}

void psr9000_state::palette_init(palette_device &palette)
{
	palette.set_pen_color(0, rgb_t(0xdb, 0xe9, 0xff));
	palette.set_pen_color(1, rgb_t(0x36, 0x41, 0xcf));
}

void psr9000_state::map(address_map &map)
{
	// CS0: program flash
	map(0x00000000, 0x003fffff).rom().region("maincpu", 0);
	// CS1: data flash IC27 0x400000-0x5fffff, style flash IC28 0x600000-0x7fffff (28F160S5, 16-bit)
	map(0x00400000, 0x005fffff).rw(m_dataflash, FUNC(intelfsh16_device::read), FUNC(intelfsh16_device::write));
	map(0x00600000, 0x007fffff).rw(m_styleflash, FUNC(intelfsh16_device::read), FUNC(intelfsh16_device::write));
	// CS2/CS3: peripherals, being identified (see docs/EMULATOR.md)
	// 0xc80000/1: SED1335 LCDC (A0=0 status/data, A0=1 command/data read), found at 0x10C0AA
	map(0x00c80000, 0x00c80000).rw(m_lcdc, FUNC(sed1330_device::status_r), FUNC(sed1330_device::data_w));
	map(0x00c80001, 0x00c80001).rw(m_lcdc, FUNC(sed1330_device::data_r), FUNC(sed1330_device::command_w));
	// 0xc00000-2: HD63266F FDC (msr / fifo / extstat). Status poll loop at 0x317786.
	map(0x00c00000, 0x00c00002).m(m_fdc, FUNC(hd63266f_device::map));
	// 0x980000-0x98007f: quad 16550-style UART, channel n at +0x20*n, register r at +4*r+3.
	// Init at 0x125BFE (DLAB, divisor 20 = 31250 baud with a 10 MHz clock -> MIDI).
	for(int ch = 0; ch < 4; ch++)
		map(0x00980000 + 0x20*ch, 0x0098001f + 0x20*ch).lrw8(
			NAME(([this, ch](offs_t offset) { return m_uart[ch]->ins8250_r(offset); })),
			NAME(([this, ch](offs_t offset, u8 data) { m_uart[ch]->ins8250_w(offset, data); }))).umask32(0x000000ff);
	// 0xe80000-3: bit-banged serial lines (clock/data), written at 0x1258D8-0x125988. Unknown target.
	// 0xe00000/1: MSM82C51A USART (TO HOST), reset sequence 00 00 00 40 5D 14 at 0x246370. Not hooked up yet.
	// DRAM area
	map(0x01000000, 0x01ffffff).ram();
}

void psr9000_state::lcdc_map(address_map &map)
{
	map.global_mask(0x7fff);
	map(0x0000, 0x7fff).ram();
}

static void psr9000_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD);
}

static INPUT_PORTS_START( psr9000 )
INPUT_PORTS_END

void psr9000_state::psr9000(machine_config &config)
{
	SH7043(config, m_maincpu, 7_MHz_XTAL * 4); // clock not verified
	m_maincpu->set_addrmap(AS_PROGRAM, &psr9000_state::map);

	auto &palette = PALETTE(config, "palette", FUNC(psr9000_state::palette_init), 2);

	auto &screen = SCREEN(config, "screen").set_lcd();
	screen.set_refresh_hz(60);
	screen.set_screen_update(m_lcdc, FUNC(sed1330_device::screen_update));
	screen.set_size(320, 240);
	screen.set_visarea(0, 320-1, 0, 240-1);
	screen.set_palette(palette);

	SED1330(config, m_lcdc, 8'000'000); // really a SED1335, clock not verified
	m_lcdc->set_screen("screen");
	m_lcdc->set_addrmap(0, &psr9000_state::lcdc_map);

	// 28F160S5: closest MAME model is the TE28F160 (same size/commands, reports a Sharp maker id)
	INTEL_TE28F160(config, m_dataflash);
	INTEL_TE28F160(config, m_styleflash);

	for(int ch = 0; ch < 4; ch++)
		NS16550(config, m_uart[ch], 10_MHz_XTAL); // clock deduced from the MIDI divisor

	HD63266F(config, m_fdc, 16_MHz_XTAL); // clock not verified
	FLOPPY_CONNECTOR(config, "fdc:0", psr9000_floppies, "35hd", floppy_image_device::default_pc_floppy_formats);

	SPEAKER(config, "speaker", 2).front();
}

ROM_START( psr9000 )
	ROM_REGION32_BE( 0x400000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "p9000m.002", 0x000000, 0x160000, CRC(b3bf5f73) SHA1(d25788d86dbd1f6f2817c487ff7db449c817d23e) )
	ROM_LOAD( "p9000m.003", 0x160000, 0x160000, CRC(bf4b3d4c) SHA1(cfb1f108d8d88ff76322f61613c284a6b1226a90) )
	ROM_LOAD( "p9000m.004", 0x2c0000, 0x140000, CRC(d410dfae) SHA1(b9a507ec8e386a6a00fb8f7285b3bd8e7623f235) )

	// Data flash IC27: only the part written by the update (loads at 0x4f0000) is known.
	// (initial contents of the emulated flash; later writes are saved to MAME's nvram folder)
	ROM_REGION16_BE( 0x200000, "data", ROMREGION_ERASEFF )
	ROM_LOAD( "p9000m.001", 0x0f0000, 0x110000, CRC(d9784659) SHA1(2c64885ee56e00bce777e34533ee98c594681227) )

	// Style flash IC28: factory contents not dumped.
	ROM_REGION16_BE( 0x200000, "style", ROMREGION_ERASEFF )
ROM_END

} // anonymous namespace

SYST( 1999, psr9000, 0, 0, psr9000, psr9000, psr9000_state, init_psr9000, "Yamaha", "PSR-9000 (V3.06, WIP)", MACHINE_NO_SOUND | MACHINE_NOT_WORKING )
