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

#include <deque>

#define LOG_LINK (1U << 1)
#define VERBOSE (LOG_LINK)
#include "logmacro.h"

namespace {

class psr9000_state : public driver_device
{
public:
	psr9000_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_subcpu(*this, "subcpu")
		, m_lcdc(*this, "lcdc")
		, m_fdc(*this, "fdc")
		, m_dataflash(*this, "data")
		, m_styleflash(*this, "style")
		, m_uart(*this, "uart%u", 0U)
		, m_rom(*this, "maincpu")
		, m_subrom(*this, "subcpu")
		, m_styrom(*this, "styrom")
	{ }

	void psr9000(machine_config &config) ATTR_COLD;
	void init_psr9000() ATTR_COLD;

private:
	required_device<sh7043_device> m_maincpu, m_subcpu;
	required_device<sed1330_device> m_lcdc;
	required_device<hd63266f_device> m_fdc;
	required_device<intelfsh16_device> m_dataflash, m_styleflash;
	required_device_array<ns16550_device, 4> m_uart;
	required_region_ptr<u32> m_rom, m_subrom;
	required_region_ptr<u16> m_styrom;
	u16 m_pe = 0;

	// Main <-> sub link ("PIF32" in the gate array): one queue per direction.
	static constexpr size_t LINK_DEPTH = 64; // real depth unknown
	std::deque<u32> m_to_sub, m_to_main;
	u32 m_last_to_main = 0, m_last_to_sub = 0;
	u32 pa_r();
	void pa_w(u32 data);
	virtual void machine_reset() override ATTR_COLD;
	u32 sub_pa_r();
	u32 link_main_r();
	void link_main_w(u32 data);
	u32 link_sub_r();
	void link_sub_w(u32 data);
	void sub_map(address_map &map) ATTR_COLD;
	u16 pe_r() { return m_pe; }
	void pe_w(u16 data) { m_pe = data; }
	u16 style_r(offs_t offset);
	void style_w(offs_t offset, u16 data);
	virtual void machine_start() override ATTR_COLD;

