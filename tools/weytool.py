#!/usr/bin/env python3
"""
weytool.py - list, read, write and delete files on a Weytec MK06 keyboard.

Python port of weytool (https://github.com/funkenzuzzler/weytools), so it also
runs on Windows. Talks to the keyboard over its own USB connection (default)
or over the serial Host Port A (-D COM3 / -D /dev/ttyUSB0).

Install:
    pip install pyusb libusb-package     # USB
    pip install pyserial                 # only for -D (serial)

Windows: the keyboard's second USB interface (Interface 1) needs the WinUSB
driver; install it with Zadig. Leave Interface 0 (the keyboard itself) alone.

Examples:
    python weytool.py -l
    python weytool.py -r 8,10
    python weytool.py -w 8,10,KCT10_PCSTR110.kct
    python weytool.py -w LAYER10.LAY
    python weytool.py --rawcmd 7f,e8 --rawrx 3
"""
import argparse
import os
import re
import struct
import sys
import time

VID, PID = 0x0744, 0x003F
EP_OUT, EP_IN = 0x06, 0x85
USB_IFACE = 1
TIMEOUT_MS = 60000
SERIAL_IDLE_S = 10  # give up when the serial line is silent this long

CMD_READGRAPH = 0xA3
CMD_WRITEFILE = 0xA5
CMD_READFILE = 0xA6
CMD_DELETE = 0xA8
CMD_LISTFILES = 0xA9
STATUS_OK = 0xD000

VERBOSE = False


def hexdump(prefix, data):
    if not VERBOSE:
        return
    for off in range(0, len(data), 16):
        chunk = data[off:off + 16]
        hx = " ".join(f"{b:02X}" for b in chunk)
        txt = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        print(f"{prefix}: {off:04x}: {hx:<48} {txt}", file=sys.stderr)


class WeyError(Exception):
    pass


# --------------------------------------------------------------------------
# Transports
# --------------------------------------------------------------------------

class UsbLink:
    def __init__(self):
        try:
            import usb.core
            import usb.util
        except ImportError:
            raise WeyError("pyusb is missing: pip install pyusb libusb-package")
        backend = None
        try:
            import libusb_package  # ships libusb-1.0.dll on Windows
            backend = libusb_package.get_libusb1_backend()
        except ImportError:
            pass
        self.usb = usb
        dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend)
        if dev is None:
            raise WeyError(f"keyboard {VID:04x}:{PID:04x} not found on USB "
                           "(Windows: is WinUSB installed on Interface 1 with Zadig?)")
        try:
            if dev.is_kernel_driver_active(USB_IFACE):
                dev.detach_kernel_driver(USB_IFACE)
        except (NotImplementedError, usb.core.USBError):
            pass
        if sys.platform != "win32":
            # Windows cannot (and need not) select the configuration of a
            # composite device; the keyboard is already configured anyway.
            try:
                dev.set_configuration(1)
            except (NotImplementedError, usb.core.USBError):
                pass
        try:
            usb.util.claim_interface(dev, USB_IFACE)
        except (NotImplementedError, usb.core.USBError) as e:
            hint = ""
            if sys.platform == "win32":
                hint = ("\nWindows: install the WinUSB driver on the keyboard's Interface 1 with Zadig "
                        "(Options > List All Devices, pick the 0744:003F entry ending in 'Interface 1', "
                        "WinUSB, Replace Driver). Do not change Interface 0.")
            elif isinstance(e, usb.core.USBError) and e.errno == 13:
                hint = "\nLinux: run with sudo or add a udev rule for 0744:003f."
            raise WeyError(f"cannot open the keyboard's USB interface {USB_IFACE}: {e}{hint}")
        self.dev = dev
        self.rx = bytearray()
        # Switch the keyboard's file protocol to USB.
        self.write(b"\x7f\xf0mode-usb")
        time.sleep(1)

    def write(self, data):
        hexdump("TX", data)
        for off in range(0, len(data), 64):
            self.dev.write(EP_OUT, data[off:off + 64], timeout=TIMEOUT_MS)

    def read(self, count):
        while len(self.rx) < count:
            try:
                self.rx += bytes(self.dev.read(EP_IN, 4096, timeout=TIMEOUT_MS))
            except self.usb.core.USBTimeoutError:
                raise WeyError("timeout waiting for the keyboard")
        out, self.rx = bytes(self.rx[:count]), self.rx[count:]
        hexdump("RX", out)
        return out

    def close(self):
        self.usb.util.release_interface(self.dev, USB_IFACE)
        self.usb.util.dispose_resources(self.dev)


