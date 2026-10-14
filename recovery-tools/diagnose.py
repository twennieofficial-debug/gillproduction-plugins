"""Offline UDIF structure/chunk integrity inspection; never an Apple mount test."""
import argparse
import hashlib
import json
import plistlib
import struct
import zipfile
import zlib

def inspect(b):
    assert len(b) >= 512 and b[-512:-508] == b'koly', 'Missing UDIF trailer'
    trailer = b[-512:]
    assert struct.unpack_from('>II', trailer, 4) == (4, 512), 'Unsupported UDIF trailer'
    data_offset, data_length = struct.unpack_from('>QQ', trailer, 24)
    xml_offset, xml_length = struct.unpack_from('>QQ', trailer, 216)
    assert data_offset + data_length <= xml_offset
    assert xml_offset + xml_length + 512 == len(b), 'Truncated or extra bytes'
    blocks = []
    for item in plistlib.loads(b[xml_offset:xml_offset+xml_length])['resource-fork']['blkx']:
        d = item['Data']
        assert d[:4] == b'mish'
        base = struct.unpack_from('>Q', d, 24)[0]
        count = struct.unpack_from('>I', d, 200)[0]
        assert len(d) == 204 + count * 40
        crc, end, run_types = 0, 0, set()
        for i in range(count):
            typ, _, sector, sectors, offset, length = struct.unpack_from('>IIQQQQ', d, 204+i*40)
            run_types.add(hex(typ))
            if typ == 0xffffffff:
                assert i == count-1 and sector == end
                continue
            assert sector == end, 'Missing or overlapping sectors'
            end += sectors
            assert data_offset + base + offset + length <= data_offset + data_length
            raw = b[data_offset+base+offset:data_offset+base+offset+length]
            if typ == 0x80000005:
                raw = zlib.decompress(raw)
            elif typ == 0:
                raw = bytes(sectors*512)
            elif typ == 2:
                # UDIF IGNORE chunks have no checksum contribution.
                assert length == 0
                continue
            else:
                assert typ == 1, 'Unsupported UDIF chunk codec'
            assert len(raw) == sectors*512, 'Decoded chunk length mismatch'
            crc = zlib.crc32(raw, crc)
        stored = struct.unpack_from('>I', d, 72)[0]
        assert struct.unpack_from('>I', d, 64)[0] == 2 and crc == stored, 'Chunk checksum mismatch'
        blocks.append({'name': item['Name'], 'sector_count': end, 'crc32_stored': stored,
                       'crc32_calculated': crc, 'chunk_types': sorted(run_types)})
    return {'size': len(b), 'sha256': hashlib.sha256(b).hexdigest(), 'trailer_magic': 'koly',
            'data_offset': data_offset, 'data_length': data_length, 'xml_offset': xml_offset,
            'xml_length': xml_length, 'exact_trailer_bounds': True, 'blocks': blocks,
            'all_blkx_crc32_match': True, 'native_hdiutil_verified': False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive')
    parser.add_argument('output')
    args = parser.parse_args()
    with zipfile.ZipFile(args.archive) as archive:
        records = {name: inspect(archive.read(name + '-22-MAC-UNSIGNIERT-TESTVERSION.dmg'))
                   for name in ('GILLPRECISION', 'GILLRACK')}
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(records, f, indent=2)

if __name__ == '__main__':
    main()
