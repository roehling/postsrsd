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
import contextlib
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


@contextlib.contextmanager
def postsrsd_instance(faketime, postsrsd, when, use_database):
    with tempfile.TemporaryDirectory() as tmpdirname:
        tmpdir = pathlib.Path(tmpdirname)
        with open(tmpdir / "postsrsd.conf", "w") as f:
            f.write(
                'domains = {"example.com"}\n'
                "keep-alive = 2\n"
                'chroot-dir = ""\n'
                'unprivileged-user = ""\n'
                f'original-envelope = {"database" if use_database else "embedded"}\n'
                f'socketmap = unix:{tmpdir / "postsrsd.sock"}\n'
                f'secrets-file = {tmpdir / "postsrsd.secret"}\n'
                f'envelope-database = sqlite:{tmpdir / "postsrsd.db"}\n'
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
            yield str(tmpdir / "postsrsd.sock").encode()
        finally:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
            proc.wait()


def execute_queries(faketime, postsrsd, when, use_database, queries):
    with postsrsd_instance(faketime, postsrsd, when, use_database) as endpoint:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
        sock.connect(endpoint)
        try:
            for nr, query in enumerate(queries, start=1):
                write_netstring(sock, query[0])
                result = read_netstring(sock)
                if result != query[1]:
                    raise AssertionError(
                        f"query[{query[0]}]: FAILED: Expected reply {query[1]!r}, got: {result!r}"
                    )
                sys.stderr.write(f"query[{query[0]}]: Passed\n")
        finally:
            sock.close()


def execute_death_tests(faketime, postsrsd, when, use_database, queries):
    with postsrsd_instance(faketime, postsrsd, when, use_database) as endpoint:
        for nr, query in enumerate(queries, start=1):
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
                sock.settimeout(0.5)
                sock.connect(endpoint)
                sock.send(query)
                result = read_netstring(sock)
                if result != "PERM Invalid query.":
                    raise AssertionError(
                        f"death_test[{query}]: FAILED: Expected reply 'PERM Invalid query.', got: {result!r}"
                    )
                try:
                    write_netstring(sock, "forward test@example.com")
                    result = read_netstring(sock)
                    raise AssertionError(
                        f"death_test[{query}]: FAILED: Expected connection closed, got: {result!r}"
                    )
                except TimeoutError:
                    pass
                sys.stderr.write(f"death_test[{query}]: Passed\n")
            finally:
                sock.close()


if __name__ == "__main__":
    execute_queries(
        sys.argv[1],
        sys.argv[2],
        when="2020-01-01 00:01:00 UTC",
        use_database=False,
        queries=[
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
                "NOTFOUND Time stamp out of date.",
            ),
            # Reject valid SRS0 address with time stamp 6 month in the future
            (
                "reverse SRS0=VcIb=7N=otherdomain.com=test@example.com",
                "NOTFOUND Time stamp out of date.",
            ),
            # Reject SRS0 address with invalid hash
            (
                "reverse SRS0=FAKE=2V=otherdomain.com=test@example.com",
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
                "NOTFOUND No hash in SRS0 address.",
            ),
            # Reject SRS0 address without time stamp
            (
                "reverse SRS0=XjO9@example.com",
                "NOTFOUND No timestamp in SRS0 address.",
            ),
            # Reject SRS0 address without original domain
            (
                "reverse SRS0=XjO9=2V@example.com",
                "NOTFOUND No host in SRS0 address.",
            ),
            # Reject SRS0 address without original localpart
            (
                "reverse SRS0=XjO9=2V=otherdomain.com@example.com",
                "NOTFOUND No user in SRS0 address.",
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
        ],
    )
    execute_death_tests(
        sys.argv[1],
        sys.argv[2],
        when="2020-01-01 00:01:00 UTC",
        use_database=False,
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
    )
    if sys.argv[3] == "1":
        execute_queries(
            sys.argv[1],
            sys.argv[2],
            when="2020-01-01 00:01:00 UTC",
            use_database=True,
            queries=[
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
            ],
        )
    sys.exit(0)
