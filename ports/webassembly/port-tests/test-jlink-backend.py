"""Run with host CPython: python port-tests/test-jlink-backend.py"""
import importlib.util
from pathlib import Path
import sys
import types

class FakeNative(types.ModuleType):
    def __init__(self):
        super().__init__("jaylink_native")
        self.bound = False
        self.device = None
    def bind_usb(self, dev, ep_in, ep_out, interface, serial):
        self.bound = True
        self.device = dev
        assert (ep_in, ep_out, interface, serial) == (0x81, 0x02, 0, None)
    def unbind_usb(self):
        self.bound = False
    def firmware_version(self):
        return "Fake firmware version"
    def bulk_write(self, data, timeout):
        return self.device.write(0x02, data, timeout)
    def bulk_read(self, size, timeout):
        return self.device.read(0x81, size, timeout)

sys.modules["jaylink_native"] = FakeNative()
module_path = Path(__file__).resolve().parent.parent / "python" / "pylink_integration" / "jlink_backend.py"
spec = importlib.util.spec_from_file_location("jlink_backend", module_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Device:
    idVendor = 0x1366
    idProduct = 0x1015
    def read(self, ep, size, timeout):
        assert (ep, size, timeout) == (0x81, 3, 1000)
        return b"123"
    def write(self, ep, data, timeout):
        assert (ep, data, timeout) == (0x02, b"OK", 1000)
        return 2

link = module.JLink()
link.open(Device(), ep_in=0x81, ep_out=0x02)
assert link.firmware_version() == "Fake firmware version"
assert link.transport_write(b"OK") == 2
assert link.transport_read(3) == b"123"
try:
    link.connect()
except NotImplementedError:
    pass
else:
    raise AssertionError("connect must not silently pretend to work")
link.close()
assert not sys.modules['jaylink_native'].bound
print("pylink browser backend smoke OK")
