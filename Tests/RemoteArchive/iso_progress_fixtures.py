"""Small ISO trees for progress checks, with primary/Joliet/Rock Ridge names."""
import struct


def write_iso_tree(path, target, *, joliet=False, rock_ridge=False, multi_extent=False):
    block = 2048
    blocks = 2048
    image = bytearray(block * blocks)

    def both16(n):
        return struct.pack('<H', n) + struct.pack('>H', n)

    def both32(n):
        return struct.pack('<I', n) + struct.pack('>I', n)

    def record(name, extent, size, flags=0, extra=b''):
        length = 33 + len(name) + (len(name) % 2 == 0) + len(extra)
        return (bytes((length, 0)) + both32(extent) + both32(size)
                + bytes((126, 10, 31, 0, 0, 0, 0, flags, 0, 0)) + both16(1)
                + bytes((len(name),)) + name + bytes(len(name) % 2 == 0) + extra)

    def put(lba, data):
        image[lba * block:lba * block + len(data)] = data

    def nm(name):
        raw = name.encode('ascii')
        return b'NM' + bytes((5 + len(raw), 1, 0)) + raw

    # Keep the primary tree readable even when Joliet is selected.
    for unicode, root in [(False, 32), *([(True, 48)] if joliet else [])]:
        docs, nested, other = root + 1, root + 2, root + 3

        def named(name, extent, size, flags=0):
            alternate = rock_ridge and not unicode
            identifier = name.upper() if alternate else name
            identifier += '' if flags & 2 else ';1'
            encoded = identifier.encode('utf-16-be' if unicode else 'ascii')
            return record(encoded, extent, size, flags, nm(name) if alternate else b'')

        def dots(at, parent, is_root=False):
            extra = b'SP\x07\x01\xbe\xef\x00' if rock_ridge and is_root else b''
            return record(b'\x00', at, block, 2, extra) + record(b'\x01', parent, block, 2)

        put(root, dots(root, root, True) + named('docs', docs, block, 2)
            + named('other', other, block, 2) + named('root.txt', 256, 4))
        doc_data = dots(docs, root) + named('nested', nested, block, 2)
        if multi_extent:
            doc_data += named('target.txt', 512, block, 0x80)
            doc_data += named('target.txt', 768, len(target) - block)
        else:
            doc_data += named('target.txt', 512, len(target))
        put(docs, doc_data)
        leaf = '\u0645\u0644\u0641.txt' if unicode else 'leaf.txt'
        put(nested, dots(nested, docs) + named(leaf, 257, 6))
        put(other, dots(other, root) + named('empty.bin', 258, 0))
        descriptor = bytearray(block)
        descriptor[:7] = bytes((2 if unicode else 1,)) + b'CD001\x01'
        descriptor[8:72] = b' ' * 64
        descriptor[80:88] = both32(blocks)
        if unicode:
            descriptor[88:91] = b'%/E'
        descriptor[120:124] = both16(1)
        descriptor[124:128] = both16(1)
        descriptor[128:132] = both16(block)
        descriptor[156:190] = record(b'\x00', root, block, 2)
        for offset in (813, 830, 847, 864):
            descriptor[offset:offset + 17] = b'2026103100000000\x00'
        descriptor[881] = 1
        put(17 if unicode else 16, descriptor)
    put(18 if joliet else 17, b'\xffCD001\x01')
    put(256, b'root')
    put(257, b'nested')
    if multi_extent:
        assert len(target) > block
        put(512, target[:block])
        put(768, target[block:])
    else:
        put(512, target)
    path.write_bytes(image)
    leaf = '\u0645\u0644\u0641.txt' if joliet else 'leaf.txt'
    return ['docs', 'other', 'root.txt', 'docs/nested', 'docs/target.txt',
            'docs/nested/' + leaf, 'other/empty.bin']
