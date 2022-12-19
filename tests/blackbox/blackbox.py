# PostSRSd - Sender Rewriting Scheme daemon for Postfix
# Copyright 2012-2022 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: GPL-3.0-only
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
import os
import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import time


def write_netstring(sock, data):
    data_bytes = data.encode()
    sock.send(f"{len(data_bytes)}:".encode() + data_bytes + b",")


def read_netstring(sock):
    digit = sock.recv(1)
    data_size = 0
    while digit >= b"0" and digit <= b"9":
        data_size = 10 * data_size + int(digit)
        digit = sock.recv(1)
    if digit != b":":
        print("ERR: ':' expected")
        return None
    data = sock.recv(data_size)
    comma = sock.recv(1)
    if comma != b",":
        print("ERR: ',' expected")
        return None
    return data.decode()


def execute_queries(faketime, postsrsd, when, queries):
    with tempfile.TemporaryDirectory() as tmpdirname:
        tmpdir = pathlib.Path(tmpdirname)
        with open(tmpdir / "postsrsd.conf", "w") as f:
            f.write(
                'domains = {"example.com"}\n'
                "keep-alive = 2\n"
                'chroot-dir = ""\n'
                'unprivileged-user = ""\n'
                f'socketmap = unix:{tmpdir / "postsrsd.sock"}\n'
                f'secrets-file = {tmpdir / "postsrsd.secret"}\n'
            )
        with open(tmpdir / "postsrsd.secret", "w") as f:
            f.write("tops3cr3t\n")
        proc = subprocess.Popen(
            [faketime, when, postsrsd, "-C", str(tmpdir / "postsrsd.conf")],
            start_new_session=True,
        )
        while not (tmpdir / "postsrsd.sock").exists():
            time.sleep(0.1)
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
            sock.connect(str(tmpdir / "postsrsd.sock").encode())
            for nr, query in enumerate(queries, start=1):
                sys.stderr.write(f"[{nr}] {query[0]}\n")
                sys.stderr.flush()
                write_netstring(sock, query[0])
                result = read_netstring(sock)
                if result != query[1]:
                    raise AssertionError(f"Expected: {query[1]!r}, Got: {result!r}")
                sys.stderr.write(f"[{nr}] OK: {query[1]}\n")
            sock.close()
        finally:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            proc.wait()


if __name__ == "__main__":
    execute_queries(
        sys.argv[1],
        sys.argv[2],
        "2020-01-01 00:01:00 UTC",
        [
            ("forward test@example.com", "NOTFOUND Need not rewrite local domain."),
            (
                "forward test@otherdomain.com",
                "OK SRS0=vmyz=2W=otherdomain.com=test@example.com",
            ),
            ("forward foo", "NOTFOUND No domain."),
            (
                "forward SRS0=XjO9=2V=otherdomain.com=test@example.com",
                "NOTFOUND Need not rewrite local domain.",
            ),
            (
                "forward SRS0=opaque+string@otherdomain.com",
                "OK SRS1=chaI=otherdomain.com==opaque+string@example.com",
            ),
            (
                "forward SRS1=X=thirddomain.com==opaque+string@otherdomain.com",
                "OK SRS1=JIBX=thirddomain.com==opaque+string@example.com",
            ),
            (
                "reverse SRS0=XjO9=2V=otherdomain.com=test@example.com",
                "OK test@otherdomain.com",
            ),
            (
                "reverse SRS1=JIBX=thirddomain.com==opaque+string@example.com",
                "OK SRS0=opaque+string@thirddomain.com",
            ),
            (
                "reverse test@example.com",
                "NOTFOUND Not an SRS address.",
            ),
            (
                "reverse SRS0=te87=T7=otherdomain.com=test@example.com",
                "NOTFOUND Time stamp out of date.",
            ),
            (
                "reverse SRS0=VcIb=7N=otherdomain.com=test@example.com",
                "NOTFOUND Time stamp out of date.",
            ),
            (
                "reverse SRS0=FAKE=2V=otherdomain.com=test@example.com",
                "NOTFOUND Hash invalid in SRS address.",
            ),
            (
                "reverse srs0=xjo9=2v=otherdomain.com=test@example.com",
                "OK test@otherdomain.com",
            ),
            (
                "reverse SRS0=XJO9=2V=OTHERDOMAIN.COM=TEST@EXAMPLE.COM",
                "OK TEST@OTHERDOMAIN.COM",
            ),
            (
                "reverse SRS0=@example.com",
                "NOTFOUND No hash in SRS0 address.",
            ),
            (
                "reverse SRS0=XjO9@example.com",
                "NOTFOUND No timestamp in SRS0 address.",
            ),
            (
                "reverse SRS0=XjO9=2V@example.com",
                "NOTFOUND No host in SRS0 address.",
            ),
            (
                "reverse SRS0=XjO9=2V=otherdomain.com@example.com",
                "NOTFOUND No user in SRS0 address.",
            ),
            (
                "test@example.com",
                "PERM Invalid map.",
            ),
            (
                "",
                "PERM Invalid query.",
            ),
        ],
    )
    sys.exit(0)
