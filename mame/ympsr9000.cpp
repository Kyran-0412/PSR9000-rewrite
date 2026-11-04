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

#include "bus/midi/midi.h"
#include "cpu/sh/sh7042.h"
#include "cpu/sh/sh_dmac.h"
#include "imagedev/floppy.h"
#include "machine/ins8250.h"
#include "machine/intelfsh.h"
#include "machine/upd765.h"
#include "diserial.h"
#include "video/sed1330.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"

#include "psr9000.lh"

#include <deque>
#include <map>

#define LOG_LINK (1U << 1)
#define VERBOSE (LOG_LINK)
#include "logmacro.h"

namespace {

class psr9000_state : public driver_device, public device_serial_interface
{
public:
	psr9000_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, device_serial_interface(mconfig, *this)
		, m_maincpu(*this, "maincpu")
		, m_lcdc(*this, "lcdc")
		, m_fdc(*this, "fdc")
		, m_dataflash(*this, "data")
		, m_styleflash(*this, "style")
		, m_uart(*this, "uart%u", 0U)
		, m_rom(*this, "maincpu")
		, m_subrom(*this, "subrom")
		, m_styrom(*this, "styrom")
		, m_dial(*this, "DIAL")
		, m_dialbtn(*this, "DIALBTN")
	{ }

	DECLARE_INPUT_CHANGED_MEMBER(panel_button);

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
	required_ioport m_dial;
	required_ioport m_dialbtn;
	int m_dialbtn_hold = 0;

	// Front panel CPU (MN101C027, mask ROM not dumped) simulated: sends button and dial packets
	// to the main CPU over quad UART channel 3 (31250 baud, 8N2).
	std::deque<u8> m_panel_tx;
	u8 m_dial_last = 0;
	emu_timer *m_dial_timer = nullptr;
	TIMER_CALLBACK_MEMBER(dial_poll);
	void panel_send(u8 a, u8 b, u8 c);
	void panel_rx_w(int state) { device_serial_interface::rx_w(state); }
	virtual void tra_callback() override;
	virtual void tra_complete() override;
	virtual void rcv_complete() override;
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
	// Floppy data goes through DMA channel 0 (external request, FDC data register -> RAM);
	// the end of the count is the controller's TC.
	sh_dmac_channel_device *m_fdc_dma = nullptr;
	floppy_connector *m_floppy = nullptr;
	u8 m_fdc_ctrl = 0;   // HD63266F register at +2 (see map)
	bool m_fdc_drq = false;
	void fdc_drq_w(int state) { m_fdc_drq = state; if(m_fdc_dma) m_fdc_dma->dreq_w(state); }
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

