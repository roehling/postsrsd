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
import struct
import sys
import time

from testhelper import *


def mf_optneg(sock_stream: SockStream) -> bool:
    milter_write(sock_stream, struct.pack(">cLLL", b"O", 6, 0xFF, 0xFF))
    return milter_read(sock_stream)[:1] == b"O"


def mf_macro(sock_stream: SockStream, code: bytes):
    milter_write(sock_stream, b"D" + code + b"i\x0001234567\x00")


def mf_envfrom(sock_stream: SockStream, envfrom: str) -> bytes:
    milter_write(sock_stream, b"M<" + envfrom.encode() + b">\x00")
    return milter_read(sock_stream)[:1]


def mf_rcptto(sock_stream: SockStream, rcptto: str) -> bytes:
    milter_write(sock_stream, b"R<" + rcptto.encode() + b">\x00")
    return milter_read(sock_stream)[:1]


def mf_eom(sock_stream: SockStream) -> tuple[bytes, str | None, list[str] | None]:
    new_from = None
    new_rcpts: list[str] | None = None
    milter_write(sock_stream, b"E")
    data = milter_read(sock_stream)
    code = data[:1]
    while code in [b"+", b"-", b"e"]:
        if code == b"+":
            new_rcpt = data[1:-1].decode()
            if new_rcpt[0] == "<" and new_rcpt[-1] == ">":
                new_rcpt = new_rcpt[1:-2]
            if new_rcpts is None:
                new_rcpts = list()
            new_rcpts.append(new_rcpt)
        if code == b"e":
            new_from = data[1:-1].decode()
            if new_from[0] == "<" and new_from[-1] == ">":
                new_from = new_from[1:-2]
        data = milter_read(sock_stream)
        code = data[:1]
    return code, new_from, new_rcpts


def execute_queries(
    postsrsd: str,
    when: str,
    queries: list[
        tuple[tuple[str, list[str]], tuple[bytes, str | None, list[str] | None]]
    ],
    database: Database = Database.NONE,
    socket_family: SocketFamily = SocketFamily.UNIX,
):
    with PostSRSd(
        postsrsd,
        when=when,
        database=database,
        socket_family=socket_family,
        socket_type=SocketType.MILTER,
    ) as daemon:
        with daemon.connect_stream() as sock_stream:
            assert mf_optneg(sock_stream), "milter option negotation failed"
            for query in queries:
                orig_from, orig_rcpts = query[0]
                result, new_from, new_rcpts = query[1]
                try:
                    srs_from = None
                    srs_rcpts = None
                    mf_macro(sock_stream, b"M")
                    srs_result = mf_envfrom(sock_stream, orig_from)
                    if srs_result == b"c":
                        mf_macro(sock_stream, b"R")
                        for orig_rcpt in orig_rcpts:
                            srs_result = mf_rcptto(sock_stream, orig_rcpt)
                            if srs_result != b"c":
                                break
                        if srs_result == b"c":
                            mf_macro(sock_stream, b"E")
                            srs_result, srs_from, srs_rcpts = mf_eom(sock_stream)
                    assert (
                        srs_result == result
                    ), f"expected action {result!r} and got {srs_result!r}"
                    assert (
                        srs_from == new_from
                    ), f"expected from {new_from!r} and got {srs_from!r}"
                    assert (
                        srs_rcpts == new_rcpts
                    ), f"expected rcpt {new_rcpts!r} and got {srs_rcpts!r}"
                    sys.stderr.write(
                        f"PASS: {database!r},{socket_family!r},{query[0]!r}\n"
                    )
                except AssertionError as e:
                    sys.stderr.write(
                        f"*** FAIL: {database!r},{socket_family!r},{query[0]!r}: {str(e)}\n"
                    )
                    return False
    return True


