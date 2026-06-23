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


def send_milter(sock: socket.socket, code: bytes, data: bytes):
    sock.send(struct.pack(">Lc", len(data) + 1, code) + data)


def recv_milter(sock: socket.socket):
    size = struct.unpack(">L", sock.recv(4))
    data = sock.recv(*size)
    return data[:1], data[1:]


def mf_optneg(sock: socket.socket):
    send_milter(sock, b"O", struct.pack(">LLL", 6, 0xFF, 0xFF))
    code, _ = recv_milter(sock)
    return code == b"O"


def mf_macro(sock: socket.socket, code: bytes):
    send_milter(sock, b"D", code + b"i\x0001234567\x00")


def mf_envfrom(sock: socket.socket, envfrom: str):
    send_milter(sock, b"M", b"<" + envfrom.encode() + b">\x00")
    code, _ = recv_milter(sock)
    return code


def mf_rcptto(sock: socket.socket, rcptto: str):
    send_milter(sock, b"R", b"<" + rcptto.encode() + b">\x00")
    code, _ = recv_milter(sock)
    return code


def mf_eom(sock: socket.socket):
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
    return code, new_from, new_rcpt


@contextlib.contextmanager
def postsrsd_instance(
    postsrsd: str,
    when: str | None = None,
    with_sqlite: bool = False,
    with_redis: bool = False,
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
                "keep-alive = 1\n"
                'chroot-dir = ""\n'
                'unprivileged-user = ""\n'
                f'original-envelope = {"database" if with_sqlite or with_redis else "embedded"}\n'
                f'socketmap = ""\n'
                f'milter = unix:{tmpdir / "postsrsd.sock"}\n'
                f'secrets-file = {tmpdir / "postsrsd.secret"}\n'
                f"envelope-database = {db_uri}\n"
            )
        with open(tmpdir / "postsrsd.secret", "w") as f:
            f.write("tops3cr3t\n")
        if when is not None:
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


def execute_queries(
    postsrsd: str,
    when: str,
    queries: list[tuple[tuple[str, str], tuple[bytes, str | None, str | None]]],
    with_sqlite: bool = False,
    with_redis: bool = False,
):
    with postsrsd_instance(
        postsrsd, when, with_sqlite=with_sqlite, with_redis=with_redis
    ) as endpoint:
        for query in queries:
            orig_from, orig_rcpt = query[0]
            result, new_from, new_rcpt = query[1]
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
            try:
                sock.settimeout(0.5)
                sock.connect(endpoint)
                assert mf_optneg(sock), "milter option negotation failed"
                srs_from = None
                srs_rcpt = None
                mf_macro(sock, b"M")
                srs_result = mf_envfrom(sock, orig_from)
                if srs_result == b"c":
                    mf_macro(sock, b"R")
                    srs_result = mf_rcptto(sock, orig_rcpt)
                    if srs_result == b"c":
                        mf_macro(sock, b"E")
                        srs_result, srs_from, srs_rcpt = mf_eom(sock)
                assert (
                    srs_result == result
                ), f"expected action {result!r} and got {srs_result!r}"
                assert (
                    srs_from == new_from
                ), f"expected from {new_from!r} and got {srs_from!r}"
                assert (
                    srs_rcpt == new_rcpt
                ), f"expected rcpt {new_rcpt!r} and got {srs_rcpt!r}"
                sys.stderr.write(f"PASS: {query[0]}\n")
            except AssertionError as e:
                sys.stderr.write(f"*** FAIL: {query[0]}: {str(e)}\n")
                return False
            finally:
                sock.close()
    return True


