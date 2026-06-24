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
import signal
import socket
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterable


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
def postsrsd_instance(
    postsrsd: str, when: str, with_sqlite: bool = False, with_redis: bool = False
):
    with tempfile.TemporaryDirectory() as tmpdirname:
        tmpdir = pathlib.Path(tmpdirname)
        with open(tmpdir / "postsrsd.conf", "w") as f:
            db_uri = '""'
            if with_sqlite:
                db_uri = f'sqlite:{tmpdir / "postsrsd.db"}'
            if with_redis:
                db_uri = "redis:localhost:6379"
            f.write(
                'domains = {"example.com"}\n'
                "keep-alive = 10\n"
                'chroot-dir = ""\n'
                'unprivileged-user = ""\n'
                f'original-envelope = {"database" if with_sqlite or with_redis else "embedded"}\n'
                f'socketmap = unix:{tmpdir / "postsrsd.sock"}\n'
                f'secrets-file = {tmpdir / "postsrsd.secret"}\n'
                f"envelope-database = {db_uri}\n"
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
            yield str(tmpdir / "postsrsd.sock").encode(), proc.pid
        finally:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            proc.wait()


def execute_queries(
    postsrsd: str,
    when: str,
    queries: Iterable[tuple[str, str]],
    with_sqlite: bool = False,
    with_redis: bool = False,
):
    with postsrsd_instance(
        postsrsd, when, with_sqlite=with_sqlite, with_redis=with_redis
    ) as daemon:
        st = os.stat(daemon[0])
        assert st.st_mode & 0o777 == 0o666
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
        sock.connect(daemon[0])
        sock_stream = SockStream(sock)
        try:
            for query in queries:
                write_netstring(sock_stream, query[0])
                result = read_netstring(sock_stream)
                if result != query[1]:
                    raise AssertionError(
                        f"{query[0]!r},sqlite={with_sqlite},redis={with_redis}: expected reply {query[1]!r}, got: {result!r}"
                    )
                sys.stderr.write(
                    f"PASS: {query[0]!r},sqlite={with_sqlite},redis={with_redis}\n"
                )
        except AssertionError as e:
            sys.stderr.write(f"*** FAIL: {str(e)}\n")
            return False
        finally:
            sock.close()
    return True


def execute_death_tests(postsrsd: str, when: str, queries: Iterable[bytes]):
    with postsrsd_instance(postsrsd, when) as daemon:
        for query in queries:
            sock = None
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
                sock.settimeout(10)
                sock.connect(daemon[0])
                sock_stream = SockStream(sock)
                sock_stream.write(query)
                result = read_netstring(sock_stream)
                if result != "PERM Invalid query.":
                    raise AssertionError(
                        f"expected reply 'PERM Invalid query.', got: {result!r}"
                    )
                try:
                    write_netstring(sock_stream, "forward test@example.com")
                    result = read_netstring(sock_stream)
                    raise AssertionError(f"expected connection closed, got: {result!r}")
                except ConnectionError:
                    # Expected behavior
                    pass
                sys.stderr.write(f"PASS: {query!r}\n")
            except AssertionError as e:
                sys.stderr.write(f"*** FAIL: {query!r}: {str(e)}\n")
                return False
            finally:
                if sock is not None:
                    sock.close()
    return True


def execute_sighup_tests(
    postsrsd: str,
    when: str,
    queries: Iterable[tuple[str, str]],
    with_sqlite: bool = False,
    with_redis: bool = False,
):
    with postsrsd_instance(
        postsrsd, when, with_sqlite=with_sqlite, with_redis=with_redis
    ) as daemon:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
        sock.connect(daemon[0])
        sock_stream = SockStream(sock)
        try:
            for query in queries:
                retry = 3
                while True:
                    try:
                        write_netstring(sock_stream, query[0])
                        result = read_netstring(sock_stream)
                        if result != query[1]:
                            raise AssertionError(
                                f"{query[0]!r},sqlite={with_sqlite},redis={with_redis}]: expected reply {query[1]!r}, got: {result!r}"
                            )
                        sys.stderr.write(
                            f"PASS: {query[0]!r},sqlite={with_sqlite},redis={with_redis}\n"
                        )
                        os.kill(daemon[1], signal.SIGHUP)
                        break
                    except ConnectionError as e:
                        if retry > 0:
                            sys.stderr.write("(reconnect after SIGHUP)\n")
                            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
                            sock.connect(daemon[0])
                            sock_stream = SockStream(sock)
                            retry -= 1
                        else:
                            sys.stderr.write(
                                f"FAIL: {query[0]!r},sqlite={with_sqlite},redis={with_redis}: "
                            )
        except AssertionError as e:
            sys.stderr.write(f"*** FAIL: {str(e)}\n")
            return False
        finally:
            sock.close()
    return True


STATELESS_QUERIES = [
    # No rewrite for local domain
    ("forward test@example.com", "NOTFOUND Need not rewrite local domain."),
    # Regular rewrite
    (
        "forward test@otherdomain.com",
        "OK SRS0=vmyz=2W=otherdomain.com=test@example.com",
    ),
    # No rerwite for mail address without domain
    ("forward foo", "NOTFOUND No domain."),
    # No rewrite for SRS address which is already in the local domain
    (
        "forward SRS0=XjO9=2V=otherdomain.com=test@example.com",
        "NOTFOUND Need not rewrite local domain.",
    ),
    # Convert foreign SRS0 address to SRS1 address
    (
        "forward SRS0=opaque+string@otherdomain.com",
        "OK SRS1=chaI=otherdomain.com==opaque+string@example.com",
    ),
    # Change domain part of foreign SRS1 address
    (
        "forward SRS1=X=thirddomain.com==opaque+string@otherdomain.com",
        "OK SRS1=JIBX=thirddomain.com==opaque+string@example.com",
    ),
    # Recover original mail address from valid SRS0 address
    (
        "reverse SRS0=XjO9=2V=otherdomain.com=test@example.com",
        "OK test@otherdomain.com",
    ),
    # Recover original SRS0 address from valid SRS1 address
    (
        "reverse SRS1=JIBX=thirddomain.com==opaque+string@example.com",
        "OK SRS0=opaque+string@thirddomain.com",
    ),
    # Do not rewrite mail address which is not an SRS address
    (
        "reverse test@example.com",
        "NOTFOUND Not an SRS address.",
    ),
    # Reject valid SRS0 address with time stamp older than 6 months
    (
        "reverse SRS0=te87=T7=otherdomain.com=test@example.com",
        "PERM Time stamp out of date.",
    ),
    # Reject valid SRS0 address with time stamp 6 months in the future
    (
        "reverse SRS0=VcIb=7N=otherdomain.com=test@example.com",
        "PERM Time stamp out of date.",
    ),
    # Reject SRS0 address with invalid hash
    (
        "reverse SRS0=FAKE=2V=otherdomain.com=test@example.com",
        "PERM Hash invalid in SRS address.",
    ),
    # Pass through (do not reject) an SRS address in a domain we do not sign
    # for: its hash can never validate under our secret because it was minted
    # by a different signer and is only transiting this host. Rejecting it
    # would defer legitimate relay / backup-MX / multi-hop forwarding.
    (
        "reverse SRS0=FAKE=2V=otherdomain.com=test@foreign.example",
        "NOTFOUND Hash invalid in SRS address.",
    ),
    # Recover mail address from all-lowercase SRS0 address
    (
        "reverse srs0=xjo9=2v=otherdomain.com=test@example.com",
        "OK test@otherdomain.com",
    ),
    # Recover mail address from all-uppcase SRS0 address
    (
        "reverse SRS0=XJO9=2V=OTHERDOMAIN.COM=TEST@EXAMPLE.COM",
        "OK TEST@OTHERDOMAIN.COM",
    ),
    # Reject SRS0 address without authenticating hash
    (
        "reverse SRS0=@example.com",
        "PERM No hash in SRS0 address.",
    ),
    # Reject SRS0 address without time stamp
    (
        "reverse SRS0=XjO9@example.com",
        "PERM No timestamp in SRS0 address.",
    ),
    # Reject SRS0 address without original domain
    (
        "reverse SRS0=XjO9=2V@example.com",
        "PERM No host in SRS0 address.",
    ),
    # Reject SRS0 address without original localpart
    (
        "reverse SRS0=XjO9=2V=otherdomain.com@example.com",
        "PERM No user in SRS0 address.",
    ),
    # Reject Database alias
    (
        "reverse SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com",
        "PERM No database for alias.",
    ),
    # Reject invalid socketmap
    (
        "test@example.com",
        "PERM Invalid map.",
    ),
    # Test long address
    (
        ("forward test@" + "a" * (512 - 9) + ".net"),
        ("OK SRS0=G7tR=2W=" + "a" * (512 - 9) + ".net=test@example.com"),
    ),
    # Recover long address
    (
        ("reverse SRS0=iCvJ=2W=" + "a" * (512 - 34) + ".net=test@example.com"),
        ("OK test@" + "a" * (512 - 34) + ".net"),
    ),
    # Test too long address
    (
        ("forward test@" + "a" * (513 - 9) + ".net"),
        "PERM Too big.",
    ),
    # Test empty address
    (
        "forward ",
        "NOTFOUND No domain.",
    ),
    # Test empty quotes
    (
        'forward ""',
        "NOTFOUND No domain.",
    ),
]

DATABASE_QUERIES = [
    # Regular rewrite
    (
        "forward test@otherdomain.com",
        "OK SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com",
    ),
    # Recover address from alias
    (
        "reverse SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com",
        "OK test@otherdomain.com",
    ),
    # Recover address from case-munged alias
    (
        "reverse SRS0=bxzH=2W=1=dcjgde6n24lcrt41a4t0g1uif0dtkkqj@example.com",
        "OK test@otherdomain.com",
    ),
    # Reject unknown alias
    (
        "reverse SRS0=hdxW=2W=1=VVVVVVUNVVVVVVS1VVVVVVUIVVVTKKQJ@example.com",
        "NOTFOUND Unknown alias.",
    ),
    # No rewrite for SRS address which is already in the local domain
    (
        "forward SRS0=XjO9=2V=otherdomain.com=test@example.com",
        "NOTFOUND Need not rewrite local domain.",
    ),
    # Convert foreign SRS0 address to SRS1 address
    (
        "forward SRS0=opaque+string@otherdomain.com",
        "OK SRS1=chaI=otherdomain.com==opaque+string@example.com",
    ),
    # Change domain part of foreign SRS1 address
    (
        "forward SRS1=X=thirddomain.com==opaque+string@otherdomain.com",
        "OK SRS1=JIBX=thirddomain.com==opaque+string@example.com",
    ),
    # Recover original mail address from valid SRS0 address
    (
        "reverse SRS0=XjO9=2V=otherdomain.com=test@example.com",
        "OK test@otherdomain.com",
    ),
]

if __name__ == "__main__":
    if not execute_queries(
        sys.argv[1],
        when="1577836860",  # 2020-01-01 00:01:00 UTC
        with_sqlite=False,
        queries=STATELESS_QUERIES,
    ):
        sys.exit(1)
    if not execute_death_tests(
        sys.argv[1],
        when="1577836860",  # 2020-01-01 00:01:00 UTC
        queries=[
            # Empty query
            b"0:,",
            # Netstring that exceeds the allowed length
            (b"1024:forward " + b"a" * 1016 + b","),
            # Old-style TCP table query
            b"get test@example.com\n",
            # Excessively large netstring length
            b"18446744073709551616:some data...",
            # Invalid netstring terminator
            b"28:forward test@otherdomain.com;",
        ],
    ):
        sys.exit(1)
    if not execute_sighup_tests(
        sys.argv[1],
        when="1577836860",  # 2020-01-01 00:01:00 UTC
        with_sqlite=False,
        queries=STATELESS_QUERIES,
    ):
        sys.exit(1)
    if sys.argv[2] == "1":
        if not execute_queries(
            sys.argv[1],
            when="1577836860",  # 2020-01-01 00:01:00 UTC
            with_sqlite=True,
            queries=DATABASE_QUERIES,
        ):
            sys.exit(1)
        if not execute_sighup_tests(
            sys.argv[1],
            when="1577836860",  # 2020-01-01 00:01:00 UTC
            with_sqlite=True,
            queries=DATABASE_QUERIES,
        ):
            sys.exit(1)
    if sys.argv[3] == "1":
        if not execute_queries(
            sys.argv[1],
            when="1577836860",  # 2020-01-01 00:01:00 UTC
            with_redis=True,
            queries=DATABASE_QUERIES,
        ):
            sys.exit(1)
        if not execute_sighup_tests(
            sys.argv[1],
            when="1577836860",  # 2020-01-01 00:01:00 UTC
            with_redis=True,
            queries=DATABASE_QUERIES,
        ):
            sys.exit(1)
    sys.exit(0)
