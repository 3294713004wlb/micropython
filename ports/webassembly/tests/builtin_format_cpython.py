"""Tests for MicroPython WASM's CPython-style built-in format()."""

assert format(255, "04x") == "00ff"
assert format(255, "#x") == "0xff"
assert format(10, "d") == "10"
assert format(10) == "10"
assert format(True) == "True"
assert format(True, "d") == "1"
assert format("hi", ">5") == "   hi"
assert format("hi") == "hi"

class Custom:
    def __format__(self, spec):
        return "custom:[" + spec + "]"

assert format(Custom()) == "custom:[]"
assert format(Custom(), "a{b}") == "custom:[a{b}]"
assert "{:20}".format(Custom()) == "custom:[20]"

class NonString:
    def __format__(self, spec):
        return 123

try:
    format(NonString(), "x")
except TypeError:
    pass
else:
    raise AssertionError("__format__ return must be str")

try:
    format(100, 3)
except TypeError:
    pass
else:
    raise AssertionError("format_spec must be str")

class Basic:
    def __str__(self):
        return "basic"

assert format(Basic()) == "basic"
try:
    format(Basic(), "4")
except TypeError:
    pass
else:
    raise AssertionError("non-empty object spec must fail")

class Status:
    def __int__(self):
        return 15
    def __format__(self, spec):
        return format(int(self), spec)

assert "{:04x}".format(Status()) == "000f"
print("PASS: built-in format()")
