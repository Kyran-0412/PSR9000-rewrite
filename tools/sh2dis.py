"""Small SH-2 helpers on top of capstone for main_prog.bin analysis."""
import struct, capstone

def load(path):
    return open(path, 'rb').read()

def lit(img, ins):
    """Resolve a PC-relative mov.l/mov.w literal (capstone prints its address).
    Returns (value, literal_addr, dest_reg) or None."""
    if ins.mnemonic not in ('mov.l', 'mov.w') or not ins.op_str.startswith('0x'):
        return None
    a_s, reg = ins.op_str.split(',')
    a = int(a_s, 16)
    if ins.mnemonic == 'mov.l':
        return struct.unpack('>I', img[a:a+4])[0], a, reg.strip()
    return struct.unpack('>h', img[a:a+2])[0], a, reg.strip()

def dis(img, start, end):
    """Linear sweep; skips 2 bytes whenever capstone hits an invalid opcode."""
    md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_SH2)
    out, a = [], start
    while a < end:
        n = 0
        for ins in md.disasm(img[a:end], a):
            out.append(ins); n += 1
            a = ins.address + ins.size
        if n == 0 or a < end:
            a += 2 if n == 0 else 0
            if n == 0:
                continue
    return out
