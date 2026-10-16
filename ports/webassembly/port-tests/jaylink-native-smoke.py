"""Load in MicroPython WASM after building the user C module.

No real USB device is needed. A fake J-Link responds with bytes so that the
C -> PyUSB -> C bridge can be checked before any libjaylink firmware tests.
"""
import jaylink_native


class FakeUSB:
    idVendor = 0x1366
    idProduct = 0x1015

    def __init__(self):
        self.last = None

    def write(self, ep, data, timeout):
        assert ep == 0x02
        assert timeout == 1000
        self.last = bytes(data)
        return len(data)

    def read(self, ep, length, timeout):
        assert ep == 0x81
        assert length == 3
        assert timeout == 1000
        return b"ABC"


dev = FakeUSB()
assert not jaylink_native.ready()
jaylink_native.bind_usb(dev, 0x81, 0x02, 0, "123456789")
assert jaylink_native.ready()
assert jaylink_native.bulk_write(b"\x01\x02", 1000) == 2
assert dev.last == b"\x01\x02"
assert jaylink_native.bulk_read(3, 1000) == b"ABC"
jaylink_native.unbind_usb()
assert not jaylink_native.ready()
print("jaylink-native USB smoke OK")