def milter_protocol_violations(postsrsd: str, when: str):

    def duplicate_optneg(sock: socket.socket):
        assert mf_optneg(sock), "initial milter option negotiation failed"
        send_milter(sock, b"O", struct.pack(">LLL", 6, 0xFF, 0xFF))
        code, _ = recv_milter(sock)
        assert (
            code == b"t"
        ), "milter should have temp-failed on repeated option negotiation"

    def no_optneg(sock: socket.socket):
        code = mf_envfrom(sock, "sender@jumps-the-gun.com>")
        assert code == b"t", "milter should have temp-failed"

    def no_mail_command(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        code = mf_rcptto(sock, "somewhere@over-the-rainbow.com")
        assert code == b"t", "milter should have temp-failed"

    def no_rcpt_command(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        code = mf_envfrom(sock, "sender@jumps-the-gun.com")
        assert code == b"c", "milter should have continued"
        code, _, _ = mf_eom(sock)
        assert code == b"t", "milter should have temp-failed"

    def send_mail_command_twice(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        code = mf_envfrom(sock, "sender@example.com")
        assert code == b"c", "milter should have continued"
        code = mf_envfrom(sock, "sender2@example.com")
        assert code == b"t", "milter should have temp-failed"

    def unsupported_chgfrom_action(sock: socket.socket):
        send_milter(sock, b"O", struct.pack(">LLL", 6, 0x3F, 0xFF))
        code, _ = recv_milter(sock)
        assert code == b"t", "milter should have temp-failed"

    def unsupported_addrcpt_action(sock: socket.socket):
        send_milter(sock, b"O", struct.pack(">LLL", 6, 0xFB, 0xFF))
        code, _ = recv_milter(sock)
        assert code == b"t", "milter should have temp-failed"

    def unsupported_delrcpt_action(sock: socket.socket):
        send_milter(sock, b"O", struct.pack(">LLL", 6, 0xF7, 0xFF))
        code, _ = recv_milter(sock)
        assert code == b"t", "milter should have temp-failed"

    def missing_null_terminators(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        send_milter(sock, b"M", b"<sender@otherdomain.com>")
        code, _ = recv_milter(sock)
        assert code == b"c", "milter should have continued"
        send_milter(sock, b"R", b"<SRS0=vmyz=2W=otherdomain.com=test@example.com>")
        code, _ = recv_milter(sock)
        assert code == b"c", "milter should have continued"
        code, new_from, new_rcpt = mf_eom(sock)
        assert code == b"a", f"milter should have accepted"
        assert (
            new_from == "SRS0=9KJ+=2W=otherdomain.com=sender@example.com"
        ), f"unexpected rewrite of envelope sender: {new_from!r}"
        assert (
            new_rcpt == "test@otherdomain.com"
        ), f"unexpected rewrite of recipient: {new_rcpt!r}"

    def oversized_mail_command(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        send_milter(sock, b"M", b"<" + (b"a" * 509) + b">\x00")
        code, _ = recv_milter(sock)
        assert code == b"r", "milter should have rejected"

    def malformed_mail_command(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        send_milter(sock, b"M", b">test@example.com<")
        code, _ = recv_milter(sock)
        assert code == b"r", "milter should have rejected"

    def aborted_transaction(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        code = mf_envfrom(sock, "mail@example.com")
        assert code == b"c", "milter should have continued"
        code = mf_rcptto(sock, "recipient@example.com")
        assert code == b"c", "milter should have continued"
        send_milter(sock, b"A", b"")
        code = mf_envfrom(sock, "sender@otherdomain.com")
        assert code == b"c", "milter should have continued"
        code = mf_rcptto(sock, "SRS0=vmyz=2W=otherdomain.com=test@example.com")
        assert code == b"c", "milter should have continued"
        code, new_from, new_rcpt = mf_eom(sock)
        assert code == b"a", f"milter should have accepted"
        assert (
            new_from == "SRS0=9KJ+=2W=otherdomain.com=sender@example.com"
        ), f"unexpected rewrite of envelope sender: {new_from!r}"
        assert (
            new_rcpt == "test@otherdomain.com"
        ), f"unexpected rewrite of recipient: {new_rcpt!r}"

    def close_on_quit(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        send_milter(sock, b"Q", b"")
        try:
            mf_envfrom(sock, "mail@example.com")
            raise AssertionError("milter should have disconnected")
        except (ConnectionError, struct.error, TimeoutError):
            pass

    def oversized_rcpt_command(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        assert mf_envfrom(sock, "a" * 508), "milter MAIL command failed"
        send_milter(sock, b"R", b"<" + (b"a" * 509) + b">\x00")
        code, _ = recv_milter(sock)
        assert code == b"r", "milter should have rejected"

    def malformed_rcpt_command(sock: socket.socket):
        assert mf_optneg(sock), "milter option negotiation failed"
        assert mf_envfrom(sock, "a" * 508), "milter MAIL command failed"
        send_milter(sock, b"R", b">recipient@otherdomain.com<")
        code, _ = recv_milter(sock)
        assert code == b"r", "milter should have rejected"

    def send_garbage_first(sock: socket.socket):
        sock.send(b"\x00\x00\xff\xff" + b"!" * 65535)
        assert mf_optneg(sock), "milter option negotiation failed"
        code = mf_envfrom(sock, "sender@otherdomain.com")
        assert code == b"c", "milter should have continued"
        code = mf_rcptto(sock, "SRS0=vmyz=2W=otherdomain.com=test@example.com")
        assert code == b"c", "milter should have continued"
        code, new_from, new_rcpt = mf_eom(sock)
        assert code == b"a", f"milter should have accepted"
        assert (
            new_from == "SRS0=9KJ+=2W=otherdomain.com=sender@example.com"
        ), f"unexpected rewrite of envelope sender: {new_from!r}"
        assert (
            new_rcpt == "test@otherdomain.com"
        ), f"unexpected rewrite of recipient: {new_rcpt!r}"

    def keep_alive_timeout(sock: socket.socket):
        time.sleep(1.1)
        try:
            mf_optneg(sock)
            raise AssertionError("milter should have disconnected")
        except (ConnectionError, struct.error, TimeoutError):
            pass

    with postsrsd_instance(postsrsd, when=when) as endpoint:
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
            send_garbage_first,
            keep_alive_timeout,
        ]:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM, 0)
            try:
                sock.settimeout(0.5)
                sock.connect(endpoint)
                test_func(sock)
                sys.stderr.write(f"PASS: {test_func.__name__}\n")
            except AssertionError as e:
                sys.stderr.write(f"*** FAIL: {test_func.__name__}: {str(e)}\n")
                return False
            finally:
                sock.close()
    return True


STATELESS_QUERIES: list[
    tuple[tuple[str, str], tuple[bytes, str | None, str | None]]
] = [
    # No rewrite for local domain
    (("sender@example.com", "recipient@example.com"), (b"a", None, None)),
    # No rewrite if recipient is local
    (
        ("sender@otherdomain.com", "recipient@example.com"),
        (b"a", None, None),
    ),
    # Regular rewrite
    (
        ("sender@otherdomain.com", "recipient@thirddomain.com"),
        (b"a", "SRS0=9KJ+=2W=otherdomain.com=sender@example.com", None),
    ),
    # No rewrite for sender without domain
    (
        ("foo", "recipient@thirddomain.com"),
        (b"a", None, None),
    ),
    # Treat recipient without domain as local
    (
        ("sender@otherdomain.com", "bar"),
        (b"a", None, None),
    ),
    # Convert foreign SRS0 address to SRS1 address
    (
        ("SRS0=opaque+string@otherdomain.com", "recipient@thirddomain.com"),
        (b"a", "SRS1=chaI=otherdomain.com==opaque+string@example.com", None),
    ),
    # Change domain part of foreign SRS1 address
    (
        (
            "SRS1=X=thirddomain.com==opaque+string@otherdomain.com",
            "recipient@thirddomain.com",
        ),
        (b"a", "SRS1=JIBX=thirddomain.com==opaque+string@example.com", None),
    ),
    # Recover original mail address from valid SRS0 address
    (
        (
            "sender@example.com",
            "SRS0=9KJ+=2W=otherdomain.com=sender@example.com",
        ),
        (b"a", None, "sender@otherdomain.com"),
    ),
    # Rewrite sender if recipient turns out to be non-local
    (
        (
            "sender@otherdomain.com",
            "SRS0=9KJ+=2W=otherdomain.com=sender@example.com",
        ),
        (
            b"a",
            "SRS0=9KJ+=2W=otherdomain.com=sender@example.com",
            "sender@otherdomain.com",
        ),
    ),
    # Recover original SRS0 address from valid SRS1 address
    (
        (
            "sender@example.com",
            "SRS1=JIBX=thirddomain.com==opaque+string@example.com",
        ),
        (b"a", None, "SRS0=opaque+string@thirddomain.com"),
    ),
    # Reject valid SRS0 address with time stamp older than 6 months
    (
        (
            "sender@otherdomain.com",
            "SRS0=te87=T7=otherdomain.com=test@example.com",
        ),
        (b"r", None, None),
    ),
    # Reject valid SRS0 address with time stamp 6 months in the future
    (
        (
            "sender@otherdomain.com",
            "SRS0=VcIb=7N=otherdomain.com=test@example.com",
        ),
        (b"r", None, None),
    ),
    # Recover mail address from all-lowercase SRS0 address
    (
        (
            "sender@example.com",
            "srs0=xjo9=2v=otherdomain.com=test@example.com",
        ),
        (b"a", None, "test@otherdomain.com"),
    ),
    # Recover mail address from all-uppercase SRS0 address
    (
        (
            "sender@example.com",
            "SRS0=XJO9=2V=OTHERDOMAIN.COM=TEST@EXAMPLE.COM",
        ),
        (b"a", None, "TEST@OTHERDOMAIN.COM"),
    ),
    # Reject SRS0 address with invalid hash
    (
        (
            "sender@otherdomain.com",
            "SRS0=FAKE=2V=otherdomain.com=test@example.com",
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without authenticating hash
    (
        (
            "sender@otherdomain.com",
            "SRS0=@example.com",
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without time stamp
    (
        (
            "sender@otherdomain.com",
            "SRS0=XjO9@example.com",
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without original domain
    (
        (
            "sender@otherdomain.com",
            "SRS0=XjO9=2V@example.com",
        ),
        (b"r", None, None),
    ),
    # Reject SRS0 address without original localpart
    (
        (
            "sender@otherdomain.com",
            "SRS0=XjO9=2V=otherdomain.com@example.com",
        ),
        (b"r", None, None),
    ),
    # Reject database alias without database backend
    (
        (
            "sender@otherdomain.com",
            "SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com",
        ),
        (b"r", None, None),
    ),
    # Test long address
    (
        (
            "test@" + "a" * (508 - 9) + ".net",
            "recipient@otherdomain.com",
        ),
        (b"a", "SRS0=ckoK=2W=" + "a" * (508 - 9) + ".net=test@example.com", None),
    ),
    # Recover long address
    (
        (
            "sender@example.com",
            "SRS0=845K=2W=" + "a" * (508 - 34) + ".net=test@example.com",
        ),
        (b"a", None, "test@" + "a" * (508 - 34) + ".net"),
    ),
    # Test too long address
    (
        (
            "test@" + "a" * (509 - 9) + ".net",
            "recipient@otherdomain.com",
        ),
        (b"r", None, None),
    ),
    # Handle bounce mail (empty sender)
    (
        (
            "",
            "SRS0=9KJ+=2W=otherdomain.com=sender@example.com",
        ),
        (b"a", None, "sender@otherdomain.com"),
    ),
]

DATABASE_QUERIES: list[tuple[tuple[str, str], tuple[bytes, str | None, str | None]]] = [
    # Regular rewrite
    (
        ("test@otherdomain.com", "recipient@thirddomain.com"),
        (b"a", "SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com", None),
    ),
    # Recover address from alias
    (
        (
            "sender@example.com",
            "SRS0=bxzH=2W=1=DCJGDE6N24LCRT41A4T0G1UIF0DTKKQJ@example.com",
        ),
        (b"a", None, "test@otherdomain.com"),
    ),
    # Recover address from case-munged alias
    (
        (
            "sender@example.com",
            "SRS0=bxzH=2W=1=dcjgde6n24lcrt41a4t0g1uif0dtkkqj@example.com",
        ),
        (b"a", None, "test@otherdomain.com"),
    ),
    # Reject unknown alias
    (
        (
            "sender@example.com",
            "SRS0=hdxW=2W=1=VVVVVVUNVVVVVVS1VVVVVVUIVVVTKKQJ@example.com",
        ),
        (b"a", None, None),
    ),
    # No rewrite for SRS address which is already in the local domain
    (
        ("SRS0=XjO9=2V=otherdomain.com=test@example.com", "recipient@otherdomain.com"),
        (b"a", None, None),
    ),
    # Convert foreign SRS0 address to SRS1 address
    (
        ("SRS0=opaque+string@otherdomain.com", "recipient@thirddomain.com"),
        (b"a", "SRS1=chaI=otherdomain.com==opaque+string@example.com", None),
    ),
    # Change domain part of foreign SRS1 address
    (
        (
            "SRS1=X=thirddomain.com==opaque+string@otherdomain.com",
            "recipient@thirddomain.com",
        ),
        (b"a", "SRS1=JIBX=thirddomain.com==opaque+string@example.com", None),
    ),
    # Recover original mail address from valid SRS0 address
    (
        (
            "sender@example.com",
            "SRS0=9KJ+=2W=otherdomain.com=sender@example.com",
        ),
        (b"a", None, "sender@otherdomain.com"),
    ),
]


if __name__ == "__main__":
    if not execute_queries(
        sys.argv[1],
        when="1577836860",  # 2020-01-01 00:01:00 UTC
        queries=STATELESS_QUERIES,
    ):
        sys.exit(1)
    if not milter_protocol_violations(sys.argv[1], when="1577836860"):
        sys.exit(1)
    if sys.argv[2] == "1":
        if not execute_queries(
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
    sys.exit(0)
