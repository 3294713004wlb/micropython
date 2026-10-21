"""Special-method lookup must ignore instance __getattr__ for formatting."""


class SpecialForm:
    def __init__(self, name):
        self.name = name
        self.missing = []

    def __getattr__(self, attr):
        self.missing.append(attr)
        raise AttributeError(attr)

    def __repr__(self):
        return "typing." + self.name


value = SpecialForm("Optional")
# MicroPython rewrites f-strings to str.format() internally.
assert f"{value}" == "typing.Optional"
assert "{}".format(value) == "typing.Optional"
assert "{:}".format(value) == "typing.Optional"
assert format(value) == "typing.Optional"
assert value.missing == [], value.missing

try:
    format(value, ">20")
except TypeError:
    pass
else:
    raise AssertionError("Non-empty format spec should be rejected")
assert value.missing == [], value.missing


class WithFormatter(SpecialForm):
    def __format__(self, spec):
        return "special:" + spec


custom = WithFormatter("X")
assert format(custom, "04x") == "special:04x"
assert "{:04x}".format(custom) == "special:04x"
assert custom.missing == []

# Even an instance-level __format__ attribute must not override a special method.
custom.__format__ = lambda spec: "WRONG"
assert format(custom, "y") == "special:y"
assert "{:z}".format(custom) == "special:z"

# Exercise the real typing.Optional path used by pyOCD.
from typing import Optional
assert Optional[int] is not None
print("PASS: format lookup skips instance __getattr__ and typing.Optional works")
