# PostSRSd - Sender Rewriting Scheme daemon for Postfix
# Copyright 2012-2026 Timo Röhling <timo@gaussglocke.de>
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
import itertools
import os
import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import time


class SockStream:
    def __init__(self, sock: socket.socket):
        self._sock = sock
        self._rdbuf = b""

    def read(self, size: int):
        result = b""
        remaining = size
        while remaining > len(self._rdbuf):
            result += self._rdbuf
            remaining -= len(self._rdbuf)
            self._rdbuf = self._sock.recv(4096)
            if len(self._rdbuf) == 0:
                raise ConnectionError("no data")
        result += self._rdbuf[:remaining]
        self._rdbuf = self._rdbuf[remaining:]
        return result

    def write(self, data: bytes):
        self._sock.sendall(data)


def write_netstring(sock_stream: SockStream, data: str):
    data_bytes = data.encode()
    sock_stream.write(f"{len(data_bytes)}:".encode() + data_bytes + b",")


def read_netstring(sock_stream: SockStream):
    digit = sock_stream.read(1)
    data_size = 0
    while digit >= b"0" and digit <= b"9":
        data_size = 10 * data_size + int(digit)
        digit = sock_stream.read(1)
    if digit != b":":
        print("ERR: ':' expected")
        return None
    data = sock_stream.read(data_size)
    comma = sock_stream.read(1)
    if comma != b",":
        print("ERR: ',' expected")
        return None
    return data.decode()


@contextlib.contextmanager
def postsrsd_instance(postsrsd: str, domains_file: pathlib.Path):
    with tempfile.TemporaryDirectory() as tmpdirname:
        tmpdir = pathlib.Path(tmpdirname)
        with open(tmpdir / "postsrsd.conf", "w") as f:
            f.write(
                "domains = {}\n"
                f"domains-file = {str(domains_file)}\n"
                "domains-file-watch = on\n"
                "keep-alive = 10\n"
                'chroot-dir = ""\n'
                'unprivileged-user = ""\n'
                f"original-envelope = embedded\n"
                f'socketmap = unix:{tmpdir / "postsrsd.sock"}\n'
                f'secrets-file = {tmpdir / "postsrsd.secret"}\n'
            )
        with open(tmpdir / "postsrsd.secret", "w") as f:
            f.write("tops3cr3t\n")
        proc = subprocess.Popen(
            [postsrsd, "-C", str(tmpdir / "postsrsd.conf")],
            start_new_session=True,
        )
        wait = 50
        while not (tmpdir / "postsrsd.sock").exists() and wait > 0:
            time.sleep(0.1)
            wait -= 1
        try:
            yield str(tmpdir / "postsrsd.sock").encode(), proc.pid
        finally:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            except PermissionError:
                os.kill(proc.pid, signal.SIGTERM)
            proc.wait()


def run_query(sock_stream: SockStream, test_domain: str | None):
    write_netstring(sock_stream, "forward test@otherdomain.com")
    result = read_netstring(sock_stream)
    if (
        result is None
        or not result.startswith("OK SRS0=")
        or not result.endswith(f"=otherdomain.com=test@{test_domain}")
    ):
        raise AssertionError(
            f"expected 'OK SRS0=...=otherdomain.com=test@{test_domain}', got {result!r}"
        )


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmpdirname:
        domains_file = pathlib.Path(tmpdirname) / "domains.txt"
        with open(domains_file, "w") as f:
            f.write("example.com")
        with postsrsd_instance(sys.argv[1], domains_file=domains_file) as daemon:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
            sock.connect(daemon[0])
            sock_stream = SockStream(sock)
            try:
                previous_domain = "example.com"
                run_query(sock_stream, previous_domain)
                for counter, test_domain in enumerate(
                    itertools.cycle(
                        [
                            "one.com",
                            "two.com",
                            "three.com",
                        ]
                    )
                ):
                    if counter % 3 == 0:
                        sys.stderr.write("Opening domain file and modifying content\n")
                        with open(domains_file, "w") as f:
                            f.write(f"{test_domain}\n")
                    elif counter % 3 == 1:
                        sys.stderr.write(
                            "Renaming different file to replace the domain file\n"
                        )
                        tmp_file = pathlib.Path(str(domains_file) + ".tmp")
                        with open(tmp_file, "w") as f:
                            f.write(f"{test_domain}\n")
                        tmp_file.rename(domains_file)
                    else:
                        sys.stderr.write(
                            "Deleting old domains file and creating a new one\n"
                        )
                        domains_file.unlink()
                        with open(domains_file, "w") as f:
                            f.write(f"{test_domain}\n")
                    try:
                        # We deliberately keep the connection open and continue querying
                        # PostSRSd. The daemon should close the connection as soon as the
                        # configuration has been reloaded.
                        max_wait = 100
                        while max_wait > 0:
                            run_query(sock_stream, previous_domain)
                            time.sleep(0.1)
                            max_wait -= 1
                        raise AssertionError("configuration update failed")
                    except ConnectionError:
                        pass
                    sock.close()
                    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
                    sock.connect(daemon[0])
                    sock_stream = SockStream(sock)
                    run_query(sock_stream, test_domain)
                    sys.stderr.write(
                        f"PASS: inotify test {counter + 1} [{test_domain}]\n"
                    )
                    previous_domain = test_domain
                    if counter == 8:
                        break
            except Exception as e:
                sys.stderr.write(f"*** FAIL: {e.__class__.__name__}: {str(e)}\n")
                sys.exit(1)
            finally:
                sock.close()
    sys.exit(0)
