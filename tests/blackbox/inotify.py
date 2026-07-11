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
import itertools
import sys
import time

from testhelper import *


def run_query(sock_stream: SockStream, test_domain: str | None):
    write_netstring(sock_stream, "forward test@otherdomain.com")
    result = read_netstring(sock_stream)
    if not result.startswith("OK SRS0=") or not result.endswith(
        f"=otherdomain.com=test@{test_domain}"
    ):
        raise AssertionError(
            f"expected 'OK SRS0=...=otherdomain.com=test@{test_domain}', got {result!r}"
        )


if __name__ == "__main__":
    with PostSRSd(sys.argv[1], use_file_watch=True) as daemon:
        sock = daemon.connect()
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
                    with open(daemon.domains_file, "w") as f:
                        f.write(f"{test_domain}\n")
                elif counter % 3 == 1:
                    sys.stderr.write(
                        "Renaming different file to replace the domain file\n"
                    )
                    tmp_file = pathlib.Path(str(daemon.domains_file) + ".tmp")
                    with open(tmp_file, "w") as f:
                        f.write(f"{test_domain}\n")
                    tmp_file.rename(daemon.domains_file)
                else:
                    sys.stderr.write(
                        "Deleting old domains file and creating a new one\n"
                    )
                    daemon.domains_file.unlink()
                    with open(daemon.domains_file, "w") as f:
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
                sock = daemon.connect()
                sock_stream = SockStream(sock)
                run_query(sock_stream, test_domain)
                sys.stderr.write(f"PASS: inotify test {counter + 1} [{test_domain}]\n")
                previous_domain = test_domain
                if counter == 8:
                    break
        except Exception as e:
            sys.stderr.write(f"*** FAIL: {e.__class__.__name__}: {str(e)}\n")
            sys.exit(1)
        finally:
            sock.close()
    sys.exit(0)
