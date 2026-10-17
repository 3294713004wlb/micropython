"""Runtime regression for general bytearray/array growth after building WASM.

The test targets semantics, alias-safe copying of mutable data, and large
incremental buffers. Capacity is an internal implementation detail.
"""

from array import array
import hashlib

CHUNK = b"\x00\x7f\x80\xff" * 1024
COUNT = 1024

out = bytearray()
for i in range(COUNT):
    if i & 1:
        out.extend(CHUNK)
    else:
        out += CHUNK
assert len(out) == len(CHUNK) * COUNT
assert out[:len(CHUNK)] == CHUNK
assert out[-len(CHUNK):] == CHUNK
assert hashlib.md5(out).digest() == hashlib.md5(CHUNK * COUNT).digest()
assert bytes(out) == CHUNK * COUNT

small = bytearray(b"abcd")
small += bytes(small)
assert small == b"abcdabcd"
small[2:4] = b"TEST1234"
assert small == b"abTEST1234abcd"
small.append(255)
assert small[-1] == 255

words = array("I")
for i in range(2048):
    words.append(i)
words.extend(array("I", (2048, 2049)))
assert len(words) == 2050
assert words[-2] == 2048 and words[-1] == 2049

print("PASS: generic bytearray + array growth, 4 MiB accumulation")
