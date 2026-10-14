"""Smoke-test the FATFS, LittleFS v1/v2 VFS classes in MicroPython WASM.

Run from the MicroPython terminal after compiling and loading the WASM build:

    exec(open('/workspace/vfs-smoke.py').read())

This test creates memory-backed file systems; it does not touch real flash.
"""

import os
import vfs


class RamBlockDevice:
    """A block device with the extended LittleFS read/write interface."""

    def __init__(self, block_size, block_count):
        self.block_size = block_size
        self.block_count = block_count
        self.data = bytearray(b"\xff" * (block_size * block_count))

    def readblocks(self, block_num, buf, offset=0):
        start = block_num * self.block_size + offset
        end = start + len(buf)
        if start < 0 or end > len(self.data):
            return 1
        memoryview(buf)[:] = memoryview(self.data)[start:end]
        return 0

    def writeblocks(self, block_num, buf, offset=None):
        start = block_num * self.block_size
        if offset is None:
            # Basic-interface writes must erase the addressed blocks first.
            length = ((len(buf) + self.block_size - 1) // self.block_size)
            length *= self.block_size
            self.data[start:start + length] = b"\xff" * length
        else:
            start += offset
        end = start + len(buf)
        if start < 0 or end > len(self.data):
            return 1
        self.data[start:end] = buf
        return 0

    def ioctl(self, op, arg):
        if op == 4:
            return self.block_count
        if op == 5:
            return self.block_size
        if op == 6:
            start = arg * self.block_size
            self.data[start:start + self.block_size] = b"\xff" * self.block_size
            return 0
        if op in (1, 2, 3):
            return 0
        return 0


def smoke(name, device):
    cls = getattr(vfs, name)
    cls.mkfs(device)
    fs = cls(device)
    mountpoint = "/__vfs_smoke_" + name.lower()
    try:
        os.mkdir(mountpoint)
    except OSError:
        pass
    mounted = False
    try:
        vfs.mount(fs, mountpoint)
        mounted = True
        os.mkdir(mountpoint + "/folder")
        testfile = mountpoint + "/folder/example.txt"
        with open(testfile, "wb") as stream:
            stream.write(b"MicroPython WASM filesystem test\n")
        with open(testfile, "rb") as stream:
            assert stream.read() == b"MicroPython WASM filesystem test\n"
        assert "example.txt" in os.listdir(mountpoint + "/folder")
        print("PASS:", name)
    finally:
        if mounted:
            vfs.umount(mountpoint)
        os.rmdir(mountpoint)


for name in ("VfsFat", "VfsLfs1", "VfsLfs2"):
    assert hasattr(vfs, name), "Missing vfs." + name

smoke("VfsFat", RamBlockDevice(512, 4096))
smoke("VfsLfs1", RamBlockDevice(4096, 64))
smoke("VfsLfs2", RamBlockDevice(4096, 64))
print("All three filesystem variants passed")
