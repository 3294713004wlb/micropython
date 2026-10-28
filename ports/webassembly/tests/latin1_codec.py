"""Regression checks for WebAssembly's native Latin-1 str/bytes codec."""

all_bytes = bytes(range(256))

for encoding in (
    'latin1',
    'latin-1',
    'latin_1',
    'ISO-8859-1',
    'iso8859_1',
    'CP819',
):
    decoded = all_bytes.decode(encoding)
    assert decoded.encode(encoding) == all_bytes
    assert bytes(decoded, encoding) == all_bytes
    assert str(all_bytes, encoding) == decoded

assert bytes('A\u00e9\u00ff', 'latin1') == b'A\xe9\xff'
assert b'\x80\xff'.decode('latin1') == '\u0080\u00ff'
assert '\u20ac'.encode('latin1', 'ignore') == b''
assert '\u20ac'.encode('latin1', 'replace') == b'?'

try:
    '\u20ac'.encode('latin1')
except UnicodeError:
    pass
else:
    raise AssertionError('non-Latin-1 text was encoded without error')

assert '中文'.encode('utf-8').decode('utf-8') == '中文'
assert b'abc'.decode('ascii') == 'abc'

print('LATIN1_CODEC_OK')
