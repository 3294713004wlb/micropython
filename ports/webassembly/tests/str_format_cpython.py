"""CPython-compatible str.format regression tests for MicroPython WASM.

Run with CPython for reference and with the rebuilt MicroPython WASM to
verify the result in the target runtime. No tqdm dependency is required.
"""


class FormatReplace:
    def __init__(self, replace=""):
        self.replace = replace
        self.calls = []

    def __format__(self, spec):
        self.calls.append(spec)
        return self.replace

    def __str__(self):
        return "must-not-use-str"


class Bar:
    def __init__(self, value):
        self.value = value
        self.calls = []

    def __format__(self, spec):
        self.calls.append(spec)
        width = int(spec or "10")
        return "#" * self.value + "-" * (width - self.value)


class NoFormatter:
    def __str__(self):
        return "plain"


class BadFormatter:
    def __format__(self, spec):
        return 42


# tqdm's measurement pass: __format__ must be called even when spec is blank.
marker = FormatReplace()
assert "x{bar:20}y".format(bar=marker) == "xy"
assert marker.calls == ["20"], marker.calls

# tqdm's rendering pass: the complete format spec must reach Bar.__format__.
bar = Bar(3)
assert "[{:10}]".format(bar) == "[###-------]"
assert bar.calls == ["10"], bar.calls

# Empty spec and nested spec must be passed to the object's special method.
marker2 = FormatReplace("ok")
assert "{} {:} {:{width}}".format(marker2, marker2, marker2, width=7) == "ok ok ok"
assert marker2.calls == ["", "", "7"], marker2.calls

# Conversion happens before formatting and bypasses the original __format__.
marker3 = FormatReplace("ignored")
assert "{!s}".format(marker3) == "must-not-use-str"
assert marker3.calls == []

# Returning a non-string from __format__ is not legal.
try:
    "{}".format(BadFormatter())
except TypeError:
    pass
else:
    raise AssertionError("__format__() must return str")

# Ordinary objects use object.__format__ fallback semantics.
assert "{}".format(NoFormatter()) == "plain"
assert "{:}".format(NoFormatter()) == "plain"
try:
    "{:>4}".format(NoFormatter())
except TypeError:
    pass
else:
    raise AssertionError("nonempty spec must fail when __format__ is absent")

# bytes are not strings in Python 3: only the empty format spec is allowed.
assert "{}".format(b"ab") == "b'ab'"
try:
    "{:>8}".format(b"ab")
except TypeError:
    pass
else:
    raise AssertionError("bytes with a nonempty spec must fail")

# Existing native formatting paths must not regress.
assert "{:08x}".format(255) == "000000ff"
assert "{:.2f}".format(1.5) == "1.50"
assert "{:>5}".format("a") == "    a"
assert "{0:{1}}".format(5, "04d") == "0005"
assert "{{{}}}".format("v") == "{v}"
print("PASS: str.format __format__ dispatch and native formatting")