class SerialLink:
    def __init__(self, port, baud):
        try:
            import serial
        except ImportError:
            raise WeyError("pyserial is missing: pip install pyserial")
        self.s = serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1,
                               timeout=0.2, rtscts=False, xonxoff=False)
        self.s.reset_input_buffer()

    def write(self, data):
        hexdump("TX", data)
        self.s.write(data)
        self.s.flush()

    def read(self, count):
        out = bytearray()
        last = time.monotonic()
        while len(out) < count:
            chunk = self.s.read(count - len(out))
            if chunk:
                out += chunk
                last = time.monotonic()
            elif time.monotonic() - last > SERIAL_IDLE_S:
                hexdump("RX (partial)", bytes(out))
                raise WeyError(f"timeout: got {len(out)} of {count} bytes, "
                               f"then nothing for {SERIAL_IDLE_S} s")
        hexdump("RX", bytes(out))
        return bytes(out)

    def close(self):
        self.s.close()


# --------------------------------------------------------------------------
# File operations (all multi-byte fields big-endian)
# --------------------------------------------------------------------------

def cstr(b):
    return b.split(b"\0", 1)[0].decode("latin1")


def list_files(link):
    link.write(bytes([CMD_LISTFILES, 0, 0, 0]))
    _cmd, _length, count = struct.unpack(">B2xII", link.read(11))
    if not 0 < count * 36 <= 1048576:
        raise WeyError(f"unexpected file count {count}")
    data = link.read(count * 36)
    files = []
    for i in range(count):
        idx, sub, name = struct.unpack(">HH32s", data[i * 36:(i + 1) * 36])
        files.append((idx, sub, cstr(name)))
    return files


def parse_spec(spec):
    m = re.fullmatch(r"\s*(\d+)\s*,\s*(\d+)\s*", spec)
    if not m:
        raise WeyError(f"invalid spec {spec!r}, expected INDEX,SUBINDEX")
    return int(m.group(1)), int(m.group(2))


def read_graph(link, idx, sub, outdir):
    if idx == 4:
        magic, subfield, name = 0xA054, (sub + 0x70) << 8, f"BMP{sub}.BMP"
    else:
        magic, subfield, name = 0x0101, sub, "Colorparm.par"
    link.write(struct.pack(">BHHI", CMD_READGRAPH, magic, subfield, 1000000))
    status = link.read(1)[0]
    if status != CMD_READGRAPH:
        raise WeyError(f"read failed: {status:02x}")
    link.read(4)
    (size,) = struct.unpack(">I", link.read(4))
    return name, link.read(size)


def read_file(link, spec, outdir="."):
    idx, sub = parse_spec(spec)
    if idx in (4, 6):
        name, data = read_graph(link, idx, sub, outdir)
    else:
        link.write(struct.pack(">BHH", CMD_READFILE, idx, sub))
        head = link.read(7)
        cmd, ridx, rsub, status = struct.unpack(">BHHH", head)
        if cmd != CMD_READFILE or status >> 8 == 0xD0:
            raise WeyError(f"read {idx},{sub} failed: {status:04x}")
        # No error: the two "status" bytes are the start of the file name.
        rest = head[5:7] + link.read(34)
        name_b, size = struct.unpack(">32sI", rest)
        name = cstr(name_b)
        print(f"{ridx},{rsub}: {name} {size} bytes")
        data = link.read(size)
    path = os.path.join(outdir, os.path.basename(name) or f"file_{idx}_{sub}.bin")
    with open(path, "wb") as f:
        f.write(data)
    return path


