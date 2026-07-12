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
import sys

from testhelper import *
from collections.abc import Iterable


def execute_queries(
    postsrsd: str,
    when: str,
    queries: Iterable[tuple[str, str]],
    database: Database = Database.NONE,
    socket_family: SocketFamily = SocketFamily.UNIX,
):
    with PostSRSd(
        postsrsd,
        when=when,
        database=database,
        socket_family=socket_family,
        socket_type=SocketType.SOCKETMAP,
    ) as daemon:
        sock = daemon.connect()
        sock_stream = SockStream(sock)
        try:
            for query in queries:
                write_netstring(sock_stream, query[0])
                result = read_netstring(sock_stream)
                if result != query[1]:
                    raise AssertionError(
                        f"{query[0]!r}: expected reply {query[1]!r}, got: {result!r}"
                    )
                sys.stderr.write(f"PASS: {database!r},{socket_family!r},{query[0]!r}\n")
        except AssertionError as e:
            sys.stderr.write(f"*** FAIL: {database!r},{socket_family!r},{str(e)}\n")
            return False
        finally:
            sock.close()
    return True


def socketmap_protocol_violations(
    postsrsd: str, when: str, queries: Iterable[bytes], socket_family: SocketFamily
):
    with PostSRSd(postsrsd, when=when, socket_family=socket_family) as daemon:
        for query in queries:
            sock = None
            try:
                sock = daemon.connect()
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
                sys.stderr.write(f"PASS: {socket_family!r},{query!r}\n")
            except AssertionError as e:
                sys.stderr.write(f"*** FAIL: {socket_family!r},{query!r}: {str(e)}\n")
                return False
            finally:
                if sock is not None:
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
    # Ignore valid SRS0 address with time stamp older than 6 months
    (
        "reverse SRS0=te87=T7=otherdomain.com=test@example.com",
        "NOTFOUND Time stamp out of date.",
    ),
    # Ignore valid SRS0 address with time stamp 6 months in the future
    (
        "reverse SRS0=VcIb=7N=otherdomain.com=test@example.com",
        "NOTFOUND Time stamp out of date.",
    ),
    # Ignore SRS0 address with invalid hash (locally created)
    (
        "reverse SRS0=FAKE=2V=otherdomain.com=test@example.com",
        "NOTFOUND Hash invalid in SRS address.",
    ),
    # Ignore SRS0 address with invalid hash (foreign sender)
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
    # Check base64url hash encoding
    (
        "forward hash234@somedomain.com",
        "OK SRS0=-3_5=2W=somedomain.com=hash234@example.com",
    ),
    # Recover mail address from base64url encoding
    (
        "reverse SRS0=-3_5=2W=somedomain.com=hash234@example.com",
        "OK hash234@somedomain.com",
    ),
    # Recover mail address from base64 encoding
    (
        "reverse SRS0=+3/5=2W=somedomain.com=hash234@example.com",
        "OK hash234@somedomain.com",
    ),
    # Ignore SRS0 address without authenticating hash
    (
        "reverse SRS0=@example.com",
        "NOTFOUND No hash in SRS0 address.",
    ),
    # Ignore SRS0 address without time stamp
    (
        "reverse SRS0=XjO9@example.com",
        "NOTFOUND No timestamp in SRS0 address.",
    ),
    # Ignore SRS0 address without original domain
    (
        "reverse SRS0=XjO9=2V@example.com",
        "NOTFOUND No host in SRS0 address.",
    ),
    # Ignore SRS0 address without original localpart
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
    for socket_family in [SocketFamily.UNIX, SocketFamily.IP]:
        if not execute_queries(
            sys.argv[1],
            when="1577836860",  # 2020-01-01 00:01:00 UTC
            queries=STATELESS_QUERIES,
            socket_family=socket_family,
        ):
            sys.exit(1)
        if sys.argv[2] == "1":
            if not execute_queries(
                sys.argv[1],
                when="1577836860",  # 2020-01-01 00:01:00 UTC
                queries=DATABASE_QUERIES,
                database=Database.SQLITE,
                socket_family=socket_family,
            ):
                sys.exit(1)
        if sys.argv[3] == "1":
            if not execute_queries(
                sys.argv[1],
                when="1577836860",  # 2020-01-01 00:01:00 UTC
                queries=DATABASE_QUERIES,
                database=Database.REDIS,
                socket_family=socket_family,
            ):
                sys.exit(1)
        if not socketmap_protocol_violations(
            sys.argv[1],
            when="1577836860",
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
            socket_family=socket_family,
        ):
            sys.exit(1)
