#!/usr/bin/python3

from pathlib import Path
import struct

DIR = Path(__file__).parent / "milter"


def pkt(payload: bytes):
    return struct.pack(">L", len(payload)) + payload


optneg = struct.pack(">cLLL", b"O", 6, 0xFF, 0xFF)
mailfrom = b"M<sender@example.com>\x00"
rcptto = b"R<recipient@example.net>\x00"
eom = b"E"

(DIR / "valid").write_bytes(pkt(optneg) + pkt(mailfrom) + pkt(rcptto) + pkt(eom))
(DIR / "optneg").write_bytes(pkt(optneg))
(DIR / "mailfrom").write_bytes(pkt(mailfrom))
(DIR / "rcptto").write_bytes(pkt(rcptto))
(DIR / "eom").write_bytes(pkt(eom))