def write_file(link, spec):
    m = re.fullmatch(r"(?i)(?:.*[\\/])?LAYER(\d\d)\.LAY", spec)
    if m:
        idx, sub, path = 9, int(m.group(1)), spec
    else:
        m = re.fullmatch(r"\s*(\d+)\s*,\s*(\d+)\s*,(.+)", spec)
        if not m:
            raise WeyError(f"invalid spec {spec!r}, expected INDEX,SUBINDEX,FILE or LAYERnn.LAY")
        idx, sub, path = int(m.group(1)), int(m.group(2)), m.group(3)
    name = os.path.basename(path)
    if len(name) > 31:
        raise WeyError(f"file name {name} too long (max 31 characters)")
    with open(path, "rb") as f:
        data = f.read()
    link.write(struct.pack(">BHH32sI", CMD_WRITEFILE, idx, sub,
                           name.encode("latin1"), len(data)))
    for off in range(0, len(data), 512):
        link.write(data[off:off + 512])
        print(f"sent {min(off + 512, len(data))}/{len(data)} bytes", end="\r", file=sys.stderr)
    print(file=sys.stderr)
    cmd, _i, _s, status = struct.unpack(">BHHH", link.read(7))
    if cmd != CMD_WRITEFILE or status != STATUS_OK:
        raise WeyError(f"write {name} failed: {status:04x}")


def delete_file(link, spec):
    idx, sub = parse_spec(spec)
    link.write(struct.pack(">BHH", CMD_DELETE, idx, sub))
    cmd, _i, _s, status = struct.unpack(">BHHH", link.read(7))
    if cmd != CMD_DELETE or status != STATUS_OK:
        raise WeyError(f"delete {idx},{sub} failed: {status:04x}")


def parse_hex(arg):
    try:
        return bytes(int(x, 16) for x in re.split(r"[;,\s]+", arg.strip()) if x)
    except ValueError:
        raise WeyError(f"invalid hex byte list {arg!r}")


# --------------------------------------------------------------------------

def main(argv=None):
    global VERBOSE
    ap = argparse.ArgumentParser(description="Weytec MK06 file tool (USB or serial Host Port A)")
    ap.add_argument("-D", "--device", help="serial port (COM3, /dev/ttyUSB0); default: USB")
    ap.add_argument("-b", "--baud", type=int, default=115200, help="serial baud rate (default 115200)")
    ap.add_argument("-l", "--list", action="store_true", help="list files on the keyboard")
    ap.add_argument("-r", "--read", action="append", metavar="IDX,SUB", help="download a file (repeatable)")
    ap.add_argument("-w", "--write", action="append", metavar="IDX,SUB,FILE", help="upload a file, or LAYERnn.LAY (repeatable)")
    ap.add_argument("-d", "--delete", metavar="IDX,SUB", help="delete a file")
    ap.add_argument("-B", "--backup", metavar="DIR", help="download every readable file into DIR")
    ap.add_argument("-R", "--reboot", action="store_true", help="reboot the keyboard")
    ap.add_argument("--rawcmd", metavar="HEX", help="send raw bytes, e.g. 7f,e8")
    ap.add_argument("--rawrx", type=int, metavar="LEN", help="receive LEN raw bytes and dump them")
    ap.add_argument("-v", "--verbose", action="store_true", help="hex dump all traffic")
    a = ap.parse_args(argv)
    VERBOSE = a.verbose

    # If this keyboard is typing into this PC, give the Enter key time to come up.
    time.sleep(1)
    try:
        link = SerialLink(a.device, a.baud) if a.device else UsbLink()
    except WeyError as e:
        print(e, file=sys.stderr)
        return 1
    try:
        if a.list:
            print("Number Index SubIndex Name")
            for n, (idx, sub, name) in enumerate(list_files(link)):
                print(f"{n:6d} {idx:5d} {sub:8d} {name}")
        if a.backup:
            os.makedirs(a.backup, exist_ok=True)
            for idx, sub, name in list_files(link):
                if idx == 16:  # PinCode: write-only
                    continue
                try:
                    p = read_file(link, f"{idx},{sub}", a.backup)
                    print(f"  -> {p}")
                except WeyError as e:
                    print(f"  {idx},{sub} {name}: {e}", file=sys.stderr)
        if a.delete:
            delete_file(link, a.delete)
        for spec in a.read or []:
            read_file(link, spec)
        for spec in a.write or []:
            write_file(link, spec)
        if a.rawcmd:
            link.write(parse_hex(a.rawcmd))
        if a.rawrx:
            data = link.read(a.rawrx)
            if not VERBOSE:
                print(" ".join(f"{b:02X}" for b in data))
        if a.reboot:
            link.write(bytes([0x7F, 0xE4, 0x31, 0xC0, 0x02]))
    except WeyError as e:
        print(e, file=sys.stderr)
        return 1
    finally:
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
