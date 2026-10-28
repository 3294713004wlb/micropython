"""Native WebAssembly text codec regression tests (run inside the rebuilt VM)."""


def verify_roundtrip(encoding, value):
    native = value.encode(encoding)
    assert bytes(value, encoding) == native
    assert native.decode(encoding) == value
    assert str(native, encoding) == value


for name in (
    "latin1",
    "latin-1",
    "ISO-8859-1",
    "iso8859_1",
):
    all_bytes = bytes(range(256))
    assert str(all_bytes, name).encode(name) == all_bytes
    assert bytes("A\u00ff\x00", name) == b"A\xff\x00"

for name in (
    "utf-8-sig",
    "UTF_8_SIG",
    "utf-16",
    "utf-16-le",
    "UTF-16BE",
    "utf-16le",
    "utf-32",
    "utf-32-le",
    "UTF_32_BE",
):
    verify_roundtrip(name, "A\u00e9\u6c49\U0001f680")
    verify_roundtrip(name, "")

assert "\u6c49".encode("utf-8-sig") == b"\xef\xbb\xbf\xe6\xb1\x89"
assert b"\xef\xbb\xbfHello".decode("utf-8-sig") == "Hello"
assert b"Hello".decode("utf-8-sig") == "Hello"
assert "A".encode("utf-16") == b"\xff\xfeA\x00"
assert b"\xfe\xff\x00A".decode("utf-16") == "A"
assert "A".encode("utf-32") == b"\xff\xfe\x00\x00A\x00\x00\x00"
assert b"\x00\x00\xfe\xff\x00\x00\x00A".decode("utf-32") == "A"
assert b"\xff\xfe".decode("utf-16-le") == "\ufeff"
assert b"\x00\xd8A\x00".decode("utf-16-le", "replace") == "\ufffdA"
assert b"\x00\xd8A\x00".decode("utf-16-le", "ignore") == "A"
assert b"\xff".decode("utf-16-le", "replace") == "\ufffd"
assert b"\x00\x00\x11\x00".decode("utf-32-le", "replace") == "\ufffd"

for invalid, encoding in (
    (b"\xff", "utf-16-le"),
    (b"\x00\xd8", "utf-16-le"),
    (b"\x00\x00\x11\x00", "utf-32-le"),
    (b"\x81", "cp1252"),
):
    try:
        invalid.decode(encoding)
    except UnicodeError:
        pass
    else:
        raise AssertionError("expected a UnicodeError: " + encoding)

assert "\u20ac".encode("cp1252") == b"\x80"
assert b"\x80".decode("cp1252") == "\u20ac"
assert b"\x81".decode("cp1252", "replace") == "\ufffd"
assert "\u00e9".encode("cp437") == b"\x82"
assert b"\x82".decode("cp437") == "\u00e9"
assert bytes(range(256)).decode("cp437").encode("cp437") == bytes(range(256))
assert "\u6c49".encode("cp437", "replace") == b"?"
assert "\u6c49".encode("cp437", "ignore") == b""

# The original NodeMCU protocol does not use a Python fallback encoder.
assert bytes("UUUU", "latin1") == b"UUUU"
assert b"\x80\xff".decode("latin1") == "\u0080\u00ff"

print("WASM_EXTRA_TEXT_CODECS_OK")
