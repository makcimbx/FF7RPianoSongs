"""Disk-only regression for the MSVC x64 initialize_proxy_inline callback.

Usage: --object main.obj --image FF7RPianoSongsProxy.dll; --selftest uses bytes only.
Regular AMD64 COFF COMDAT only (not LTCG/bigobj). This intentionally recognizes a
small straight-line compiler pattern, not arbitrary x64 instructions. Unsupported
code/relocations/unwind forms fail closed. It never loads or executes the image.

Format authority: learn.microsoft.com/en-us/windows/win32/debug/pe-format and
learn.microsoft.com/en-us/cpp/build/exception-handling-x64.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def span(data, offset, size):
    require(0 <= offset <= len(data) and 0 <= size <= len(data) - offset, 'Truncated/out-of-range binary field')
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, span(data, offset, struct.calcsize(fmt)))


def callback_object(data):
    machine, count, _, table, symbols, optional, _ = unpack('<HHIIIHH', data, 0)
    require(machine == 0x8664 and optional == 0 and 0 < count <= 16384 and 0 < symbols <= 1000000,
            'Expected regular AMD64 COFF; LTCG/bigobj/import objects unsupported')
    sections = [unpack('<8sIIIIIIHHI', data, 20 + i * 40) for i in range(count)]
    span(data, table, symbols * 18)
    strings_at = table + symbols * 18
    strings = span(data, strings_at, unpack('<I', data, strings_at)[0])
    require(len(strings) >= 4, 'Invalid COFF string table')
    records = {}
    i = 0
    while i < symbols:
        raw = span(data, table + i * 18, 18)
        if raw[:4] == bytes(4):
            offset = unpack('<I', raw, 4)[0]
            require(4 <= offset < len(strings), 'Bad symbol string offset')
            end = strings.find(b'\0', offset)
            require(end >= offset, 'Unterminated symbol')
            name = strings[offset:end].decode('ascii')
        else:
            name = raw[:8].rstrip(b'\0').decode('ascii')
        value, section, kind, storage, aux = unpack('<IhHBB', raw, 8)
        require(i + aux < symbols, 'Bad auxiliary symbol count')
        records[i] = (name, value, section, kind, storage, span(data, table + (i + 1) * 18, aux * 18))
        i += 1 + aux
    # MSVC also emits $pdata$/$unwind$ symbols containing the callback's name.
    candidates = [r for r in records.values() if re.fullmatch(r'\?initialize_proxy_inline@.+@@YAXXZ', r[0])]
    require(len(candidates) == 1, 'Missing/ambiguous initialize_proxy_inline symbol')
    name, value, section, kind, storage, _ = candidates[0]
    require(re.fullmatch(r'\?initialize_proxy_inline@.+@@YAXXZ', name) and kind == 0x20 and storage in (2, 3) and
            value == 0 and 1 <= section <= count, 'Unsupported callback symbol/extent')
    header = sections[section - 1]
    require(header[0] == b'.text$mn' and header[9] & 0x20001020 == 0x20001020 and not header[9] & 0x81000000,
            'Expected dedicated executable COMDAT without relocation overflow')
    require(sum(r[2] == section and r[3] == 0x20 for r in records.values()) == 1, 'Shared callback section unsupported')
    definitions = [r for r in records.values() if r[2] == section and r[3] == 0 and r[4] == 3 and len(r[5]) == 18]
    require(len(definitions) == 1, 'Missing/ambiguous COFF section definition')
    length = unpack('<I', definitions[0][5], 0)[0]
    require(0 < length <= header[3] and length <= 4096, 'Invalid logical callback section size')
    code = span(data, header[4], length)  # Raw file padding is not function code.
    relocations = {}
    for i in range(header[7]):
        offset, symbol, relocation = unpack('<IIH', data, header[5] + i * 10)
        # Documented IMAGE_REL_AMD64_REL32 / REL32_1..5; no arbitrary wildcard bytes.
        require(4 <= relocation <= 9 and offset + 4 <= length and symbol in records,
                'Unsupported/out-of-range AMD64 code relocation')
        require(all(abs(offset - old) >= 4 for old in relocations), 'Overlapping code relocations')
        relocations[offset] = (relocation, records[symbol][0])
    return name, code, relocations, header[3] - length


def instructions(code, relocations):
    """Decode only the small existing MSVC callback family, including the old tail jump."""
    i, stack, allocation, allocation_end = 0, 0, 0, 0
    used, calls, stores = set(), [], []
    terminal = None
    def reference(offset, kind):
        require(offset in relocations and relocations[offset][0] == kind, 'Instruction/relocation boundary mismatch')
        used.add(offset)
        return relocations[offset][1]
    while i < len(code):
        tail = code[i:]
        if tail.startswith(b'\x66\x90'):
            i += 2
        elif tail.startswith(b'\x90'):
            i += 1
        elif tail[:3] in (b'\x48\x83\xec', b'\x48\x83\xc4'):
            amount = span(code, i + 3, 1)[0]
            require(0 < amount <= 127 and amount % 8 == 0, 'Unsupported stack adjustment')
            if tail[2] == 0xec:
                require(not allocation and stack == 0, 'Unsupported callback stack frame')
                stack = allocation = amount
                allocation_end = i + 4
            else:
                require(stack == amount, 'Unbalanced callback epilogue')
                stack = 0
            i += 4
        elif tail[:3] in (b'\x48\x8d\x0d', b'\x48\x8d\x15', b'\x4c\x8d\x05'):
            reference(i + 3, 4)
            i += 7
        elif tail.startswith(b'\x45\x33\xc0'):
            i += 3
        elif tail.startswith(b'\xc6\x05'):
            name = reference(i + 2, 5)  # REL32_1 accounts for the following immediate.
            require(span(code, i + 6, 1) == b'\x01', 'Expected atomic boolean publication')
            stores.append((i, name))
            i += 7
        elif tail[:1] in (b'\xe8', b'\xe9'):
            name = reference(i + 1, 4)
            if tail[0] == 0xe8:
                require(stack >= 32 and (8 - stack) % 16 == 0, 'CALL without aligned shadow space')
                calls.append((i, name))
                i += 5
            else:
                i += 5
                terminal = 'tail_jump'
                break
        elif tail[:1] == b'\xc3':
            require(stack == 0, 'Unbalanced callback return')
            i += 1
            terminal = 'return'
            break
        else:
            raise ValueError(f'Unsupported callback instruction at +0x{i:x}')
    require(terminal and all(byte in (0, 0x90, 0xcc) for byte in code[i:]), 'Invalid terminal instruction/section padding')
    require(used == set(relocations) and all(offset + 4 <= i for offset in used), 'Relocation outside decoded function')
    return i, terminal, calls, stores, allocation, allocation_end


class Image:
    def __init__(self, data):
        self.data = data
        require(span(data, 0, 2) == b'MZ', 'Missing DOS signature')
        pe = unpack('<I', data, 60)[0]
        require(span(data, pe, 4) == b'PE\0\0', 'Missing PE signature')
        machine, count, _, _, _, optional, flags = unpack('<HHIIIHH', data, pe + 4)
        require(machine == 0x8664 and flags & 0x2000 and 0 < count <= 96 and 144 <= optional <= 4096, 'Expected x64 PE DLL')
        opt = pe + 24
        require(unpack('<H', data, opt)[0] == 0x20b and unpack('<I', data, opt + 108)[0] >= 4, 'Missing PE32+ directories')
        span(data, opt, optional)
        self.pdata, self.pdata_size = unpack('<II', data, opt + 136)
        self.sections = [unpack('<8sIIIIIIHHI', data, opt + optional + i * 40) for i in range(count)]
        ranges = sorted((s[2], s[2] + s[1]) for s in self.sections if s[1])
        require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), 'Overlapping image sections')

    def read(self, rva, size, executable=False):
        matches = [s for s in self.sections if s[2] <= rva and rva + size <= s[2] + min(s[1], s[3])]
        require(len(matches) == 1, 'RVA is not uniquely file-backed within virtual section')
        section = matches[0]
        if executable:
            require(section[9] & 0x20000020 == 0x20000020 and not section[9] & 0x80000000, 'Expected nonwritable executable code')
        return span(self.data, section[4] + rva - section[2], size)

    def runtime_function(self, rva, length):
        require(self.pdata and 0 < self.pdata_size <= 4 * 1024 * 1024 and self.pdata_size % 12 == 0, 'Invalid exception directory')
        records = list(struct.iter_unpack('<III', self.read(self.pdata, self.pdata_size)))
        previous = 0
        for begin, end, _ in records:
            require(previous <= begin < end, 'Invalid/overlapping runtime-function table')
            self.read(begin, end - begin, True)
            previous = end
        covering = [r for r in records if r[0] <= rva < r[1]]
        require(len(covering) == 1, f'Callback RVA 0x{rva:x} has no unique .pdata coverage')
        begin, end, unwind = covering[0]
        require(begin == rva and rva + length <= end, 'Runtime function does not cover complete callback instruction extent')
        require(unwind and unwind % 4 == 0, 'Invalid unwind-info alignment')
        version_flags, prolog, count, frame = self.read(unwind, 4)
        require(version_flags == 1 and frame == 0 and 0 < count <= 32 and prolog <= length,
                'Unsupported/invalid callback unwind version/flags/frame/prologue')
        codes = self.read(unwind + 4, ((count + 1) & ~1) * 2)
        i, allocations = 0, []
        while i < count:
            at, operation = codes[i * 2:i * 2 + 2]
            op, info = operation & 15, operation >> 4
            require(at <= prolog and op in (1, 2), 'Unsupported callback unwind operation')
            extra = 0 if op == 2 else (1 if info == 0 else 2)
            require(op == 2 or info <= 1, 'Invalid large stack allocation')
            require(i + extra < count, 'Truncated unwind allocation')
            amount = info * 8 + 8 if op == 2 else (unpack('<H', codes, (i + 1) * 2)[0] * 8 if info == 0 else unpack('<I', codes, (i + 1) * 2)[0])
            allocations.append((at, amount))
            i += 1 + extra
        return (begin, end, unwind), allocations


def check_callback(obj, image, report=None):
    report = {} if report is None else report
    name, raw, relocations, file_padding = callback_object(obj)
    length, terminal, calls, stores, allocation, allocation_end = instructions(raw, relocations)
    code = raw[:length]
    report.update(symbol=name, code_bytes=length, coff_file_padding_bytes=file_padding,
                  coff_terminal_padding_bytes=len(raw) - length,
                  masked_relocations=[{'offset': o, 'type': r[0], 'symbol': r[1]} for o, r in sorted(relocations.items())])
    pe = Image(image)
    mask = [True] * length
    for offset in relocations:
        mask[offset:offset + 4] = [False] * 4
    runs = list(re.finditer(b'\x01+', bytes(mask)))
    require(runs, 'No exact callback bytes')
    anchor = max(runs, key=lambda r: r.end() - r.start())
    candidates = []
    for section in pe.sections:
        if section[9] & 0x20000020 != 0x20000020 or section[9] & 0x80000000:
            continue
        blob = span(image, section[4], min(section[1], section[3]))
        at = blob.find(code[anchor.start():anchor.end()])
        while at >= 0:
            start = at - anchor.start()
            if 0 <= start and start + length <= len(blob) and all(not exact or blob[start + j] == code[j] for j, exact in enumerate(mask)):
                candidates.append(section[2] + start)
            at = blob.find(code[anchor.start():anchor.end()], at + 1)
    require(len(candidates) == 1, f'Missing/ambiguous linked callback match ({len(candidates)})')
    rva = candidates[0]
    report['callback_rva'] = f'0x{rva:x}'
    runtime, allocations = pe.runtime_function(rva, length)  # The old leaf must fail here, not be excused as leaf-valid.
    report['runtime_function'] = dict(zip(('begin_rva', 'end_rva', 'unwind_rva'), (f'0x{x:x}' for x in runtime)))
    require(terminal == 'return' and len(calls) == 1 and allocations == [(allocation_end, allocation)], 'Callback is not the supported unwindable non-tail call/return pattern')
    call, callee = calls[0]
    require('run_startup_once@' in callee or callee.startswith('?initialize_inline@'), 'Unexpected callback callee')
    post = [(at, symbol) for at, symbol in stores if at > call and symbol.startswith('?g_proxy_startup_returned@')]
    require(len(post) == 1, 'Missing post-call startup-returned bookkeeping')
    linked = pe.read(rva, length, True)
    target = rva + call + 5 + unpack('<i', linked, call + 1)[0]
    pe.read(target, 1, True)
    offset = post[0][0]
    flag = rva + offset + 7 + unpack('<i', linked, offset + 2)[0]
    # The boolean may be in the .data virtual zero-fill tail (not file-backed).
    require(any(s[2] <= flag < s[2] + s[1] and s[9] & 0x80000000 and not s[9] & 0x20000000 for s in pe.sections),
            'Post-call bookkeeping does not address writable non-code image data')
    report.update(status='passed', call_rva=f'0x{rva + call:x}', call_return_rva=f'0x{rva + call + 5:x}',
                  post_call_bookkeeping_rva=f'0x{rva + offset:x}', scope='Static callback frame regression only; not runtime/bootstrap qualification')
    return report


def read_artifact(path):
    path = Path(path)
    with path.open('rb') as stream:
        before = stream.fileno()
        import os
        first = os.fstat(before)
        require(0 < first.st_size <= 128 * 1024 * 1024, 'Artifact exceeds bound')
        raw = stream.read(first.st_size + 1)
        last = os.fstat(before)
    require(len(raw) == first.st_size and (first.st_size, first.st_mtime_ns, first.st_ino) ==
            (last.st_size, last.st_mtime_ns, last.st_ino), 'Artifact changed while reading')
    return raw, {'path': str(path.resolve()), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--object', type=Path)
    parser.add_argument('--image', type=Path)
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args(argv)
    if args.selftest:
        require(args.object is None and args.image is None, 'Selftest does not take artifacts')
        selftest()
        return 0
    if args.object is None or args.image is None:
        parser.error('--object and --image are required')
    report = {'status': 'failed'}
    try:
        obj, report['object'] = read_artifact(args.object)
        image, report['image'] = read_artifact(args.image)
        check_callback(obj, image, report)
    except (OSError, ValueError, struct.error) as error:
        report.update(status='failed', error=str(error))
    print(json.dumps(report, indent=2))
    return 0 if report['status'] == 'passed' else 1


def selftest():
    # Synthetic COFF includes both logical terminal padding and unrelated raw padding.
    code = bytes.fromhex('4883ec28c6050000000001e800000000c60500000000014883c428c3cccccccc')
    names = ['.text$mn', '?initialize_proxy_inline@?A0x123@@YAXXZ', '?run_startup_once@core@@YAXXZ',
             '?g_proxy_startup_returned@?A0x123@@3_NA', '?g_proxy_bootstrap@?A0x123@@3_NA']
    strings = bytearray(bytes(4))
    def symbol(name, section=0, kind=0, storage=2, aux=0):
        if len(name) <= 8:
            field = name.encode().ljust(8, b'\0')
        else:
            field = struct.pack('<II', 0, len(strings)); strings.extend(name.encode() + b'\0')
        return field + struct.pack('<IhHBB', 0, section, kind, storage, aux)
    raw = code.ljust(48, b'\0')
    records = symbol(names[0], 1, storage=3, aux=1) + struct.pack('<I', len(code)) + bytes(14)
    records += symbol(names[1], 1, 0x20, 3) + symbol(names[2]) + symbol(names[3], 2) + symbol(names[4], 2)
    records += symbol('$pdata$' + names[1], 2, storage=3) + symbol('$unwind$' + names[1], 2, storage=3)
    struct.pack_into('<I', strings, 0, len(strings))
    reloc = b''.join(struct.pack('<IIH', *r) for r in ((6, 5, 5), (12, 3, 4), (18, 4, 5)))
    data_at = 100
    table_at = data_at + len(raw) + len(reloc) + 1
    obj = struct.pack('<HHIIIHH', 0x8664, 2, 0, table_at, 8, 0, 0)
    obj += struct.pack('<8sIIIIIIHHI', b'.text$mn', 0, 0, len(raw), data_at, data_at + len(raw), 0, 3, 0, 0x60501020)
    obj += struct.pack('<8sIIIIIIHHI', b'.data', 0, 0, 1, table_at - 1, 0, 0, 0, 0, 0xc0000040)
    obj += raw + reloc + b'\0' + records + strings
    image = bytearray(2048)
    image[:2] = b'MZ'; struct.pack_into('<I', image, 60, 128)
    image[128:132] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', image, 132, 0x8664, 3, 0, 0, 0, 240, 0x2022)
    struct.pack_into('<H', image, 152, 0x20b); struct.pack_into('<I', image, 260, 16)
    struct.pack_into('<II', image, 288, 0x2000, 12)
    for i, (name, rva, at, flags) in enumerate(((b'.text', 0x1000, 512, 0x60000020),
                                               (b'.pdata', 0x2000, 1024, 0x40000040), (b'.data', 0x3000, 1536, 0xc0000040))):
        struct.pack_into('<8sIIIIIIHHI', image, 392 + i * 40, name, 256 if i == 0 else 512, rva, 512, at, 0, 0, 0, 0, flags)
    linked = bytearray(code[:28])
    for offset, target, addend in ((6, 0x3000, 1), (12, 0x10c0, 0), (18, 0x3001, 1)):
        struct.pack_into('<i', linked, offset, target - (0x1020 + offset + 4 + addend))
    image[544:544 + len(linked)] = linked
    image[704] = 0xc3
    struct.pack_into('<III', image, 1024, 0x1020, 0x103c, 0x2100)
    image[1280:1288] = bytes.fromhex('0104010004420000')
    result = check_callback(obj, bytes(image))
    require(result['callback_rva'] == '0x1020' and result['coff_file_padding_bytes'] == 16 and
            result['coff_terminal_padding_bytes'] == 4, 'Synthetic callback/padding mismatch')
    zero_fill = bytearray(image)
    struct.pack_into('<I', zero_fill, 392 + 2 * 40 + 16, 1)
    check_callback(obj, zero_fill)  # Post-call flag is legitimate virtual zero-fill.

    # Historical leaf shape captured from the first artifact, not an executed fixture.
    tail = bytes.fromhex('669090488d15000000004533c0c6050000000001488d0d00000000e900000000')
    tail_obj = bytearray(obj)
    tail_obj[data_at:data_at + len(raw)] = tail.ljust(len(raw), b'\0')
    struct.pack_into('<I', tail_obj, table_at + 18, len(tail))
    struct.pack_into('<H', tail_obj, 20 + 32, 4)
    tail_reloc = b''.join(struct.pack('<IIH', *r) for r in ((6, 3, 4), (15, 5, 5), (23, 3, 4), (28, 3, 4)))
    # Grow only this synthetic object, updating its symbol-table pointer.
    tail_obj[data_at + len(raw):data_at + len(raw) + len(reloc)] = tail_reloc
    struct.pack_into('<I', tail_obj, 8, table_at + 10)
    tail_image = bytearray(image)
    tail_image[544:544 + len(tail)] = tail
    struct.pack_into('<III', tail_image, 1024, 0x10c0, 0x10c1, 0x2100)
    red = {}
    try:
        check_callback(tail_obj, tail_image, red)
    except ValueError as error:
        require('no unique .pdata coverage' in str(error) and red['callback_rva'] == '0x1020',
                'Historical tail shape did not fail at missing unwind coverage')
    else:
        raise AssertionError('Historical leaf without pdata accepted')
    failures = 1  # Historical tail shape rejected specifically for absent unwind coverage.
    for label, change in (
            ('missing pdata', lambda b: struct.pack_into('<III', b, 1024, 0x10c0, 0x10c1, 0x2100)),
            ('short coverage', lambda b: struct.pack_into('<I', b, 1028, 0x1030)),
            ('invalid unwind', lambda b: b.__setitem__(1280, 0)),
            ('wrong allocation', lambda b: b.__setitem__(1285, 0x32)),
            ('ambiguous', lambda b: b.__setitem__(slice(640, 640 + len(linked)), linked)),
            ('changed opcode', lambda b: b.__setitem__(544, 0x90)),
            ('call outside image', lambda b: struct.pack_into('<i', b, 544 + 12, 0x70000000))):
        broken = bytearray(image); change(broken)
        try:
            check_callback(obj, broken)
        except ValueError:
            failures += 1
        else:
            raise AssertionError(f'Accepted {label}')
    for label, broken in (('truncated', obj[:25]), ('LTO', bytes(20)),
                          ('unsupported relocation', obj[:data_at + len(raw) + 8] + b'\x01\0' + obj[data_at + len(raw) + 10:])):
        try:
            check_callback(broken, image)
        except ValueError:
            failures += 1
        else:
            raise AssertionError(f'Accepted {label}')
    print(f'Callback checker selftest passed: valid code/unwind/padding and {failures} rejection cases')


if __name__ == '__main__':
    sys.exit(main())
