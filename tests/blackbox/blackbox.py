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
            for nr, query in enumerate(queries):
                write_netstring(sock, query[0])
                result = read_netstring(sock)
                assert result == query[1]
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
            ("forward test@example.com", "NOTFOUND "),
            (
                "forward test@otherdomain.com",
                "OK SRS0=vmyz=2W=otherdomain.com=test@example.com",
            ),
        ],
    )
    sys.exit(0)
