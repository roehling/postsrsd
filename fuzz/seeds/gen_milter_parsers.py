#!/usr/bin/python3

from pathlib import Path
import struct

DIR = Path(__file__).parent / "milter_parsers"

example_list = [b"i", b"foo", b"bar", b"foobar", b"test@example.com"]

(DIR / "list").write_bytes(b"\x00".join(example_list))