	// The preset style mask ROM is not dumped.  Until it is, fill it with empty "No Data"
	// styles, using the same 512-byte header layout the firmware itself writes for empty
	// flash styles, so style loading does not run off into garbage.  Every slot of the 2 MB
	// is filled: the default preset at power-on reads slot 125 (0x60FA10), and an unfilled
	// (FF) header gave the style clock a beat length of 0, so START/STOP hung the firmware.
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
		const int slots = (0x200000 - 0x10) / 0x200;
		std::fill(img.begin(), img.begin() + 0x10 + slots*0x200, 0x00);
		for(int n = 0; n < slots; n++) {
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

	set_data_frame(1, 8, PARITY_NONE, STOP_BITS_2);
	set_rate(31250);
	m_uart[3]->rx_w(1);
	m_floppy = subdevice<floppy_connector>("fdc:0");
	m_fdc_dma = m_maincpu->subdevice<sh_dmac_channel_device>("dmac:0");
	if(m_fdc_dma)
		m_fdc_dma->set_tend_handler([this]() { m_fdc->tc_w(1); m_fdc->tc_w(0); });
	else
		logerror("DMA channel 0 not found: floppy reads will fail\n");
	m_uart[0]->rx_w(1);   // MIDI A/B inputs idle until a -midiin port drives them
	m_uart[1]->rx_w(1);
	m_dial_timer = timer_alloc(FUNC(psr9000_state::dial_poll), this);
	m_dial_timer->adjust(attotime::from_msec(10), 0, attotime::from_msec(10));
	m_lcd_timer->adjust(attotime::from_usec(64), 0, attotime::from_usec(64));
	// NOTE reg 11 low 3 bits = SCSI bus phase (0x31D7DC). The early probe loop (0x31E14E, called
	// from 0x315BE8 before the driver's state 0x12BB1C4 is set by 0x31E978) only ends on phase 1
	// (but then the driver assumes a device and sends INQUIRY) or a completed selection timeout
	// (which the interrupt handler ignores while that state is 0). Still unsolved.
	save_item(NAME(m_pe));
	save_item(NAME(m_last_to_main));
	save_item(NAME(m_uart_irq));
	save_item(NAME(m_fdc_irq));
	save_item(NAME(m_fdc_ctrl));
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
	// Data rate: 500 kbit/s = 1.44 MB HD disks. The real board selects the rate with a pin
	// (not traced yet); at the controller's default 250 kbit/s every READ ID on an HD disk
	// fails (ST1 = missing address mark) and the keyboard shows "Unformatted disk".
	// 720 KB DD disks would need 250 kbit/s.
	m_fdc->rate_w(1);
	m_fdc->ready_w(false);   // false = ready
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

void psr9000_state::panel_send(u8 a, u8 b, u8 c)
{
	bool idle = m_panel_tx.empty() && is_transmit_register_empty();
	m_panel_tx.push_back(a);
	m_panel_tx.push_back(b);
	m_panel_tx.push_back(c);
	if(idle)
		tra_complete();
}

void psr9000_state::tra_callback()
{
	m_uart[3]->rx_w(transmit_register_get_data_bit());
}

void psr9000_state::tra_complete()
{
	if(!m_panel_tx.empty()) {
		transmit_register_setup(m_panel_tx.front());
		m_panel_tx.pop_front();
	}
}

void psr9000_state::rcv_complete()
{
	// main -> panel (LED/lamp data): not used yet
	receive_register_extract();
	logerror("main->panel %02x\n", get_received_char());
}

// Button packet: 80, row, column | 0x40 when pressed (parser 0x8A748)
INPUT_CHANGED_MEMBER(psr9000_state::panel_button)
{
	panel_send(0x80, param >> 4, (param & 15) | (newval ? 0x40 : 0));
}

// Dial packet: D0 = +n, D8 = -n, n = hi * 16 + lo
TIMER_CALLBACK_MEMBER(psr9000_state::dial_poll)
{

	u8 cur = m_dial->read();
	s8 delta = s8(cur - m_dial_last);
	m_dial_last = cur;
	// clickable dial halves: one step on press, then repeat while held
	u8 btn = m_dialbtn->read() & 3;
	if(btn == 1 || btn == 2) {
		if(m_dialbtn_hold == 0 || (m_dialbtn_hold >= 40 && m_dialbtn_hold % 6 == 0))
			delta += btn == 2 ? 1 : -1;
		m_dialbtn_hold++;
	} else
		m_dialbtn_hold = 0;
	if(delta) {
		int n = delta > 0 ? delta : -delta;
		panel_send(delta > 0 ? 0xd0 : 0xd8, (n >> 4) & 7, n & 15);
	}
}

TIMER_CALLBACK_MEMBER(psr9000_state::lcd_pulse)
{
	// Only while PE2 is set to TIOC0C (PECR2 bits 5-4 = 01): the capture handler switches it
	// back to plain I/O after each chunk and the DMA end handler re-enables it.
	if(((m_maincpu->pcf_e() >> 4) & 3) == 1)
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
// IRQ2 is edge-triggered (the handler clears its flag in the ISR at 0xFFFF835A) and serves each
// pending channel once per interrupt, so every channel that starts interrupting must make a new
// edge, even while another channel still holds the line. With a plain OR of the four lines,
// MIDI A and B sending at the same time left the line high for good and MIDI output stopped.
template <int Ch> void psr9000_state::uart_irq_w(int state)
{
	u8 old = m_uart_irq;
	if(state)
		m_uart_irq |= 1 << Ch;
	else
		m_uart_irq &= ~(1 << Ch);
	if(state && !(old & (1 << Ch))) {
		m_maincpu->set_input_line(2, CLEAR_LINE);
		m_maincpu->set_input_line(2, ASSERT_LINE);
	} else if(old && !m_uart_irq)
		m_maincpu->set_input_line(2, CLEAR_LINE);
}

void psr9000_state::palette_init(palette_device &palette)
{
	// Kyran's keyboard: a set pixel is bright pastel yellow, a clear pixel blue
	// (e.g. the top status bar is blue, unselected boxes are yellow with blue text)
	palette.set_pen_color(0, rgb_t(0x36, 0x41, 0xcf));
	palette.set_pen_color(1, rgb_t(0xfb, 0xf3, 0xa8));
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
	// DMA transfers through the data register: on the board the DMA acknowledge makes the
	// controller hand over (disk read) or take (disk write) its data byte; MAME's model only does
	// that through dma_r() / dma_w(). Without the write half every sector written to the floppy
	// came out as zeros.
	map(0x00c00000, 0x00c00003).lrw8(
			NAME([this](offs_t) { return m_fdc_drq ? m_fdc->dma_r() : m_fdc->fifo_r(); }),
			NAME([this](offs_t, u8 data) { if(m_fdc_drq) m_fdc->dma_w(data); else m_fdc->fifo_w(data); })).umask32(0x00ff0000);
	// +2: bit set/reset register (MAME's model has only a read-only status there). Writes:
	// 0xE0 | bits = set, 0xD0 | bits = clear (0xFF at reset). Reads give the bits back in the low
	// nibble (bit 0 = motor on, polled by the floppy task) with the interrupt in bit 6; bit 7 is
	// read as "disk removed" by the floppy task (0x316E70), so it stays 0 while a disk is in.
	// The firmware's controller self-test at boot (0x317638) sets and clears bits 2/3 and reads
	// them back; when it failed, the file system was never marked initialised and a later
	// re-init wiped the "disk ready" flag, so every disk showed as "Unformatted".
	map(0x00c00000, 0x00c00003).lrw8(
			NAME([this](offs_t) {
				floppy_image_device *fl = m_floppy ? m_floppy->get_device() : nullptr;
				return u8((m_fdc->extstat_r() & 0x40) | (m_fdc_ctrl & 0x0f) | ((m_fdc->extstat_r() & 1) ? 1 : 0) | ((fl && fl->exists()) ? 0 : 0x80));
			}),
			NAME([this](offs_t, u8 data) {
				if((data & 0xf0) == 0xe0)
					m_fdc_ctrl |= data & 0x0f;
				else if((data & 0xf0) == 0xd0)
					m_fdc_ctrl &= ~data & 0x0f;
				else if(data == 0xff)
					m_fdc_ctrl = 0;
				floppy_image_device *fl = m_floppy ? m_floppy->get_device() : nullptr;
				if(fl && (m_fdc_ctrl & 1))
					fl->mon_w(0);   // bit 0 = motor on (switching off is left to the controller model)
			})).umask32(0x0000ff00);
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
	// Front panel switch matrix as reported by the panel CPU: row r, column c -> packet 80 r c.
	// Names from the firmware switch test table (0x3AD240); button IDs from 0x337514[r*10+c].
	PORT_START("ROW0")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SONG FILE DIRECTORY 1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x00)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SONG FILE DIRECTORY 2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x01)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SONG FILE DIRECTORY 3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x02)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SONG FILE DIRECTORY 4") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x03)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SONG FILE DIRECTORY 5") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x04)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SONG SETUP") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x05)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SONG PLAYER") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x06)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("AUTO ACCOMPANIMENT") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x07)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("8 BEAT") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x08)
	PORT_START("ROW1")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("16 BEAT") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x10)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("DANCE") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x11)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SWING & JAZZ") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x12)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("R & B") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x13)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("COUNTRY") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x14)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LATIN") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x15)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MARCH & WALTZ") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x16)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("BALLROOM") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x17)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x18)
	PORT_START("ROW2")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x20)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x21)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 4") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x22)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 5") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x23)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 6") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x24)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 7") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x25)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FLASH STYLE 8") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x26)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("DISKDIRECT") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x27)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("INTRO 1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x28)
	PORT_START("ROW3")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("INTRO 2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x30)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("INTRO 3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x31)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("TAP TEMPO") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x32)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("ENDING 1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x33)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("ENDING 2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x34)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("ENDING 3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x35)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FADE IN/OUT") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x36)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SYNC STOP") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x37)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SYNC START") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x38)
	PORT_START("ROW4")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MAIN A") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x40)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MAIN B") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x41)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MAIN C") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x42)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MAIN D") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x43)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FILL DOWN") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x44)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FILL SELF") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x45)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FILL BREAK") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x46)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FILL UP") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x47)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("START/STOP") PORT_CODE(KEYCODE_SPACE) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x48)
	PORT_START("ROW5")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("DISK/SCSI") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x50)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("FUNCTION") PORT_CODE(KEYCODE_X) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x51)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MIDI") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x52)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("KBD TRANSPOSE -") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x53)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("KBD TRANSPOSE +") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x54)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SOUND CREATOR") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x55)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("DIGITAL RECORDING") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x56)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MIXING CONSOLE") PORT_CODE(KEYCODE_Z) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x57)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD A") PORT_CODE(KEYCODE_A) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x58)
	PORT_START("ROW6")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD B") PORT_CODE(KEYCODE_S) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x60)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD C") PORT_CODE(KEYCODE_D) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x61)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD D") PORT_CODE(KEYCODE_F) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x62)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD E") PORT_CODE(KEYCODE_G) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x63)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MULTIPAD STOP") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x64)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MULTIPAD -") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x65)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MULTIPAD +") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x66)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MULTIPAD 1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x67)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MULTIPAD 2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x68)
	PORT_START("ROW7")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MULTIPAD 3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x70)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MULTIPAD 4") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x71)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("DIRECT ACCESS") PORT_CODE(KEYCODE_N) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x74)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART ON/OFF") PORT_CODE(KEYCODE_COMMA) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x75)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MAIN MIXER") PORT_CODE(KEYCODE_M) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x76)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("EXIT") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x77)
	PORT_START("ROW8")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 1 UP") PORT_CODE(KEYCODE_1) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x80)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 2 UP") PORT_CODE(KEYCODE_2) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x81)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 3 UP") PORT_CODE(KEYCODE_3) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x82)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 4 UP") PORT_CODE(KEYCODE_4) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x83)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 5 UP") PORT_CODE(KEYCODE_5) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x84)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 6 UP") PORT_CODE(KEYCODE_6) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x85)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 7 UP") PORT_CODE(KEYCODE_7) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x86)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 8 UP") PORT_CODE(KEYCODE_8) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x87)
	PORT_START("ROW9")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 1 DOWN") PORT_CODE(KEYCODE_Q) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x90)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 2 DOWN") PORT_CODE(KEYCODE_W) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x91)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 3 DOWN") PORT_CODE(KEYCODE_E) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x92)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 4 DOWN") PORT_CODE(KEYCODE_R) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x93)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 5 DOWN") PORT_CODE(KEYCODE_T) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x94)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 6 DOWN") PORT_CODE(KEYCODE_Y) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x95)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 7 DOWN") PORT_CODE(KEYCODE_U) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x96)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD 8 DOWN") PORT_CODE(KEYCODE_I) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x97)
	PORT_START("ROW10")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("TOUCH") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa0)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("DSP(4-7)") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa1)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("HARMONY/ECHO") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa2)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SUSTAIN") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa3)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("SLOW/FAST") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa4)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("POLY/MONO") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa5)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE PIANO") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa6)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE E.PIANO") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa7)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE ORGAN") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xa8)
	PORT_START("ROW11")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE ACCORDION") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb0)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE GUITAR") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb1)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE STRINGS") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb2)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE TRUMPET") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb3)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE BRASS") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb4)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE SAXOPHONE") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb5)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE FLUTE") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb6)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE CHOIR & PAD") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb7)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE SYNTHESIZER") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xb8)
	PORT_START("ROW12")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE PERCUSSION") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc0)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE XG") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc1)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE ORGAN FLUTES") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc2)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("P.VOICE CUSTOM VOICE") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc3)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART SELECT LEFT") PORT_CODE(KEYCODE_0_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc4)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART SELECT RIGHT1") PORT_CODE(KEYCODE_1_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc5)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART SELECT RIGHT2") PORT_CODE(KEYCODE_2_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc6)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART SELECT RIGHT3") PORT_CODE(KEYCODE_3_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc7)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LEFT HOLD") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xc8)
	PORT_START("ROW13")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART ON/OFF LEFT") PORT_CODE(KEYCODE_4_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd0)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART ON/OFF RIGHT1") PORT_CODE(KEYCODE_5_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd1)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART ON/OFF RIGHT2") PORT_CODE(KEYCODE_6_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd2)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PART ON/OFF RIGHT3") PORT_CODE(KEYCODE_7_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd3)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("VOCAL HARMONY TALK") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd4)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("VOCAL HARMONY DSP(8)") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd5)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("VOCAL HARMONY (9)") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd6)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("R.MEMORY FREEZE") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd7)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("DEMO") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xd8)
	PORT_START("ROW14")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD F") PORT_CODE(KEYCODE_H) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe0)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD G") PORT_CODE(KEYCODE_J) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe1)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD H") PORT_CODE(KEYCODE_K) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe2)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD I") PORT_CODE(KEYCODE_L) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe3)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("LCD J") PORT_CODE(KEYCODE_COLON) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe4)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PAGE CONTROL BACK") PORT_CODE(KEYCODE_OPENBRACE) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe5)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("PAGE CONTROL NEXT") PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe6)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MUSIC DATABASE") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe7)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("ONE TOUCH SETTING 1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xe8)
	PORT_START("ROW15")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("ONE TOUCH SETTING 2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf0)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("ONE TOUCH SETTING 3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf1)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("ONE TOUCH SETTING 4") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf2)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("UPPER OCTAVE -") PORT_CODE(KEYCODE_MINUS_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf3)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("UPPER OCTAVE +") PORT_CODE(KEYCODE_PLUS_PAD) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf4)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("V.HARMONY SELECT") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf5)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("V.HARMONY MIC SETUP") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf6)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST BANK -") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf7)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST BANK +") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0xf8)
	PORT_START("ROW16")
	PORT_BIT(0x001, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x100)
	PORT_BIT(0x002, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x101)
	PORT_BIT(0x004, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x102)
	PORT_BIT(0x008, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 4") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x103)
	PORT_BIT(0x010, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 5") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x104)
	PORT_BIT(0x020, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 6") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x105)
	PORT_BIT(0x040, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 7") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x106)
	PORT_BIT(0x080, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("REGIST 8") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x107)
	PORT_BIT(0x100, IP_ACTIVE_HIGH, IPT_KEYPAD) PORT_NAME("MEMORY") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(psr9000_state::panel_button), 0x108)

	PORT_START("DIALBTN")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_NAME("Data Dial -") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_NAME("Data Dial +") PORT_CODE(KEYCODE_UP)

	PORT_START("DIAL")
	PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_NAME("Data Dial") PORT_SENSITIVITY(25) PORT_KEYDELTA(1) PORT_CODE_DEC(KEYCODE_LEFT) PORT_CODE_INC(KEYCODE_RIGHT)
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
	config.set_default_layout(layout_psr9000);

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
	m_uart[3]->out_tx_callback().set(FUNC(psr9000_state::panel_rx_w));

	// MIDI A / B (UART channels 0 / 1). With MAME's -midiout / -midiin options these connect to
	// the PC's MIDI ports (e.g. "Microsoft GS Wavetable Synth", or loopMIDI -> a VST host).
	// The firmware's MIDI transmit settings (Function > MIDI, template "All Parts") decide
	// what is sent: keyboard parts, style parts, song.
	MIDI_PORT(config, "mdin_a", midiin_slot, "midiin").rxd_handler().set(m_uart[0], FUNC(ns16550_device::rx_w));
	MIDI_PORT(config, "mdin_b", midiin_slot, "midiin").rxd_handler().set(m_uart[1], FUNC(ns16550_device::rx_w));
	MIDI_PORT(config, "mdout_a", midiout_slot, "midiout");
	MIDI_PORT(config, "mdout_b", midiout_slot, "midiout");
	m_uart[0]->out_tx_callback().set("mdout_a", FUNC(midi_port_device::write_txd));
	m_uart[1]->out_tx_callback().set("mdout_b", FUNC(midi_port_device::write_txd));

	HD63266F(config, m_fdc, 16_MHz_XTAL); // clock not verified
	m_fdc->intrq_wr_callback().set(FUNC(psr9000_state::fdc_irq_w));
	// With the drive's own ready signal, the boot-time RECALIBRATE runs while the motor is still
	// spinning up, the drive reports "not ready" (ST0 0x68) and the firmware treats the floppy
	// drive as absent (empty DEVICE list, no disk detection). The drive is reported ready all
	// the time, like the real one; whether a disk is in comes from bit 7 of register +2.
	// (Needs mame/patches/upd765_hd63266f_nodisk.diff: no motor spin-up wait without a disk.)
	m_fdc->set_ready_line_connected(false);   // drive always ready (see below)
	m_fdc->drq_wr_callback().set(FUNC(psr9000_state::fdc_drq_w));
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
