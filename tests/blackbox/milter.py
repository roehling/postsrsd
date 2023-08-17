# PostSRSd - Sender Rewriting Scheme daemon for Postfix
# Copyright 2012-2023 Timo Röhling <timo@gaussglocke.de>
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
import contextlib
import os
import pathlib
import socket
import signal
import subprocess
import struct
import sys
import tempfile
import time


def send_milter(sock, code, data):
    sock.send(struct.pack(">Lc", len(data) + 1, code) + data)


def recv_milter(sock):
    size = struct.unpack(">L", sock.recv(4))
    data = sock.recv(*size)
    return data[:1], data[1:]


def mf_optneg(sock):
    send_milter(sock, b"O", struct.pack(">LLL", 6, 0xFF, 0xFF))
    code, _ = recv_milter(sock)
    return code == b"O"


def mf_connect(sock):
    send_milter(sock, b"C", b"mail.example.com\x00U")
    code, _ = recv_milter(sock)
    return code == b"c"


def mf_envfrom(sock, envfrom):
    send_milter(sock, b"M", b"<" + envfrom.encode() + b">\x00")
    code, _ = recv_milter(sock)
    return code == b"c"


def mf_rcptto(sock, rcptto):
    send_milter(sock, b"R", b"<" + rcptto.encode() + b">\x00")
    code, _ = recv_milter(sock)
    return code == b"c"


def mf_eom(sock):
    new_from = None
    new_rcpt = None
    send_milter(sock, b"E", b"")
    code, data = recv_milter(sock)
    while code in [b"+", b"-", b"e"]:
        if code == b"+":
            new_rcpt = data[1:-2].decode()
        if code == b"e":
            new_from = data[1:-2].decode()
        code, data = recv_milter(sock)
    return code == b"c", new_from, new_rcpt


@contextlib.contextmanager
def postsrsd_instance(postsrsd, when):
    with tempfile.TemporaryDirectory() as tmpdirname:
        tmpdir = pathlib.Path(tmpdirname)
        with open(tmpdir / "postsrsd.conf", "w") as f:
            f.write(
                'domains = {"example.com"}\n'
                "keep-alive = 2\n"
                'chroot-dir = ""\n'
                'unprivileged-user = ""\n'
                f"original-envelope = embedded\n"
                f'socketmap = ""\n'
                f'milter = unix:{tmpdir / "postsrsd.sock"}\n'
                f'secrets-file = {tmpdir / "postsrsd.secret"}\n'
                f'envelope-database = sqlite:{tmpdir / "postsrsd.db"}\n'
            )
        with open(tmpdir / "postsrsd.secret", "w") as f:
            f.write("tops3cr3t\n")
        os.environ["POSTSRSD_FAKETIME"] = when
        proc = subprocess.Popen(
            [postsrsd, "-C", str(tmpdir / "postsrsd.conf")],
            start_new_session=True,
        )
        wait = 50
        while not (tmpdir / "postsrsd.sock").exists() and wait > 0:
            time.sleep(0.1)
            wait -= 1
        try:
            yield str(tmpdir / "postsrsd.sock").encode()
        finally:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            proc.wait()


def execute_queries(postsrsd, when, queries):
    with postsrsd_instance(postsrsd, when) as endpoint:
        for query in queries:
            orig_from, orig_rcpt = query[0]
            new_from, new_rcpt = query[1]
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
            try:
                sock.settimeout(0.5)
                sock.connect(endpoint)
                assert mf_optneg(sock)
                assert mf_connect(sock)
                assert mf_envfrom(sock, orig_from)
                assert mf_rcptto(sock, orig_rcpt)
                ok, srs_from, srs_rcpt = mf_eom(sock)
                assert ok, "mf_eom failed"
                assert srs_from == new_from
                assert srs_rcpt == new_rcpt
                sys.stderr.write(f"query[{query[0]}]: Passed\n")
            finally:
                sock.close()


if __name__ == "__main__":
    execute_queries(
        sys.argv[1],
        when="1577836860",  # 2020-01-01 00:01:00 UTC
        queries=[
            (("sender@example.com", "recipient@example.com"), (None, None)),
            (
                ("sender@otherdomain.com", "recipient@example.com"),
                ("SRS0=9KJ+=2W=otherdomain.com=sender@example.com", None),
            ),
            (
                ("", "SRS0=9KJ+=2W=otherdomain.com=sender@example.com"),
                (None, "sender@otherdomain.com"),
            ),
        ],
    )
