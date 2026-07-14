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
import enum
import os
import pathlib
import signal
import socket
import subprocess
import tempfile
import threading


class SocketType(enum.Enum):
    SOCKETMAP = 1
    MILTER = 2


class SocketFamily(enum.Enum):
    UNIX = 1
    IP = 2


class Database(enum.Enum):
    NONE = 0
    SQLITE = 1
    REDIS = 2


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
                raise ConnectionResetError("no data")
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
        raise ValueError("invalid netstring")
    data = sock_stream.read(data_size)
    comma = sock_stream.read(1)
    if comma != b",":
        raise ValueError("invalid netstring")
    return data.decode()


class PostSRSd:
    def __init__(
        self,
        executable: str,
        when: str | None = None,
        database: Database = Database.NONE,
        socket_family: SocketFamily = SocketFamily.UNIX,
        socket_type: SocketType = SocketType.SOCKETMAP,
        use_file_watch: bool = False,
    ):
        self._executable = executable
        self._when = when
        self._tmpdir = tempfile.TemporaryDirectory()
        self._tmpdir_path = pathlib.Path(self._tmpdir.name)
        self._proc: subprocess.Popen[bytes] | None = None
        self._notify_sock: socket.socket | None = None
        with contextlib.ExitStack() as on_failure:
            on_failure.push(self)
            socketmap_endpoint = {SocketType.SOCKETMAP: "", SocketType.MILTER: ""}
            if socket_family == SocketFamily.UNIX:
                self._family = socket.AF_UNIX
                self._addr = str(self._tmpdir_path / "postsrsd.sock")
                socketmap_endpoint[socket_type] = (
                    f'unix:{self._tmpdir_path / "postsrsd.sock"}'
                )
            elif socket_family == SocketFamily.IP:
                self._family = socket.AF_INET
                self._addr = ("127.0.0.1", 12345)
                socketmap_endpoint[socket_type] = "inet:127.0.0.1:12345"
            if database == Database.SQLITE:
                database_uri = f'sqlite:{self._tmpdir_path / "postsrsd.db"}'
            elif database == Database.REDIS:
                database_uri = "redis:localhost:6379"
            else:
                database_uri = ""
            with open(self._tmpdir_path / "postsrsd.conf", "w") as f:
                f.write(
                    f'domains-file = "{self._tmpdir_path / "postsrsd.domains"}"\n'
                    f'domains-file-watch = {"on" if use_file_watch else "off"}\n'
                    "keep-alive = 1\n"
                    'chroot-dir = ""\n'
                    'unprivileged-user = ""\n'
                    f'original-envelope = {"embedded" if database == Database.NONE else "database"}\n'
                    f'envelope-database = "{database_uri}"\n'
                    f'secrets-file = "{self._tmpdir_path / "postsrsd.secret"}"\n'
                    f'socketmap = "{socketmap_endpoint[SocketType.SOCKETMAP]}"\n'
                    f'milter = "{socketmap_endpoint[SocketType.MILTER]}"\n'
                )
            with open(self._tmpdir_path / "postsrsd.secret", "w") as f:
                f.write("tops3cr3t\n")
            with open(self._tmpdir_path / "postsrsd.domains", "w") as f:
                f.write("example.com\n")
            self._notify_addr = str(self._tmpdir_path / "postsrsd.notify")
            self._notify_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM, 0)
            self._notify_sock.settimeout(1)
            self._notify_sock.bind(self._notify_addr)
            self._ready = 0
            self._notify_thread = threading.Thread(target=self._notify_thread_run)
            self._notify_cv = threading.Condition(lock=threading.Lock())
            self._notify_thread.start()
            on_failure.pop_all()

    def _notify_thread_run(self):
        try:
            while self._notify_sock is not None:
                try:
                    data = self._notify_sock.recv(4096)
                    if b"READY=1" in data:
                        with self._notify_cv:
                            self._ready += 1
                            self._notify_cv.notify()
                except TimeoutError:
                    pass
        except OSError:
            pass

    def __enter__(self) -> "PostSRSd":
        with contextlib.ExitStack() as on_failure:
            on_failure.push(self)
            env = os.environ.copy()
            env["NOTIFY_SOCKET"] = self._notify_addr
            if self._when is not None:
                env["POSTSRSD_FAKETIME"] = self._when
            self._proc = subprocess.Popen(
                [self._executable, "-C", str(self._tmpdir_path / "postsrsd.conf")],
                start_new_session=True,
                env=env,
            )
            with self._notify_cv:
                if not self._notify_cv.wait_for(
                    lambda: self._ready > 0
                    or (self._proc is not None and self._proc.poll() is not None),
                    5.0,
                ):
                    raise RuntimeError("PostSRSd daemon failed to notify")
            retcode = self._proc.poll()
            if retcode is not None:
                raise RuntimeError(f"PostSRSd daemon failed with exit code {retcode}")
            on_failure.pop_all()
            return self

    def __exit__(self, *exc_args) -> bool:  # type: ignore
        self.cleanup()
        return False

    @property
    def domains_file(self) -> pathlib.Path:
        return self._tmpdir_path / "postsrsd.domains"

    def cleanup(self):
        if self._notify_sock is not None:
            self._notify_sock.close()
            self._notify_sock = None
        if self._proc is not None:
            try:
                try:
                    os.killpg(os.getpgid(self._proc.pid), signal.SIGTERM)
                except PermissionError:
                    os.kill(self._proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self._proc.wait(5)
            except subprocess.TimeoutExpired:
                print("(PostSRSd does not terminate, escalating to SIGKILL)")
                try:
                    os.killpg(os.getpgid(self._proc.pid), signal.SIGKILL)
                except PermissionError:
                    os.kill(self._proc.pid, signal.SIGKILL)
                self._proc.wait()
        self._tmpdir.cleanup()

    def connect(self) -> socket.socket:
        assert self._proc is not None, "cannot reload daemon if it is not running"
        retcode = self._proc.poll()
        while retcode is None:
            try:
                sock = socket.socket(self._family, socket.SOCK_STREAM, 0)
                sock.settimeout(0.5)
                sock.connect(self._addr)
                return sock
            except ConnectionRefusedError:
                print("(ignoring ConnectionRefusedError)")
            retcode = self._proc.poll()
        raise RuntimeError(f"PostSRSd daemon failed with exit code {retcode}")

    def reload(self):
        assert self._proc is not None, "cannot reload daemon if it is not running"
        retcode = self._proc.poll()
        if retcode is not None:
            raise RuntimeError(f"PostSRSd daemon failed with exit code {retcode}")
        os.kill(self._proc.pid, signal.SIGHUP)
