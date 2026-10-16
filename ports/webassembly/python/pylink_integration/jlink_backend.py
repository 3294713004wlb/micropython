"""First-stage pylink adapter for a browser MicroPython runtime.

Keep pylink-square installed unchanged. Import this module alongside pylink and
use its JLink class explicitly, or install_into_pylink(pylink) during testing.
This is NOT a replacement for SEGGER's native DLL API yet.
"""
import jaylink_native


class JLink:
    """Small pylink-style façade for the WebUSB/libjaylink transport stage."""

    def __init__(self, *args, **kwargs):
        if args or kwargs:
            raise TypeError("Browser JLink accepts no constructor parameters")
        self._opened = False

    def open(self, usb_device=None, *, ep_in=None, ep_out=None,
             interface=0, serial=None):
        if usb_device is None:
            raise ValueError("usb_device is required for the browser backend")
        if ep_in is None or ep_out is None:
            raise ValueError("provide discovered bulk endpoints ep_in and ep_out")
        if self._opened:
            raise RuntimeError("J-Link already opened")
        jaylink_native.bind_usb(usb_device, ep_in, ep_out, interface, serial)
        self._opened = True

    def close(self):
        if self._opened:
            jaylink_native.unbind_usb()
            self._opened = False

    def firmware_version(self):
        if not self._opened:
            raise RuntimeError("J-Link is not open")
        return jaylink_native.firmware_version()

    def transport_write(self, data, timeout=1000):
        if not self._opened:
            raise RuntimeError("J-Link is not open")
        return jaylink_native.bulk_write(data, timeout)

    def transport_read(self, size, timeout=1000):
        if not self._opened:
            raise RuntimeError("J-Link is not open")
        return jaylink_native.bulk_read(size, timeout)

    def connect(self, *args, **kwargs):
        raise NotImplementedError(
            "SWD connect requires the next-stage pylink DLL-API replacement"
        )

    def flash_file(self, *args, **kwargs):
        raise NotImplementedError(
            "Flash requires the next-stage CoreSight/CMSIS-Pack adapter"
        )

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def install_into_pylink(pylink_module):
    """Opt-in only: replaces the class, not other upstream pylink constants."""
    pylink_module.JLink = JLink
