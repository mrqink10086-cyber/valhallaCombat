"""Verify custom callsite offsets against a supplied unpacked executable and Address Library."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64


def mappings(path):
    data = path.read_bytes()
    at = 0
    def read(fmt):
        nonlocal at
        value = struct.unpack_from('<' + fmt, data, at)
        at += struct.calcsize('<' + fmt)
        return value[0] if len(value) == 1 else value
    format = read('I')
    if format not in (1, 2):
        raise ValueError('This verifier supports sparse SE/AE Address Library formats 1 and 2')
    version = read('4I')
    name_size = read('I')
    at += name_size
    pointer_size, count = read('2I')
    result = {}
    previous_id = previous_offset = 0
    def decode(code, previous):
        if code == 0: return read('Q')
        if code == 1: return previous + 1
        if code == 2: return previous + read('B')
        if code == 3: return previous - read('B')
        if code == 4: return previous + read('H')
        if code == 5: return previous - read('H')
        if code == 6: return read('H')
        if code == 7: return read('I')
        raise ValueError('Invalid Address Library entry')
    for _ in range(count):
        kind = read('B')
        id = decode(kind & 15, previous_id)
        high = kind >> 4
        offset = decode(high & 7, previous_offset // pointer_size if high & 8 else previous_offset)
        if high & 8: offset *= pointer_size
        result[id] = offset
        previous_id, previous_offset = id, offset
    assert at == len(data)
    return version, result


def verify(executable, library):
    version, addresses = mappings(library)
    ae = version >= (1, 6, 0, 0)
    data = executable.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    count, optional_size = struct.unpack_from('<H12xH', data, pe + 6)
    base = struct.unpack_from('<Q', data, pe + 24 + 24)[0]
    sections = []
    for index in range(count):
        at = pe + 24 + optional_size + index * 40
        _, rva, size, raw = struct.unpack_from('<IIII', data, at + 8)
        sections.append((rva, size, raw))
    def offset(rva):
        for start, size, raw in sections:
            if start <= rva < start + size: return raw + rva - start
        raise ValueError(hex(rva))
    sites = [('stamina', (37650, 38603), (0x16E, 0x171)),
             ('restore', (37510, 38452), (0x176, 0xE1)),
             ('melee_hit', (37673, 38627), (0x3C0, 0x4A8)),
             ('melee_collision', (37650, 38603), (0x38B, 0x45A)),
             ('attack_action', (48139, 49170), (0x4D7, 0x435))]
    if not ae: sites.append(('want_block', (37376, 0), (0x23, 0)))
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    results = []
    for name, ids, offsets in sites:
        id, delta = ids[ae], offsets[ae]
        rva = addresses[id] + delta
        raw = offset(rva)
        instruction = next(decoder.disasm(data[raw:raw + 15], base + rva))
        valid = data[raw] == 0xE8 and instruction.mnemonic == 'call' and instruction.size == 5
        results.append(dict(name=name, id=id, function_rva=hex(addresses[id]), delta=hex(delta),
                            address=hex(base + rva), bytes=data[raw:raw + 5].hex(),
                            instruction=instruction.mnemonic + ' ' + instruction.op_str, valid=valid))
    return dict(version=list(version), executable_sha256=hashlib.sha256(data).hexdigest(),
                library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(), callsites=results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('library', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = verify(args.executable, args.library)
    encoded = json.dumps(result, indent=2)
    if args.output: args.output.write_text(encoded + '\n', encoding='utf-8')
    print(encoded)
    raise SystemExit(0 if all(site['valid'] for site in result['callsites']) else 1)
