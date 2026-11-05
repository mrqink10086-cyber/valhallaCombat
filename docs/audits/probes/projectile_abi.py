"""Read MSVC RTTI and disassemble projectile slot 0xBE without loading Skyrim."""
import argparse
import struct
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
args = parser.parse_args()
data = args.executable.read_bytes()
pe = struct.unpack_from('<I', data, 0x3C)[0]
count, optional_size = struct.unpack_from('<H12xH', data, pe + 6)
optional = pe + 24
base = struct.unpack_from('<Q', data, optional + 24)[0]
sections = []
for i in range(count):
    at = optional + optional_size + i * 40
    virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, at + 8)
    sections.append((rva, raw, raw_size, virtual_size))

def rva_to_offset(rva):
    for start, raw, size, _ in sections:
        if start <= rva < start + size:
            return raw + rva - start
    raise ValueError(hex(rva))

def offset_to_rva(offset):
    for start, raw, size, _ in sections:
        if raw <= offset < raw + size:
            return start + offset - raw
    raise ValueError(hex(offset))

md = Cs(CS_ARCH_X86, CS_MODE_64)
seen = set()
for name in ('Projectile', 'ArrowProjectile', 'MissileProjectile'):
    td = data.find(f'.?AV{name}@@'.encode()) - 16
    assert td >= 0, name
    td_rva = offset_to_rva(td)
    needle = struct.pack('<I', td_rva)
    at = 0
    while (at := data.find(needle, at + 1)) >= 0:
        col = at - 12
        sig, obj_offset, _, _, _, self_rva = struct.unpack_from('<IIIIII', data, col)
        if sig != 1 or obj_offset != 0 or self_rva != offset_to_rva(col):
            continue
        ref = data.find(struct.pack('<Q', base + self_rva))
        if ref < 0:
            continue
        table = ref + 8
        address = struct.unpack_from('<Q', data, table + 0xBE * 8)[0]
        print(f'{name} vtable={base + offset_to_rva(table):#x} slotBE={address:#x}')
        if address in seen:
            break
        seen.add(address)
        code = rva_to_offset(address - base)
        for instruction in md.disasm(data[code:code + 4096], address):
            print(f'{instruction.address:016X}  {instruction.mnemonic:8} {instruction.op_str}')
            if instruction.mnemonic == 'ret':
                break
        break
