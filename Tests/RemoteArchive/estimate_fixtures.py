"""Deterministic stored RAR and ISO/Joliet fixtures, with no external tools."""
import struct
import zlib


def write_rar4(path, entries, solid=False):
    def header(kind, flags, body):
        raw = struct.pack('<BHH', kind, flags, 7 + len(body)) + body
        return struct.pack('<H', zlib.crc32(raw) & 0xffff) + raw

    with path.open('wb') as out:
        out.write(b'Rar!\x1a\x07\x00')
        out.write(header(0x73, 8 if solid else 0, bytes(6)))
        for i, (name, data) in enumerate(entries):
            name = name.encode('ascii')
            body = struct.pack('<IIBIIBBHI', len(data), len(data), 2,
                               zlib.crc32(data), 0, 29, 0x30, len(name), 0x20) + name
            out.write(header(0x74, 0x8000 | (0x10 if solid and i else 0), body))
            out.write(data)
        out.write(header(0x7b, 0, b''))


def vint(value):
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def write_rar5(path, entries, solid=False):
    def header(body):
        raw = vint(len(body)) + body
        return struct.pack('<I', zlib.crc32(raw)) + raw

    with path.open('wb') as out:
        out.write(b'Rar!\x1a\x07\x01\x00')
        out.write(header(b'\x01\x00' + vint(4 if solid else 0)))
        for i, (name, data) in enumerate(entries):
            name = name.encode('ascii')
            body = (b'\x02\x02' + vint(len(data)) + b'\x04' + vint(len(data))
                    + vint(0x20) + struct.pack('<I', zlib.crc32(data))
                    + vint(0x40 if solid and i else 0) + b'\x00' + vint(len(name)) + name)
            out.write(header(body))
            out.write(data)
        out.write(header(b'\x05\x00\x00'))


def write_iso(path, target, multi_extent=False):
    """A 4 MiB ISO with primary/Joliet trees and separated multi-extents."""
    block = 2048
    blocks = 2048
    image = bytearray(block * blocks)

    def both16(value):
        return struct.pack('<H', value) + struct.pack('>H', value)

    def both32(value):
        return struct.pack('<I', value) + struct.pack('>I', value)

    def record(name, extent, size, flags):
        length = 33 + len(name) + (len(name) % 2 == 0)
        return (bytes((length, 0)) + both32(extent) + both32(size)
                + bytes((126, 10, 30, 0, 0, 0, 0, flags, 0, 0))
                + both16(1) + bytes((len(name),)) + name + bytes(length - 33 - len(name)))

    def put(lba, data):
        image[lba * block:lba * block + len(data)] = data

    extents = [(256, target)]
    if multi_extent:
        assert len(target) > block
        extents = [(256, target[:block]), (768, target[block:])]
    for lba, data in extents:
        put(lba, data)
    for joliet, root, docs, table in [(False, 20, 21, 24), (True, 22, 23, 26)]:
        doc_name = 'docs'.encode('utf-16-be') if joliet else b'DOCS'
        file_name = 'target.txt;1'.encode('utf-16-be') if joliet else b'TARGET.TXT;1'
        root_data = (record(b'\x00', root, block, 2) + record(b'\x01', root, block, 2)
                     + record(doc_name, docs, block, 2))
        doc_data = record(b'\x00', docs, block, 2) + record(b'\x01', root, block, 2)
        for i, (lba, data) in enumerate(extents):
            doc_data += record(file_name, lba, len(data), 0x80 if i + 1 < len(extents) else 0)
        put(root, root_data)
        put(docs, doc_data)
        for big in (False, True):
            endian = '>' if big else '<'
            def table_entry(name, extent, parent):
                return (bytes((len(name), 0)) + struct.pack(endian + 'IH', extent, parent)
                        + name + bytes(len(name) % 2))
            data = table_entry(b'\x00', root, 1) + table_entry(doc_name, docs, 1)
            put(table + int(big), data)
        descriptor = bytearray(block)
        descriptor[:7] = bytes((2 if joliet else 1,)) + b'CD001\x01'
        descriptor[8:72] = b' ' * 64
        descriptor[80:88] = both32(blocks)
        if joliet:
            descriptor[88:91] = b'%/E'
        descriptor[120:124] = both16(1)
        descriptor[124:128] = both16(1)
        descriptor[128:132] = both16(block)
        descriptor[132:140] = both32(len(data))
        descriptor[140:144] = struct.pack('<I', table)
        descriptor[148:152] = struct.pack('>I', table + 1)
        descriptor[156:190] = record(b'\x00', root, block, 2)
        for offset in (813, 830, 847, 864):
            descriptor[offset:offset + 17] = b'2026103000000000\x00'
        descriptor[881] = 1
        put(17 if joliet else 16, descriptor)
    put(18, b'\xffCD001\x01')
    path.write_bytes(image)