	void map(address_map &map) ATTR_COLD;
	void lcdc_map(address_map &map) ATTR_COLD;
	void palette_init(palette_device &palette) ATTR_COLD;
};

void psr9000_state::init_psr9000()
{
	// Fake boot ROM: copy the application vector table (0x20000) to address 0.
	for(int i = 0; i < 0x400/4; i++)
		m_rom[i] = m_rom[0x20000/4 + i];

	// EXPERIMENT: the sub program checks for "VPRM" at the start of the (undumped) voice
	// parameter ROM before setting up its sound engine (sub 0x121F4).
	{
		u32 *param = (u32 *)memregion("param")->base();
		if(param[0] == 0xffffffff)
			param[0] = 0x5650524d; // "VPRM"
	}

	// Same for the sub CPU: its application vectors are at 0x8000.
	for(int i = 0; i < 0x400/4; i++)
		m_subrom[i] = m_subrom[0x8000/4 + i];

	// The preset style mask ROM is not dumped.  Until it is, fill it with 125 empty
	// "No Data" styles, using the same 512-byte header layout the firmware itself writes
	// for empty flash styles, so style loading does not run off into garbage.
	bool blank = true;
	for(int i = 0; i < 0x100000 && blank; i++)
		blank = m_styrom[i] == 0xffff;
	if(blank) {
		static const u8 hdr[0x30] = {
			0x00,0x00,0x02,0x00,0x04,0x02,0x18,0x08,0x00,0x60,0x00,0x78,0x00,0x00,0x00,0x00,
			'N','o',' ','D','a','t','a',0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
		static const u8 tail[0x18] = {
			0x00,0x00,0x00,0x00,0x1f,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
			0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01 };
		std::vector<u8> img(0x200000, 0xff);
		std::fill(img.begin(), img.begin() + 0x10 + 125*0x200, 0x00);
		for(int n = 0; n < 125; n++) {
			u8 *h = &img[0x10 + n*0x200];
			std::copy(std::begin(hdr), std::end(hdr), h);
			std::copy(std::begin(tail), std::end(tail), h + 0x30);
			std::fill(h + 0x48, h + 0x54, 0x01);
			std::fill(h + 0x54, h + 0x60, 0xff);
		}
		for(int i = 0; i < 0x100000; i++)
			m_styrom[i] = (img[2*i] << 8) | img[2*i+1];
	}
}

void psr9000_state::machine_start()
{
	save_item(NAME(m_pe));
	save_item(NAME(m_last_to_main));
	save_item(NAME(m_last_to_sub));
}

// Main CPU port A: bit0 = 1 when it may send to the sub CPU (0x253884),
// bit1 = 0 when a word from the sub CPU is waiting (0x2538B2).
u32 psr9000_state::pa_r()
{
	u32 r = 0;
	if(m_to_sub.size() < LINK_DEPTH)
		r |= 1;
	if(m_to_main.empty())
		r |= 2;
	return r;
}

// Main CPU port A output: bit5 = sub CPU reset, active low (0x127614: 1 = hold, 0 = release).
void psr9000_state::pa_w(u32 data)
{
	bool run = BIT(data, 5);
	m_subcpu->set_input_line(INPUT_LINE_RESET, run ? CLEAR_LINE : ASSERT_LINE);
	if(!run) {
		m_to_sub.clear();
		m_to_main.clear();
		m_subcpu->set_input_line(0, CLEAR_LINE);
	}
}

void psr9000_state::machine_reset()
{
	// Port A outputs are low after reset, so the sub CPU starts held in reset.
	m_subcpu->set_input_line(INPUT_LINE_RESET, ASSERT_LINE);
	m_to_sub.clear();
	m_to_main.clear();
}

// Sub CPU port A: bit0 = 1 when it may send to the main CPU (sub 0xA43D0).
u32 psr9000_state::sub_pa_r()
{
	return m_to_main.size() < LINK_DEPTH ? 1 : 0;
}

u32 psr9000_state::link_main_r()
{
	if(!m_to_main.empty()) {
		m_last_to_main = m_to_main.front();
		m_to_main.pop_front();
	}
	return m_last_to_main;
}

void psr9000_state::link_main_w(u32 data)
{
	LOGMASKED(LOG_LINK, "main->sub %08x\n", data);
	if(m_to_sub.size() < LINK_DEPTH)
		m_to_sub.push_back(data);
	// The sub CPU learns about incoming words through IRQ0 (it checks ISR bit 7, sub 0xA434E).
	m_subcpu->set_input_line(0, ASSERT_LINE);
}

u32 psr9000_state::link_sub_r()
{
	if(!m_to_sub.empty()) {
		m_last_to_sub = m_to_sub.front();
		m_to_sub.pop_front();
	}
	if(m_to_sub.empty())
		m_subcpu->set_input_line(0, CLEAR_LINE);
	return m_last_to_sub;
}

void psr9000_state::link_sub_w(u32 data)
{
	LOGMASKED(LOG_LINK, "sub->main %08x\n", data);
	if(m_to_main.size() < LINK_DEPTH)
		m_to_main.push_back(data);
}

// 0x600000-0x7fffff is banked by port E bit 12 (set at 0x1277C6): 1 = preset style mask ROM
// (IC29, MX23C1610, not dumped), 0 = style flash (IC28).
u16 psr9000_state::style_r(offs_t offset)
{
	return BIT(m_pe, 12) ? m_styrom[offset] : m_styleflash->read(offset);
}

void psr9000_state::style_w(offs_t offset, u16 data)
{
	if(!BIT(m_pe, 12))
		m_styleflash->write(offset, data);
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
	// CS1: data flash IC27 0x400000-0x5fffff; 0x600000-0x7fffff = style mask ROM IC29 or style flash IC28
	// (both 16-bit), selected by port E bit 12. Preset styles 0-124 at 0x600010 + n*0x200 (0x1361CA).
	map(0x00400000, 0x005fffff).rw(m_dataflash, FUNC(intelfsh16_device::read), FUNC(intelfsh16_device::write));
	map(0x00600000, 0x007fffff).rw(FUNC(psr9000_state::style_r), FUNC(psr9000_state::style_w));
	// CS2/CS3: peripherals, being identified (see docs/EMULATOR.md)
	// 0xc80000/1: SED1335 LCDC (A0=0 status/data, A0=1 command/data read), found at 0x10C0AA
	map(0x00c80000, 0x00c80000).rw(m_lcdc, FUNC(sed1330_device::status_r), FUNC(sed1330_device::data_w));
	map(0x00c80001, 0x00c80001).rw(m_lcdc, FUNC(sed1330_device::data_r), FUNC(sed1330_device::command_w));
	// 0xc00000-2: HD63266F FDC (msr / fifo / extstat). Status poll loop at 0x317786.
	map(0x00c00000, 0x00c00003).m(m_fdc, FUNC(hd63266f_device::map));
	// 0x980000-0x98007f: quad 16550-style UART, channel n at +0x20*n, register r at +4*r+3.
	// Init at 0x125BFE (DLAB, divisor 20 = 31250 baud with a 10 MHz clock -> MIDI).
	for(int ch = 0; ch < 4; ch++)
		map(0x00980000 + 0x20*ch, 0x0098001f + 0x20*ch).lrw8(
			NAME(([this, ch](offs_t offset) { return m_uart[ch]->ins8250_r(offset); })),
			NAME(([this, ch](offs_t offset, u8 data) { m_uart[ch]->ins8250_w(offset, data); }))).umask32(0x000000ff);
	// 0xa00000: 32-bit data port to/from the sub CPU ("PIF32"), send 0x2538E0, receive 0x253948.
	// Status on port A: bit0 = may send (wait for 1), bit1 = 0 when a word is waiting (0x253884/0x2538B2).
	map(0x00a00000, 0x00a00003).rw(FUNC(psr9000_state::link_main_r), FUNC(psr9000_state::link_main_w));
	// 0xe80000-3: bit-banged serial lines (clock/data), written at 0x1258D8-0x125988. Unknown target.
	// 0xe00000/1: MSM82C51A USART (TO HOST), reset sequence 00 00 00 40 5D 14 at 0x246370. Not hooked up yet.
	// DRAM area
	map(0x01000000, 0x01ffffff).ram();
}

// Sub CPU (TG board), laid out like a Yamaha MU128.
void psr9000_state::sub_map(address_map &map)
{
	map(0x00000000, 0x000fffff).rom().region("subcpu", 0);
	// 0x400000-0x5fffff: voice parameter mask ROM IC33 (16Mbit, not dumped). Tables at 0x58xxxx-0x5Axxxx.
	map(0x00400000, 0x005fffff).rom().region("param", 0);
	// 0x800000/0x802000: SWP30 master/slave. MAME's SWP30 needs a dump of the chip's internal
	// sine table (sin-table.bin) and the wave ROMs, neither available yet: plain RAM stand-in.
	map(0x00800000, 0x00803fff).ram();
	// 0x900000-0x900054, 0x902000-0x90200e: unknown (VOP3 effect DSP?)
	map(0x00c00000, 0x00c00003).rw(FUNC(psr9000_state::link_sub_r), FUNC(psr9000_state::link_sub_w));
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
	m_maincpu->read_porta().set(FUNC(psr9000_state::pa_r));
	m_maincpu->write_porta().set(FUNC(psr9000_state::pa_w));
	m_maincpu->read_porte().set(FUNC(psr9000_state::pe_r));
	m_maincpu->write_porte().set(FUNC(psr9000_state::pe_w));

	SH7043(config, m_subcpu, 7_MHz_XTAL * 4); // clock not verified
	m_subcpu->set_addrmap(AS_PROGRAM, &psr9000_state::sub_map);
	m_subcpu->read_porta().set(FUNC(psr9000_state::sub_pa_r));

	config.set_perfect_quantum(m_maincpu);

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

	ROM_REGION32_BE( 0x100000, "subcpu", ROMREGION_ERASEFF )
	ROM_LOAD( "p9000s.bin", 0x000000, 0x100000, CRC(e742bf8f) SHA1(c4b66b059f610a7d7a4885304f1fbd5cc8415a18) )

	// Voice parameter mask ROM IC33 (16Mbit): not dumped.
	ROM_REGION32_BE( 0x200000, "param", ROMREGION_ERASEFF )

	// Wave mask ROMs IC41/IC42/IC44/IC45 (4x MX23C6410, 32MB): not dumped.

	// Preset style mask ROM IC29 (MX23C1610, 2MB): not in the update files, not dumped yet.
	ROM_REGION16_BE( 0x200000, "styrom", ROMREGION_ERASEFF )
	ROM_LOAD( "xw695100.ic29", 0x000000, 0x200000, NO_DUMP )
ROM_END

} // anonymous namespace

SYST( 1999, psr9000, 0, 0, psr9000, psr9000, psr9000_state, init_psr9000, "Yamaha", "PSR-9000 (V3.06, WIP)", MACHINE_NO_SOUND | MACHINE_NOT_WORKING )
