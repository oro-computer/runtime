"""Local WebUSB test peripheral for Linux vhci_hcd. Run in a disposable Linux VM."""

import errno
import socket
import struct
from pathlib import Path


CONTROLLER = Path("/sys/devices/platform/vhci_hcd.0")
DEVICE = struct.pack("<BBHBBBBHHHBBBB", 18, 1, 0x0200, 0, 0, 0, 64,
                     0xcafe, 0x4030, 0x0100, 1, 2, 3, 1)
INTERFACE = bytes([9, 4, 0, 0, 0, 255, 0x53, 1, 0,
                   9, 4, 0, 1, 6, 255, 0x53, 1, 0])
ENDPOINTS = b"".join(
    struct.pack("<BBBBHB", 7, 5, direction | number, kind, 64, interval)
    for number, kind, interval in [(1, 2, 0), (2, 3, 1), (3, 1, 1)]
    for direction in (0, 0x80)
)
CONFIG = struct.pack("<BBHBBBBB", 9, 2, 9 + len(INTERFACE) + len(ENDPOINTS),
                     1, 1, 0, 0x80, 50) + INTERFACE + ENDPOINTS
STRINGS = ("", "Oro", "WebUSB endpoint fixture", "ORO-ENDPOINT-1")
HEADER = struct.Struct("!IIIII")
SUBMIT = struct.Struct("!Iiiii8s")
RESULT = struct.Struct("!iiiii8x")
PACKET = struct.Struct("!IIIi")


def descriptor(kind, index):
    if index == 0 and kind in (1, 2):
        return DEVICE if kind == 1 else CONFIG
    if kind == 3 and index < len(STRINGS):
        data = STRINGS[index].encode("utf-16-le") if index else bytes([9, 4])
        return bytes([len(data) + 2, 3]) + data
    return None


class Peripheral:
    def __init__(self):
        self.buffer = {}
        self.iso_packets = []
        self.halted = False
        self.configuration = 0
        self.alternate = 0
        self.disconnect_on_iso_in = False

    def control(self, setup):
        kind, request, value, index, length = struct.unpack("<BBHHH", setup)
        if (kind, request) == (0x80, 6):
            data = descriptor(value >> 8, value & 255)
            if data is not None:
                return 0, data[:length]
        if kind == 0 and request in (5, 9):
            if request == 9:
                self.disconnect_on_iso_in = False
                self.configuration = value
                self.alternate = 0
                self.halted = False
                self.buffer.clear()
                self.iso_packets.clear()
            return 0, b""
        if (kind, request) == (0x80, 8):
            return 0, bytes([self.configuration])
        if (kind, request, index) == (1, 11, 0) and value in (0, 1):
            self.disconnect_on_iso_in = False
            self.alternate = value
            self.halted = False
            self.buffer.clear()
            self.iso_packets.clear()
            return 0, b""
        if (kind, request, index) == (0x81, 10, 0):
            return 0, bytes([self.alternate])
        if request == 0 and kind in (0x80, 0x81, 0x82):
            return 0, bytes([int(kind == 0x82 and index == 0x81 and self.halted), 0])[:length]
        if self.alternate == 1 and (kind, request, value, index, length) == (0x41, 0x40, 0, 0, 0):
            self.halted = True
            return 0, b""
        if self.alternate == 1 and (kind, request, value, index, length) == (0x41, 0x41, 0, 0, 0):
            self.disconnect_on_iso_in = True
            return 0, b""
        if (kind, request, value, index, length) == (2, 1, 0, 0x81, 0):
            self.halted = False
            return 0, b""
        return -errno.EPIPE, b""

    def transfer(self, endpoint, incoming, size, data, packets, setup):
        if endpoint == 0:
            status, answer = self.control(setup)
            return status, len(answer), answer, []
        if self.alternate != 1:
            return -errno.EPIPE, 0, b"", []
        if endpoint in (1, 2):
            if incoming and endpoint == 1 and self.halted:
                return -errno.EPIPE, 0, b"", []
            if incoming:
                answer = self.buffer.pop(endpoint, b"")[:size]
                return 0, len(answer), answer, []
            self.buffer[endpoint] = data
            return 0, size, b"", []
        if endpoint != 3:
            return -errno.EPIPE, 0, b"", []
        if not incoming:
            self.iso_packets = [data[offset:offset + length] for offset, length, _, _ in packets]
        reply = []
        pieces = []
        for i, (offset, length, _, _) in enumerate(packets):
            chunk = self.iso_packets[i][:length] if i < len(self.iso_packets) else b""
            reply.append((offset, length, len(chunk), 0))
            pieces.append(chunk)
        if incoming:
            self.iso_packets.clear()
        return 0, sum(len(chunk) for chunk in pieces), b"".join(pieces) if incoming else b"", reply


def serve(stream, peripheral):
    def read(size):
        data = stream.read(size)
        if len(data) != size:
            raise EOFError("USB controller disconnected")
        return data

    while True:
        command, seq, _, direction, endpoint = HEADER.unpack(read(HEADER.size))
        body = read(SUBMIT.size)
        if command == 2:  # All submissions complete synchronously before the next unlink.
            stream.write(HEADER.pack(4, seq, 0, 0, 0) + bytes(SUBMIT.size))
            stream.flush()
            continue
        if command != 1:
            raise ValueError("Unexpected USB/IP command")
        _, size, frame, count, _, setup = SUBMIT.unpack(body)
        if not 0 <= size <= 65535 or not -1 <= count <= 1024:
            raise ValueError("Request exceeds fixture limits")
        data = read(size) if direction == 0 else b""
        packets = [PACKET.unpack(read(PACKET.size)) for _ in range(max(0, count))]
        if peripheral.disconnect_on_iso_in and endpoint == 3 and direction == 1 and packets:
            print("Disconnecting with an isochronous IN request pending", flush=True)
            return
        status, actual, payload, reply = peripheral.transfer(endpoint, direction, size, data, packets, setup)
        response = HEADER.pack(3, seq, 0, 0, 0) + RESULT.pack(status, actual, frame, len(reply), 0)
        stream.write(response + payload + b"".join(PACKET.pack(*p) for p in reply))
        stream.flush()


def main():
    ports = [line.split() for line in (CONTROLLER / "status").read_text().splitlines()[1:]]
    port = next((int(row[1]) for row in ports if row[0] == "hs" and int(row[2]) == 4), None)
    if port is None:
        raise RuntimeError("No unused full-speed-capable vhci port")
    # A private connected socket pair keeps all traffic on this machine.
    controller, peer = socket.socketpair()
    with controller, peer, peer.makefile("rwb") as stream:
        (CONTROLLER / "attach").write_text(f"{port} {controller.fileno()} 65537 2\n")
        print(f"Attached WebUSB endpoint fixture on virtual port {port}", flush=True)
        try:
            serve(stream, Peripheral())
        except (EOFError, BrokenPipeError, ConnectionResetError):
            pass


if __name__ == "__main__":
    main()
