"""Python 3.10 core API regression for the browser MicroPython runtime.

Run with the newly compiled WASM interpreter, not the old released binary.
"""
import errno
import os

# Real Python exception types, not aliases, with the standard inheritance tree.
expected = (
    (FloatingPointError, ArithmeticError),
    (BufferError, Exception),
    (ModuleNotFoundError, ImportError),
    (UnboundLocalError, NameError),
    (BlockingIOError, OSError),
    (ChildProcessError, OSError),
    (ConnectionError, OSError),
    (BrokenPipeError, ConnectionError),
    (ConnectionAbortedError, ConnectionError),
    (ConnectionRefusedError, ConnectionError),
    (ConnectionResetError, ConnectionError),
    (FileExistsError, OSError),
    (FileNotFoundError, OSError),
    (InterruptedError, OSError),
    (IsADirectoryError, OSError),
    (NotADirectoryError, OSError),
    (PermissionError, OSError),
    (ProcessLookupError, OSError),
    (TimeoutError, OSError),
    (ReferenceError, Exception),
    (RecursionError, RuntimeError),
    (SystemError, Exception),
    (TabError, IndentationError),
    (UnicodeEncodeError, UnicodeError),
    (UnicodeDecodeError, UnicodeError),
    (UnicodeTranslateError, UnicodeError),
    (DeprecationWarning, Warning),
    (PendingDeprecationWarning, Warning),
    (RuntimeWarning, Warning),
    (SyntaxWarning, Warning),
    (UserWarning, Warning),
    (FutureWarning, Warning),
    (ImportWarning, Warning),
    (UnicodeWarning, Warning),
    (BytesWarning, Warning),
    (ResourceWarning, Warning),
    (EncodingWarning, Warning),
)
for cls, base in expected:
    assert issubclass(cls, base), (cls, base)

assert IOError is OSError
assert EnvironmentError is OSError
assert type(OSError(errno.ENOENT, 'missing')) is FileNotFoundError
assert isinstance(OSError(errno.EACCES, 'denied'), PermissionError)
assert isinstance(OSError(errno.ECONNREFUSED, 'connection refused'), ConnectionRefusedError)

error = RuntimeError('before')
RuntimeError.__init__(error, 'after', 7)
assert error.args == ('after', 7)
error.args = ('reset',)
assert error.args == ('reset',)

ue = UnicodeDecodeError('utf-8', b'\xff', 0, 1, 'invalid byte')
assert ue.encoding == 'utf-8'
assert ue.object == b'\xff'
assert ue.start == 0
assert ue.end == 1
assert ue.reason == 'invalid byte'

assert 'normal'.isascii()
assert ''.isascii()
assert not '你好'.isascii()
assert b'abc'.isascii()
assert not b'\xff'.isascii()
assert 'abc'.ljust(5, 'x') == 'abcxx'
assert 'abc'.rjust(5, 'x') == 'xxabc'
assert '好'.ljust(3, '界') == '好界界'
assert b'\x00'.ljust(3, b'\xff') == b'\x00\xff\xff'
assert 'prefix-value'.removeprefix('prefix-') == 'value'
assert b'data.sfx'.removesuffix(b'.sfx') == b'data'
assert True.to_bytes(1, 'big') == b'\x01'
assert False.to_bytes(1, 'little') == b'\x00'

# The open() implementation in the browser WASM port is POSIX-backed.
base = '/workspace' if 'workspace' in os.listdir('/') else '/tmp'
path = base + '/py310-core-api-test.bin'
with open(path, 'wb') as handle:
    assert handle.name == path
    assert handle.mode == 'wb'
    assert not handle.closed
    assert handle.writable() and not handle.readable()
    assert handle.seekable()
    handle.write(b'123')
    handle.flush()

with open(path, 'rb') as handle:
    assert handle.name == path
    assert handle.mode == 'rb'
    assert handle.readable() and not handle.writable()
    assert handle.read() == b'123'

with open(path, 'rt') as handle:
    assert handle.name == path
    assert handle.mode == 'rt'

os.remove(path)
with open(path, 'xb') as handle:
    assert handle.writable()
    handle.write(b'x')
try:
    open(path, 'xb')
except FileExistsError:
    pass
else:
    raise AssertionError('Expected FileExistsError')
os.remove(path)
try:
    open(path, 'rb')
except FileNotFoundError as exc:
    assert exc.errno == errno.ENOENT
    assert exc.filename == path
else:
    raise AssertionError('Expected FileNotFoundError')

# Exercise the actual file.flush() path, not merely accepting the kwarg.
path = base + '/py310-print-flush.txt'
with open(path, 'wt') as handle:
    print('ready', file=handle, flush=True)
with open(path, 'rt') as handle:
    assert handle.read() == 'ready\n'
os.remove(path)

print('PASS: Python 3.10 core APIs')
