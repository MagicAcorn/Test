"""HVPK archive writer: a sorted directory of 32-byte aligned assets."""
import struct
from meshbuild import fnv1a


class PakWriter:
    def __init__(self):
        self.entries = {}

    def add(self, name, typ, data):
        h = fnv1a(name)
        if h in self.entries and self.entries[h][0] != name:
            raise ValueError('hash collision: %s vs %s' % (name, self.entries[h][0]))
        assert len(typ) == 4
        self.entries[h] = (name, typ.encode('ascii'), bytes(data))

    def __contains__(self, name):
        return fnv1a(name) in self.entries

    def write(self, path):
        items = sorted(self.entries.items())
        n = len(items)
        hdr_size = 32
        dir_size = 16 * n
        names = b''
        name_offs = []
        for h, (name, typ, data) in items:
            name_offs.append(len(names))
            names += name.encode('utf-8') + b'\0'
        off = hdr_size + dir_size + 4 * n + len(names)
        off += (-off) % 32
        layout = []
        for h, (name, typ, data) in items:
            layout.append(off)
            off += len(data)
            off += (-off) % 32
        total = off
        out = bytearray(total)
        struct.pack_into('>4sIIIII', out, 0, b'HVPK', 1, n, hdr_size, hdr_size + dir_size, total)
        for i, (h, (name, typ, data)) in enumerate(items):
            struct.pack_into('>I4sII', out, hdr_size + 16 * i, h, typ, layout[i], len(data))
            struct.pack_into('>I', out, hdr_size + dir_size + 4 * i, name_offs[i])
            out[layout[i]:layout[i] + len(data)] = data
        noff = hdr_size + dir_size + 4 * n
        out[noff:noff + len(names)] = names
        with open(path, 'wb') as f:
            f.write(out)
        return total
