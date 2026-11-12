"""The .amxd container, frozen: the device patcher plus its dependencies (the UI script) in one file.

Layout (little-endian chunk sizes outside `ptch`, big-endian inside, as Max writes them):

    'ampf' u32=4 <type>          type: 'aaaa' audio effect, 'iiii' instrument, 'mmmm' MIDI effect
    'meta' u32=4 u32=7           7 = frozen
    'ptch' u32=len
        'mx@c' u32be=16 u32be=0 u32be=<offset of dlst>
        <file 0: the device patcher JSON, NUL-terminated> <file 1> ...
        'dlst' u32be=len  ( 'dire' u32be=len  type fnam sz32 of32 vers flag mdat )*

Offsets in `of32` count from the 'mx@c' tag. Field sizes include their 8-byte header; `fnam` is NUL-padded to 4.
"""

from __future__ import annotations

import json
import struct

DEVICE_TYPES = {"audio_effect": b"aaaa", "instrument": b"iiii", "midi_effect": b"mmmm"}
AMXD_TYPE_CODES = {kind: struct.unpack(">I", code)[0] for kind, code in DEVICE_TYPES.items()}

# Fixed timestamp (Mac epoch seconds) so builds are byte-for-byte reproducible.
_MDAT = 0xE5000000


def _field(tag: bytes, data: bytes) -> bytes:
    return tag + struct.pack(">I", len(data) + 8) + data


def _name(name: str) -> bytes:
    raw = name.encode("utf-8") + b"\x00"
    return raw + b"\x00" * (-len(raw) % 4)


def encode(kind: str, device_filename: str, patcher: dict, files: list[tuple[str, bytes, bytes]]) -> bytes:
    """files: (name, 4-char Max type like b'TEXT', contents) embedded after the patcher."""
    body = json.dumps(patcher, indent="\t", sort_keys=False).encode("utf-8") + b"\n\x00"
    entries = [(device_filename, b"JSON", body, 0x11)] + [(n, t, data, 0) for n, t, data in files]

    payload = b""
    directory = b""
    offset = 16
    for name, ftype, data, flag in entries:
        directory += _field(b"dire", b"".join([
            _field(b"type", ftype), _field(b"fnam", _name(name)), _field(b"sz32", struct.pack(">I", len(data))),
            _field(b"of32", struct.pack(">I", offset)), _field(b"vers", struct.pack(">I", 0)),
            _field(b"flag", struct.pack(">I", flag)), _field(b"mdat", struct.pack(">I", _MDAT)),
        ]))
        payload += data
        offset += len(data)
    dlst = _field(b"dlst", directory)
    ptch = b"mx@c" + struct.pack(">III", 16, 0, 16 + len(payload)) + payload + dlst

    header = b"ampf" + struct.pack("<I", 4) + DEVICE_TYPES[kind] + b"meta" + struct.pack("<I", 4) + struct.pack("<I", 7)
    return header + b"ptch" + struct.pack("<I", len(ptch)) + ptch


def decode(data: bytes) -> tuple[str, dict, dict[str, bytes]]:
    """Inverse of encode (used by tests): (kind, patcher, {filename: contents})."""
    kind = next(k for k, code in DEVICE_TYPES.items() if data[8:12] == code)
    start = data.index(b"ptch") + 8
    ptch = data[start:]
    assert ptch[:4] == b"mx@c", "not a frozen device"
    dlst_at = struct.unpack(">I", ptch[12:16])[0]
    dlst = ptch[dlst_at:]
    files: dict[str, bytes] = {}
    i = 8
    while i < len(dlst):
        size = struct.unpack(">I", dlst[i + 4:i + 8])[0]
        fields: dict[str, bytes] = {}
        j = i + 8
        while j < i + size:
            fsize = struct.unpack(">I", dlst[j + 4:j + 8])[0]
            fields[dlst[j:j + 4].decode()] = dlst[j + 8:j + fsize]
            j += fsize
        off = struct.unpack(">I", fields["of32"])[0]
        length = struct.unpack(">I", fields["sz32"])[0]
        files[fields["fnam"].rstrip(b"\x00").decode()] = ptch[off:off + length]
        i += size
    first = next(iter(files))
    patcher = json.loads(files.pop(first).rstrip(b"\x00").decode("utf-8"))
    return kind, patcher, files
