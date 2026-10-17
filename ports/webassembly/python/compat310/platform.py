"""Python 3.10 platform query API for MicroPython WASM in the browser.

Values describe the *actual* MicroPython/wasm32 runtime. They deliberately do
not claim to be CPython 3.10, Linux, Windows, or a native browser host OS.
"""

import sys
from collections import namedtuple

_Uname = namedtuple('uname_result', 'system node release version machine processor')


def system():
    return 'Browser'


def node():
    return 'browser-worker'


def release():
    return ''


def version():
    return getattr(sys, 'version', '')


def machine():
    return 'wasm32'


def processor():
    return 'wasm32'


def uname():
    return _Uname(system(), node(), release(), version(), machine(), processor())


def architecture(executable=None, bits='', linkage=''):
    return ('32bit', 'wasm')


def platform(aliased=False, terse=False):
    return 'Browser-wasm32' if terse else 'Browser-wasm32-MicroPython'


def python_implementation():
    return 'MicroPython'


def python_version():
    return str(getattr(sys, 'version', '')).split(' ', 1)[0]


def python_version_tuple():
    parts = python_version().split('.')
    return tuple((parts + ['0', '0', '0'])[:3])


def python_compiler():
    return 'Emscripten'


def python_build():
    implementation = getattr(sys, 'implementation', None)
    value = getattr(implementation, 'version', ())
    return (str(value), 'MicroPython WASM')


def python_branch():
    return ''


def python_revision():
    return ''


def system_alias(system_name, release_name, version_name):
    return (system_name, release_name, version_name)


def libc_ver(executable=None, lib='', version='', chunksize=16384):
    return (lib, version)


def win32_ver(release='', version='', csd='', ptype=''):
    return (release, version, csd, ptype)


def mac_ver(release='', versioninfo=('', '', ''), machine=''):
    return (release, versioninfo, machine)


def java_ver(release='', vendor='', vminfo=('', '', ''), osinfo=('', '', '')):
    return (release, vendor, vminfo, osinfo)


def freedesktop_os_release():
    raise OSError('freedesktop os-release is unavailable in a browser Worker')


def invalidate_caches():
    return None


__all__ = [
    'system', 'node', 'release', 'version', 'machine', 'processor', 'uname',
    'architecture', 'platform', 'python_implementation', 'python_version',
    'python_version_tuple', 'python_compiler', 'python_build',
    'python_branch', 'python_revision', 'system_alias', 'libc_ver',
    'win32_ver', 'mac_ver', 'java_ver', 'freedesktop_os_release',
    'invalidate_caches',
]
