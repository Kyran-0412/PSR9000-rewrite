// license:BSD-3-Clause
// copyright-holders:PSR9000-rewrite project
/*************************************************************************************

    Yamaha PSR-9000 arranger keyboard (1999), work-in-progress driver.

    Main CPU: SH7043 (DM board), 4MB program flash (2x 28F160S5).
    Sub CPU:  SH7043 (TG board) driving 2x SWP30.  Not emulated: its replies on the
              main<->sub link are simulated (HLE), see link_main_w().  No sound.
    LCD:      SED1335 (DP board), 320x240 1-bit.

    The protected boot area (main 0x000000-0x01ffff) is not in the official update
    files, so this driver fakes it: the application's vector table at 0x20000 is
    copied to 0 and execution starts at the application entry point.

    ROM files: the Yamaha V3.06 update files p9000m.001-004 and p9000s.bin (the sub
    program, only used for its version strings). They are copyrighted and not distributed with this source.

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
#include <map>

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
		, m_lcdc(*this, "lcdc")
		, m_fdc(*this, "fdc")
		, m_dataflash(*this, "data")
		, m_styleflash(*this, "style")
		, m_uart(*this, "uart%u", 0U)
		, m_rom(*this, "maincpu")
		, m_subrom(*this, "subrom")
		, m_styrom(*this, "styrom")
	{ }

	void psr9000(machine_config &config) ATTR_COLD;
	void init_psr9000() ATTR_COLD;

private:
	required_device<sh7043a_device> m_maincpu;
	required_device<sed1330_device> m_lcdc;
	required_device<hd63266f_device> m_fdc;
	required_device<intelfsh16_device> m_dataflash, m_styleflash;
	required_device_array<ns16550_device, 4> m_uart;
	required_region_ptr<u32> m_rom;
	required_region_ptr<u8> m_subrom;
	required_region_ptr<u16> m_styrom;
	u16 m_pe = 0;
	u8 m_uart_irq = 0;
	// IRQ0 is shared: port F bit7 = FDC, bit6 = SCSI (both active low), port E bit15 = IDE (0x125580)
	bool m_fdc_irq = false, m_scsi_irq = false;
	u8 m_scsi[32] = {};              // simulated SCSI controller registers (0xa80000 + 4*r + 3)
	emu_timer *m_scsi_timer = nullptr;
	TIMER_CALLBACK_MEMBER(scsi_timeout);
	emu_timer *m_lcd_timer = nullptr;
	TIMER_CALLBACK_MEMBER(lcd_pulse);
	void update_irq0();
	void fdc_irq_w(int state) { m_fdc_irq = state; update_irq0(); }
	u16 pf_r() { return 0xff3f | (m_fdc_irq ? 0 : 0x80) | (m_scsi_irq ? 0 : 0x40); }
	u8 scsi_r(offs_t offset);
	void scsi_w(offs_t offset, u8 data);               // interrupt outputs of the 4 UART channels (bit n = channel n)
	template <int Ch> void uart_irq_w(int state);

	// Main <-> sub link ("PIF32" in the gate array). The sub side is simulated.
	std::deque<u32> m_to_main;
	u32 m_last_to_main = 0;
	std::vector<u32> m_msg;          // message being received from the main CPU
	int m_msg_left = 0;              // data words still expected for it
	std::map<u32, u8> m_tgparam;     // tone generator parameters written by the main CPU
	u32 pa_r();
	void pa_w(u32 data);
	virtual void machine_reset() override ATTR_COLD;
	u32 link_main_r();
	void link_main_w(u32 data);
	void sub_reply(u32 data);
	void sub_message(const std::vector<u32> &msg);
	u16 pe_r() { return m_pe | 0x8000; } // bit15 = IDE interrupt (active low): none
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

	// EMULATOR-ONLY SHORTCUT (never applied to firmware for the keyboard): the SCSI controller is not
	// emulated well enough, so make the SCSI driver's "fetch command result" routine (0x31E01E)
	// return -0x251 (selection timeout, constant at 0x31E11A) at once. Every SCSI command then
	// behaves like a bus with nothing connected. See docs/EMULATOR.md.
	auto patch16 = [this](offs_t a, u16 v) {
		u32 &w = m_rom[a >> 2];
		w = (a & 2) ? ((w & 0xffff0000) | v) : ((w & 0x0000ffff) | (u32(v) << 16));
	};
	{
		patch16(0x31e01e, 0x907c); // mov.w @(0x31e11a),r0
		patch16(0x31e020, 0x000b); // rts
		patch16(0x31e022, 0x0009); // nop
		// and "start command" (0x31E096) returns 0 at once instead of waiting for the controller
		patch16(0x31e096, 0x000b); // rts
		patch16(0x31e098, 0xe000); // mov #0,r0
	}

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
	m_scsi_timer = timer_alloc(FUNC(psr9000_state::scsi_timeout), this);
	// The LCD copy is paced by falling edges on TIOC0C (MTU0 TGRC input capture): one
	// 20-byte DMA chunk per edge. Source not identified yet (probably a line pulse from
	// the display side); 64 us is a guess at a typical line period.
	m_lcd_timer = timer_alloc(FUNC(psr9000_state::lcd_pulse), this);
	m_lcd_timer->adjust(attotime::from_usec(64), 0, attotime::from_usec(64));
	// NOTE reg 11 low 3 bits = SCSI bus phase (0x31D7DC). The early probe loop (0x31E14E, called
	// from 0x315BE8 before the driver's state 0x12BB1C4 is set by 0x31E978) only ends on phase 1
	// (but then the driver assumes a device and sends INQUIRY) or a completed selection timeout
	// (which the interrupt handler ignores while that state is 0). Still unsolved.
	save_item(NAME(m_pe));
	save_item(NAME(m_last_to_main));
	save_item(NAME(m_uart_irq));
	save_item(NAME(m_fdc_irq));
	save_item(NAME(m_scsi_irq));
	save_item(NAME(m_scsi));
}

// Main CPU port A: bit0 = 1 when it may send to the sub CPU (0x253884),
// bit1 = 0 when a word from the sub CPU is waiting (0x2538B2).
u32 psr9000_state::pa_r()
{
	return m_to_main.empty() ? 3 : 1;
}

// Main CPU port A output: bit5 = sub CPU reset, active low (0x127614: 1 = hold, 0 = release).
void psr9000_state::pa_w(u32 data)
{
	if(!BIT(data, 5)) {
		m_to_main.clear();
		m_msg.clear();
		m_msg_left = 0;
	}
}

void psr9000_state::machine_reset()
{
	m_to_main.clear();
	m_msg.clear();
	m_msg_left = 0;
}

u32 psr9000_state::link_main_r()
{
	if(!m_to_main.empty()) {
		m_last_to_main = m_to_main.front();
		m_to_main.pop_front();
	}
	return m_last_to_main;
}

void psr9000_state::sub_reply(u32 data)
{
	LOGMASKED(LOG_LINK, "sub->main %08x\n", data);
	m_to_main.push_back(data);
	// The main CPU also has an IRQ3 handler for incoming words (0x1256C8 -> 0x253E00); it is not
	// raised because the boot code polls (raising it changed the boot path with the real sub).
}

// Link messages: word 0 = 0x80|cmd, number of data words, sub-op, value; then the data words.
// The sub's handlers are in a table at sub 0xA401C. Observed with the real sub program:
void psr9000_state::link_main_w(u32 data)
{
	LOGMASKED(LOG_LINK, "main->sub %08x\n", data);
	if(m_msg_left) {
		m_msg.push_back(data);
		if(!--m_msg_left)
			sub_message(m_msg);
		return;
	}
	if(!BIT(data, 31) || data == 0xaa55aa55)
		return; // AA55AA55 = hello after reset, no reply
	m_msg.assign(1, data);
	m_msg_left = (data >> 16) & 0xff;
	if(!m_msg_left)
		sub_message(m_msg);
}

void psr9000_state::sub_message(const std::vector<u32> &msg)
{
	u8 cmd = (msg[0] >> 24) & 0x7f;
	u8 op = (msg[0] >> 8) & 0xff;
	switch(cmd) {
	case 0x01: // hello: sub answers with its protocol/firmware id
		sub_reply(0x81000300);
		break;
	case 0x02: // op 1 = "ready?" -> 83 00 00 xx, xx = 1 when the sound engine is up (sub 0xA4524)
		sub_reply(op == 1 ? 0x83000001 : 0x83000000);
		break;
	case 0x04: // sub 0xA45E0: reply 84 nn 00 00 + nn queued words + 84 00 00 00 end marker
		sub_reply(0x84000000);
		sub_reply(0x84000000);
		break;
	case 0x05: { // version strings, taken from the sub program itself (sub 0xA4742)
		static const u32 addr[5] = { 0xd4a2c, 0xd4a64, 0xd4c6c, 0xd4a6d, 0xd4a79 };
		sub_reply(0x86000000 | (op << 8));
		if(op < 5) {
			u32 a = addr[op], len = 0;
			while(m_subrom[a + len] && len < 0x100)
				len++;
			for(u32 i = 0; i < (len + 4) / 4; i++)
				{ const u8 *s = &m_subrom[a + 4*i]; sub_reply((s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]); }
		}
		break;
	}
	case 0x07: // write tone generator parameter: data = addr(24) | value(8)
	case 0x08: // several at once
		for(size_t i = 1; i < msg.size(); i++)
			m_tgparam[msg[i] >> 8] = msg[i] & 0xff;
		break;
	case 0x09: { // read parameter -> 8A 00 00 value
		auto it = msg.size() > 1 ? m_tgparam.find(msg[1] >> 8) : m_tgparam.end();
		sub_reply(0x8a000000 | (it == m_tgparam.end() ? 0 : it->second));
		break;
	}
	case 0x24: // observed: A4 01 00 00 + 1 word -> A5 01 00 00, FFFFFFFF
		sub_reply(0xa5010000);
		sub_reply(0xffffffff);
		break;
	case 0x3e: case 0x3f: case 0x40: // block transfer (start / data / end), no reply
		break;
	default:
		logerror("unhandled link command %08x (%d data words)\n", msg[0], int(msg.size()) - 1);
		break;
	}
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

void psr9000_state::update_irq0()
{
	m_maincpu->set_input_line(0, (m_fdc_irq || m_scsi_irq) ? ASSERT_LINE : CLEAR_LINE);
}

// SCSI controller (SPC7212, register r at 0xa80000 + 4*r + 3). Not emulated: it behaves like a
// bus with no devices. Starting a command (write to reg 20, see 0x31E0EC) ends in a selection
// timeout: status reg 0 bit4 (error) + reg 1 bit5, as checked at 0x31E042 -> -0x251.
// The interrupt handler (0x31D89C) reads reg 0 and writes it back to acknowledge.
u8 psr9000_state::scsi_r(offs_t offset)
{
	return m_scsi[offset & 31];
}

void psr9000_state::scsi_w(offs_t offset, u8 data)
{
	offset &= 31;
	if(offset == 0) {
		// acknowledge: written bits are cleared
		m_scsi[0] &= ~data;
		if(!(m_scsi[0] & 0x18)) {
			m_scsi_irq = false;
			update_irq0();
		}
		return;
	}
	m_scsi[offset] = data;
	if(offset == 20) // the real selection timeout takes ~250ms; the driver arms its state after this write
		m_scsi_timer->adjust(attotime::from_msec(250));
}

TIMER_CALLBACK_MEMBER(psr9000_state::lcd_pulse)
{
	m_maincpu->mtu_input_capture(0, 2);
}

TIMER_CALLBACK_MEMBER(psr9000_state::scsi_timeout)
{
	m_scsi[0] |= 0x10;
	m_scsi[1] = 0x20;
	m_scsi_irq = true;
	update_irq0();
}

// Quad UART interrupts: 0x9800A3 reads which channels are interrupting (IRQ2 handler 0x1255FA:
// bit0 -> 0x8A3C8, bit1 -> 0x8A3C2, bit3 -> 0x8AE88). All of them drive main IRQ2.
template <int Ch> void psr9000_state::uart_irq_w(int state)
{
	u8 old = m_uart_irq;
	if(state)
		m_uart_irq |= 1 << Ch;
	else
		m_uart_irq &= ~(1 << Ch);
	if(!old && m_uart_irq)
		m_maincpu->set_input_line(2, ASSERT_LINE);
	else if(old && !m_uart_irq)
		m_maincpu->set_input_line(2, CLEAR_LINE);
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
	// 0x9800a0-3: UART interrupt status (read, bits 0-3 = channels) / enable (write, 0x8A3D4)
	map(0x009800a0, 0x009800a3).lrw8(NAME([this](offs_t) { return m_uart_irq; }), NAME([](offs_t, u8) { })).umask32(0x000000ff);
	// 0xa00000: 32-bit data port to/from the sub CPU ("PIF32"), send 0x2538E0, receive 0x253948.
	// Status on port A: bit0 = may send (wait for 1), bit1 = 0 when a word is waiting (0x253884/0x2538B2).
	map(0x00a00000, 0x00a00003).rw(FUNC(psr9000_state::link_main_r), FUNC(psr9000_state::link_main_w));
	// 0xa80000-0xa8007f: SCSI controller, simulated as an empty bus
	map(0x00a80000, 0x00a8007f).rw(FUNC(psr9000_state::scsi_r), FUNC(psr9000_state::scsi_w)).umask32(0x000000ff);
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
	SH7043A(config, m_maincpu, 7_MHz_XTAL * 4); // HD6437043AE00F ("A" die: 2x A/D at 0xffff8400); clock not verified
	m_maincpu->set_addrmap(AS_PROGRAM, &psr9000_state::map);
	m_maincpu->read_porta().set(FUNC(psr9000_state::pa_r));
	m_maincpu->write_porta().set(FUNC(psr9000_state::pa_w));
	m_maincpu->read_porte().set(FUNC(psr9000_state::pe_r));
	m_maincpu->read_portf().set(FUNC(psr9000_state::pf_r));
	m_maincpu->write_porte().set(FUNC(psr9000_state::pe_w));

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
	// Channels: 0/1 MIDI (31250 8N1), 2 TO HOST? (divisor 10), 3 = 31250 8N2, probably the panel CPU.
	m_uart[0]->out_int_callback().set(FUNC(psr9000_state::uart_irq_w<0>));
	m_uart[1]->out_int_callback().set(FUNC(psr9000_state::uart_irq_w<1>));
	m_uart[2]->out_int_callback().set(FUNC(psr9000_state::uart_irq_w<2>));
	m_uart[3]->out_int_callback().set(FUNC(psr9000_state::uart_irq_w<3>));

	HD63266F(config, m_fdc, 16_MHz_XTAL); // clock not verified
	m_fdc->intrq_wr_callback().set(FUNC(psr9000_state::fdc_irq_w));
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

	// Sub CPU program (TG board). Not executed; only its version strings are used.
	ROM_REGION( 0x100000, "subrom", ROMREGION_ERASEFF )
	ROM_LOAD( "p9000s.bin", 0x000000, 0x100000, CRC(e742bf8f) SHA1(c4b66b059f610a7d7a4885304f1fbd5cc8415a18) )

	// Voice parameter mask ROM IC33 (16Mbit): not dumped.

	// Wave mask ROMs IC41/IC42/IC44/IC45 (4x MX23C6410, 32MB): not dumped.

	// Preset style mask ROM IC29 (MX23C1610, 2MB): not in the update files, not dumped yet.
	ROM_REGION16_BE( 0x200000, "styrom", ROMREGION_ERASEFF )
	ROM_LOAD( "xw695100.ic29", 0x000000, 0x200000, NO_DUMP )
ROM_END

} // anonymous namespace

SYST( 1999, psr9000, 0, 0, psr9000, psr9000, psr9000_state, init_psr9000, "Yamaha", "PSR-9000 (V3.06, WIP)", MACHINE_NO_SOUND | MACHINE_NOT_WORKING )
