"""Measures raw TCP throughput over AWDL, with nobody accepting anything.

On the Mac (the far end), which must have its AirDrop window open so that
awdl0 stays up:
    python3 awdl_speed.py serve
On the PC:
    python awdl_speed.py send <mac's awdl0 address> <megabytes> [awdl0 index]
    python awdl_speed.py fetch <mac's awdl0 address> <megabytes> [awdl0 index]

`send` measures PC to Mac, `fetch` Mac to PC over the same connection, which
the PC opens, so Windows Firewall needs no rule for it. macOS keeps ordinary
sockets off awdl0 unless they ask with SO_RECV_ANYIF.
"""
import socket
import sys
import time

PORT = 5001
SO_RECV_ANYIF = 0x1104
CHUNK = bytes(64 * 1024)


def scope_of(name):
    return socket.if_nametoindex(name)


def serve():
    listener = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.setsockopt(socket.SOL_SOCKET, SO_RECV_ANYIF, 1)
    listener.bind(('::', PORT, 0, 0))
    listener.listen(1)
    while True:
        peer, _ = listener.accept()
        with peer:
            request = peer.recv(16).decode().split()
            if len(request) != 2 or request[0] not in ('send', 'fetch'):
                continue
            total = int(request[1])
            if request[0] == 'fetch':
                left = total
                while left > 0:
                    peer.sendall(CHUNK[:min(left, len(CHUNK))])
                    left -= len(CHUNK)
            else:
                got = 0
                while got < total:
                    data = peer.recv(len(CHUNK))
                    if not data:
                        break
                    got += len(data)
                peer.sendall(b'ok')


def run(mode, address, megabytes, scope):
    total = megabytes * 1024 * 1024
    link = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    link.settimeout(20)
    link.connect((address, PORT, 0, scope))
    link.sendall(f'{mode} {total}'.ljust(16).encode())
    start = time.monotonic()
    if mode == 'send':
        left = total
        while left > 0:
            link.sendall(CHUNK[:min(left, len(CHUNK))])
            left -= len(CHUNK)
        link.recv(2)
    else:
        got = 0
        while got < total:
            data = link.recv(len(CHUNK))
            if not data:
                break
            got += len(data)
        total = got
    seconds = time.monotonic() - start
    print(f'{mode}: {total / 1048576:.1f} MB in {seconds:.1f} s, {total / 1048576 / seconds:.2f} MB/s')


if __name__ == '__main__':
    if sys.argv[1] == 'serve':
        serve()
    else:
        # The interface index of awdl0, which Windows shows in Get-NetAdapter; its name is looked up without one.
        run(sys.argv[1], sys.argv[2], int(sys.argv[3]),
            int(sys.argv[4]) if len(sys.argv) > 4 else scope_of('awdl0'))