def milter_protocol_violations(
    postsrsd: str, when: str, socket_family: SocketFamily = SocketFamily.UNIX
):

    def duplicate_optneg(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "initial milter option negotiation failed"
        milter_write(sock_stream, struct.pack(">cLLL", b"O", 6, 0xFF, 0xFF))
        code = milter_read(sock_stream)[:1]
        assert (
            code == b"t"
        ), "milter should have temp-failed on repeated option negotiation"

    def no_optneg(sock_stream: SockStream):
        code = mf_envfrom(sock_stream, "sender@jumps-the-gun.com>")
        assert code == b"t", "milter should have temp-failed"

    def no_mail_command(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        code = mf_rcptto(sock_stream, "somewhere@over-the-rainbow.com")
        assert code == b"t", "milter should have temp-failed"

    def no_rcpt_command(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        code = mf_envfrom(sock_stream, "sender@jumps-the-gun.com")
        assert code == b"c", "milter should have continued"
        code, _, _ = mf_eom(sock_stream)
        assert code == b"t", "milter should have temp-failed"

    def send_mail_command_twice(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        code = mf_envfrom(sock_stream, "sender@example.com")
        assert code == b"c", "milter should have continued"
        code = mf_envfrom(sock_stream, "sender2@example.com")
        assert code == b"t", "milter should have temp-failed"

    def unsupported_chgfrom_action(sock_stream: SockStream):
        milter_write(sock_stream, struct.pack(">cLLL", b"O", 6, 0x3F, 0xFF))
        code = milter_read(sock_stream)[:1]
        assert code == b"t", "milter should have temp-failed"

    def unsupported_addrcpt_action(sock_stream: SockStream):
        milter_write(sock_stream, struct.pack(">cLLL", b"O", 6, 0xFB, 0xFF))
        code = milter_read(sock_stream)[:1]
        assert code == b"t", "milter should have temp-failed"

    def unsupported_delrcpt_action(sock_stream: SockStream):
        milter_write(sock_stream, struct.pack(">cLLL", b"O", 6, 0xF7, 0xFF))
        code = milter_read(sock_stream)[:1]
        assert code == b"t", "milter should have temp-failed"

    def missing_null_terminators(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        milter_write(sock_stream, b"M<sender@otherdomain.com>")
        code = milter_read(sock_stream)[:1]
        assert code == b"c", "milter should have continued"
        milter_write(sock_stream, b"R<SRS0=vmyz=2W=otherdomain.com=test@example.com>")
        code = milter_read(sock_stream)[:1]
        assert code == b"c", "milter should have continued"
        code, new_from, new_rcpt = mf_eom(sock_stream)
        assert code == b"a", f"milter should have accepted"
        assert (
            new_from == "SRS0=9KJ-=2W=otherdomain.com=sender@example.com"
        ), f"unexpected rewrite of envelope sender: {new_from!r}"
        assert new_rcpt == [
            "test@otherdomain.com"
        ], f"unexpected rewrite of recipient: {new_rcpt!r}"

    def oversized_mail_command(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        milter_write(sock_stream, b"M<" + (b"a" * 509) + b">\x00")
        code = milter_read(sock_stream)[:1]
        assert code == b"r", "milter should have rejected"

    def malformed_mail_command(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        milter_write(sock_stream, b"M>test@example.com<")
        code = milter_read(sock_stream)[:1]
        assert code == b"r", "milter should have rejected"

    def aborted_transaction(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        code = mf_envfrom(sock_stream, "mail@example.com")
        assert code == b"c", "milter should have continued"
        code = mf_rcptto(sock_stream, "recipient@example.com")
        assert code == b"c", "milter should have continued"
        milter_write(sock_stream, b"A")
        code = mf_envfrom(sock_stream, "sender@otherdomain.com")
        assert code == b"c", "milter should have continued"
        code = mf_rcptto(sock_stream, "SRS0=vmyz=2W=otherdomain.com=test@example.com")
        assert code == b"c", "milter should have continued"
        code, new_from, new_rcpt = mf_eom(sock_stream)
        assert code == b"a", f"milter should have accepted"
        assert (
            new_from == "SRS0=9KJ-=2W=otherdomain.com=sender@example.com"
        ), f"unexpected rewrite of envelope sender: {new_from!r}"
        assert new_rcpt == [
            "test@otherdomain.com"
        ], f"unexpected rewrite of recipient: {new_rcpt!r}"

    def close_on_quit(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        milter_write(sock_stream, b"Q")
        try:
            mf_envfrom(sock_stream, "mail@example.com")
            raise AssertionError("milter should have disconnected")
        except (ConnectionError, TimeoutError):
            pass

    def oversized_rcpt_command(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        assert mf_envfrom(sock_stream, "a" * 508), "milter MAIL command failed"
        milter_write(sock_stream, b"R<" + (b"a" * 509) + b">\x00")
        code = milter_read(sock_stream)[:1]
        assert code == b"r", "milter should have rejected"

    def malformed_rcpt_command(sock_stream: SockStream):
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        assert mf_envfrom(sock_stream, "a" * 508), "milter MAIL command failed"
        milter_write(sock_stream, b"R>recipient@otherdomain.com<")
        code = milter_read(sock_stream)[:1]
        assert code == b"c", "milter should have continued"
        code, _, _ = mf_eom(sock_stream)
        assert code == b"r", "milter should have rejected"

    def send_garbage_first(sock_stream: SockStream):
        sock_stream.write(b"\x00\x00\xff\xff" + b"!" * 65535)
        assert mf_optneg(sock_stream), "milter option negotiation failed"
        code = mf_envfrom(sock_stream, "sender@otherdomain.com")
        assert code == b"c", "milter should have continued"
        code = mf_rcptto(sock_stream, "SRS0=vmyz=2W=otherdomain.com=test@example.com")
        assert code == b"c", "milter should have continued"
        code, new_from, new_rcpt = mf_eom(sock_stream)
        assert code == b"a", f"milter should have accepted"
        assert (
            new_from == "SRS0=9KJ-=2W=otherdomain.com=sender@example.com"
        ), f"unexpected rewrite of envelope sender: {new_from!r}"
        assert new_rcpt == [
            "test@otherdomain.com"
        ], f"unexpected rewrite of recipient: {new_rcpt!r}"

    def keep_alive_timeout(sock_stream: SockStream):
        time.sleep(2)
        try:
            mf_optneg(sock_stream)
            raise AssertionError("milter should have disconnected")
        except (ConnectionError, TimeoutError):
            pass

    with PostSRSd(
        postsrsd, when=when, socket_family=socket_family, socket_type=SocketType.MILTER
    ) as daemon:
        for test_func in [
            duplicate_optneg,
            no_optneg,
            no_mail_command,
            no_rcpt_command,
            send_mail_command_twice,
            unsupported_chgfrom_action,
            unsupported_addrcpt_action,
            unsupported_delrcpt_action,
            oversized_mail_command,
            malformed_mail_command,
            oversized_rcpt_command,
            malformed_rcpt_command,
            missing_null_terminators,
            aborted_transaction,
            close_on_quit,
            keep_alive_timeout,
            send_garbage_first,
        ]:
            with daemon.connect_stream() as sock_stream:
                try:
                    test_func(sock_stream)
                    sys.stderr.write(f"PASS: {test_func.__name__}\n")
                except AssertionError as e:
                    sys.stderr.write(f"*** FAIL: {test_func.__name__}: {str(e)}\n")
                    return False
                except RuntimeError as e:
                    sys.stderr.write(
                        f"*** FAIL: {test_func.__name__}: {e.__class__.__name__}: {str(e)}\n"
                    )
                    return False
    return True


STATELESS_QUERIES: list[
    tuple[tuple[str, list[str]], tuple[bytes, str | None, list[str] | None]]
] = [
    # No rewrite for local domain
    (("sender@example.com", ["recipient@example.com"]), (b"a", None, None)),
    # No rewrite if recipient is local
    (
        ("sender@otherdomain.com", ["recipient@example.com"]),
        (b"a", None, None),
    ),
    # Regular rewrite
    (
        ("sender@otherdomain.com", ["recipient@thirddomain.com"]),
        (b"a", "SRS0=9KJ-=2W=otherdomain.com=sender@example.com", None),
    ),
    # Regular rewrite with multiple recipients
    (
        (
            "sender@otherdomain.com",
            [
                "recipient@firstdomain.com",
                "recipient@seconddomain.com",
                "recipient@thirddomain.com",
            ],
        ),
        (b"a", "SRS0=9KJ-=2W=otherdomain.com=sender@example.com", None),
    ),
    # No rewrite for sender without domain
    (
        ("foo", ["recipient@thirddomain.com"]),
        (b"a", None, None),
    ),
    # Treat recipient without domain as local
    (
        ("sender@otherdomain.com", ["bar"]),
        (b"a", None, None),
    ),
    # Convert foreign SRS0 address to SRS1 address
    (
        ("SRS0=opaque+string@otherdomain.com", ["recipient@thirddomain.com"]),
        (b"a", "SRS1=chaI=otherdomain.com==opaque+string@example.com", None),
    ),
    # Change domain part of foreign SRS1 address
    (
        (
            "SRS1=X=thirddomain.com==opaque+string@otherdomain.com",
            ["recipient@thirddomain.com"],
        ),
        (b"a", "SRS1=JIBX=thirddomain.com==opaque+string@example.com", None),
    ),
    # Recover original mail address from valid SRS0 address
    (
        (
            "sender@example.com",
            ["SRS0=9KJ-=2W=otherdomain.com=sender@example.com"],
        ),
        (b"a", None, ["sender@otherdomain.com"]),
    ),
    # Handle reverse rewrites for multiple recipients
    (
        (
            "sender@example.com",
            [
                "SRS0=9KJ-=2W=otherdomain.com=sender@example.com",
                "recipient@example.com",
                "SRS1=chaI=otherdomain.com==opaque+string@example.com",
            ],
        ),
        (b"a", None, ["sender@otherdomain.com", "SRS0=opaque+string@otherdomain.com"]),
    ),
    # Rewrite sender if recipient turns out to be non-local
    (
        (
            "sender@otherdomain.com",
            ["SRS0=9KJ-=2W=otherdomain.com=sender@example.com"],
        ),
        (
            b"a",
            "SRS0=9KJ-=2W=otherdomain.com=sender@example.com",
            ["sender@otherdomain.com"],
        ),
    ),
    # Recover original SRS0 address from valid SRS1 address
    (
        (
            "sender@example.com",
            ["SRS1=JIBX=thirddomain.com==opaque+string@example.com"],
        ),
        (b"a", None, ["SRS0=opaque+string@thirddomain.com"]),
    ),
    # Reject valid SRS0 address with time stamp older than 6 months
    (
        (
            "sender@otherdomain.com",
            ["SRS0=te87=T7=otherdomain.com=test@example.com"],
        ),
        (b"r", None, None),
    ),
    # Reject valid SRS0 address with time stamp 6 months in the future
    (
        (
            "sender@otherdomain.com",
            ["SRS0=VcIb=7N=otherdomain.com=test@example.com"],
        ),
        (b"r", None, None),
    ),
    # Recover mail address from all-lowercase SRS0 address
    (
        (
            "sender@example.com",
            ["srs0=xjo9=2v=otherdomain.com=test@example.com"],
        ),
        (b"a", None, ["test@otherdomain.com"]),
    ),
    # Recover mail address from all-uppercase SRS0 address
    (
        (
            "sender@example.com",
            ["SRS0=XJO9=2V=OTHERDOMAIN.COM=TEST@EXAMPLE.COM"],
        ),
        (b"a", None, ["TEST@OTHERDOMAIN.COM"]),
    ),
    # Accept SRS0 address with invalid hash but from a different signer
    (
        (
            "sender@example.com",
            ["SRS0=FAKE=2V=otherdomain.com=test@foreign-signer.com"],
        ),
        (b"a", None, None),
    ),
    # Accept SRS1 address with invalid hash but from a different signer
    (
        (
            "sender@example.com",
            ["SRS1=FAKE=otherdomain.com==opaque+string@foreign-signer.com"],
        ),
        (b"a", None, None),
    ),
    # Reject SRS0 address with invalid hash
    (
        (
            "sender@otherdomain.com",
            ["SRS0=FAKE=2V=otherdomain.com=test@example.com"],
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without authenticating hash
    (
        (
            "sender@otherdomain.com",
            ["SRS0=@example.com"],
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without time stamp
    (
        (
            "sender@otherdomain.com",
            ["SRS0=XjO9@example.com"],
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without original domain
    (
        (
            "sender@otherdomain.com",
            ["SRS0=XjO9=2V@example.com"],
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without original localpart
    (
        (
            "sender@otherdomain.com",
            ["SRS0=XjO9=2V=otherdomain.com@example.com"],
        ),
        (b"r", None, None),
    ),
    # Reject database alias without database backend
    (
        (
            "sender@otherdomain.com",
            ["SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com"],
        ),
        (b"r", None, None),
    ),
    # Test long address
    (
        (
            "test@" + "a" * (508 - 9) + ".net",
            ["recipient@otherdomain.com"],
        ),
        (b"a", "SRS0=ckoK=2W=" + "a" * (508 - 9) + ".net=test@example.com", None),
    ),
    # Recover long address
    (
        (
            "sender@example.com",
            ["SRS0=845K=2W=" + "a" * (508 - 34) + ".net=test@example.com"],
        ),
        (b"a", None, ["test@" + "a" * (508 - 34) + ".net"]),
    ),
    # Test too long address
    (
        (
            "test@" + "a" * (509 - 9) + ".net",
            ["recipient@otherdomain.com"],
        ),
        (b"r", None, None),
    ),
    # Handle bounce mail (empty sender)
    (
        (
            "",
            ["SRS0=9KJ-=2W=otherdomain.com=sender@example.com"],
        ),
        (b"a", None, ["sender@otherdomain.com"]),
    ),
]

DATABASE_QUERIES: list[
    tuple[tuple[str, list[str]], tuple[bytes, str | None, list[str] | None]]
] = [
    # Regular rewrite
    (
        ("test@otherdomain.com", ["recipient@thirddomain.com"]),
        (b"a", "SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com", None),
    ),
    # Recover address from alias
    (
        (
            "sender@example.com",
            ["SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com"],
        ),
        (b"a", None, ["test@otherdomain.com"]),
    ),
    # Recover address from case-munged alias
    (
        (
            "sender@example.com",
            ["SRS0=bxzH=2W=1=dcjgde6n24lcrt41a4t0g1uif0dtkkqj@example.com"],
        ),
        (b"a", None, ["test@otherdomain.com"]),
    ),
    # Reject unknown alias
    (
        (
            "sender@example.com",
            ["SRS0=hdxW=2W=1=VVVVVVUNVVVVVVS1VVVVVVUIVVVTKKQJ@example.com"],
        ),
        (b"r", None, None),
    ),
    # No rewrite for SRS address which is already in the local domain
    (
        (
            "SRS0=XjO9=2V=otherdomain.com=test@example.com",
            ["recipient@otherdomain.com"],
        ),
        (b"a", None, None),
    ),
    # Convert foreign SRS0 address to SRS1 address
    (
        ("SRS0=opaque+string@otherdomain.com", ["recipient@thirddomain.com"]),
        (b"a", "SRS1=chaI=otherdomain.com==opaque+string@example.com", None),
    ),
    # Change domain part of foreign SRS1 address
    (
        (
            "SRS1=X=thirddomain.com==opaque+string@otherdomain.com",
            ["recipient@thirddomain.com"],
        ),
        (b"a", "SRS1=JIBX=thirddomain.com==opaque+string@example.com", None),
    ),
    # Accept SRS0 address with invalid hash but from a different signer
    (
        (
            "sender@example.com",
            ["SRS0=FAKE=2V=otherdomain.com=test@foreign-signer.com"],
        ),
        (b"a", None, None),
    ),
    # Accept SRS1 address with invalid hash but from a different signer
    (
        (
            "sender@example.com",
            ["SRS1=FAKE=otherdomain.com==opaque+string@foreign-signer.com"],
        ),
        (b"a", None, None),
    ),
    # Recover original mail address from valid SRS0 address
    (
        (
            "sender@example.com",
            ["SRS0=9KJ-=2W=otherdomain.com=sender@example.com"],
        ),
        (b"a", None, ["sender@otherdomain.com"]),
    ),
    # Handle bounce mail (empty sender)
    (
        (
            "",
            ["SRS0=9KJ-=2W=otherdomain.com=sender@example.com"],
        ),
        (b"a", None, ["sender@otherdomain.com"]),
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
        if not milter_protocol_violations(
            sys.argv[1], when="1577836860", socket_family=socket_family
        ):
            sys.exit(1)
    sys.exit(0)
