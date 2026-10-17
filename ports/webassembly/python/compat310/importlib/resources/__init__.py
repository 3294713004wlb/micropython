"""Filesystem-backed importlib.resources APIs for MicroPython 3.10 compatibility.

Resources are loaded from mounted Python package directories. Zipimport-only
packages and namespace packages without a filesystem root are unsupported.
"""

import os
from importlib import import_module


class ResourcePath:
    """Filesystem Traversable for packages mounted in the WASM VFS."""

    def __init__(self, path):
        self._path = str(path)

    @property
    def name(self):
        return self._path.rstrip('/').rsplit('/', 1)[-1]

    def __fspath__(self):
        return self._path

    def __str__(self):
        return self._path

    def __repr__(self):
        return 'ResourcePath(%r)' % self._path

    def joinpath(self, *descendants):
        path = self._path.rstrip('/')
        for part in descendants:
            part = str(part)
            if part == '..' or part.startswith('/') or '/../' in '/' + part + '/':
                raise ValueError('resource must remain inside its package')
            path = path + '/' + part
        return ResourcePath(path)

    def __truediv__(self, child):
        return self.joinpath(child)

    def is_file(self):
        try:
            return (os.stat(self._path)[0] & 0o170000) == 0o100000
        except OSError:
            return False

    def is_dir(self):
        try:
            return (os.stat(self._path)[0] & 0o170000) == 0o040000
        except OSError:
            return False

    def iterdir(self):
        for name in os.listdir(self._path):
            yield self.joinpath(name)

    def open(self, mode='r', *args, **kwargs):
        return open(self._path, mode, *args, **kwargs)

    def read_bytes(self):
        with self.open('rb') as stream:
            return stream.read()

    def read_text(self, encoding='utf-8', errors='strict'):
        with self.open('r') as stream:
            value = stream.read()
        if isinstance(value, bytes):
            return value.decode(encoding, errors)
        return value


class _ExistingPathContext:
    def __init__(self, item):
        self.item = item

    def __enter__(self):
        return self.item

    def __exit__(self, exc_type, exc_value, traceback):
        return False


def _package(package):
    if isinstance(package, str):
        package = import_module(package)
    if not hasattr(package, '__path__'):
        raise TypeError('package must be an importable package')
    return package


def files(package):
    package = _package(package)
    filename = getattr(package, '__file__', None)
    if filename:
        return ResourcePath(filename.rsplit('/', 1)[0])
    paths = list(package.__path__)
    if not paths:
        raise FileNotFoundError('package has no filesystem path')
    return ResourcePath(paths[0])


def as_file(path):
    path = path if isinstance(path, ResourcePath) else ResourcePath(path)
    return _ExistingPathContext(path)


def open_binary(package, resource):
    return files(package).joinpath(resource).open('rb')


def open_text(package, resource, encoding='utf-8', errors='strict'):
    resource_path = files(package).joinpath(resource)
    try:
        return resource_path.open('r', encoding=encoding, errors=errors)
    except TypeError:
        if encoding.lower().replace('-', '') != 'utf8' or errors != 'strict':
            raise NotImplementedError('this MicroPython VFS only supports UTF-8 text files')
        return resource_path.open('r')


def read_binary(package, resource):
    with open_binary(package, resource) as stream:
        return stream.read()


def read_text(package, resource, encoding='utf-8', errors='strict'):
    return read_binary(package, resource).decode(encoding, errors)


def contents(package):
    return (entry.name for entry in files(package).iterdir())


def is_resource(package, name):
    return files(package).joinpath(name).is_file()


def path(package, resource):
    return as_file(files(package).joinpath(resource))


__all__ = [
    'ResourcePath', 'files', 'as_file', 'open_binary', 'open_text',
    'read_binary', 'read_text', 'contents', 'is_resource', 'path',
]
