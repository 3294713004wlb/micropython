sources = [
  'buffer.c',
  'c2.c',
  'core.c',
  'device.c',
  'discovery.c',
  'discovery_tcp.c',
  'emucom.c',
  'error.c',
  'fileio.c',
  'jtag.c',
  'list.c',
  'log.c',
  'socket.c',
  'spi.c',
  'strutil.c',
  'swd.c',
  'swo.c',
  'target.c',
  'transport.c',
  'transport_tcp.c',
  'util.c',
  'version.c',
]

if have_usb
  sources += ['discovery_usb.c', 'transport_usb.c']
endif

version_h = configure_file(
  input: 'version.h.in',
  output: 'version.h',
  configuration: {
    'JAYLINK_VERSION_PACKAGE_MAJOR': package_version['major'],
    'JAYLINK_VERSION_PACKAGE_MINOR': package_version['minor'],
    'JAYLINK_VERSION_PACKAGE_MICRO': package_version['micro'],
    'JAYLINK_VERSION_PACKAGE': package_version_string,
    'JAYLINK_VERSION_LIBRARY_CURRENT': library_version['current'],
    'JAYLINK_VERSION_LIBRARY_REVISION': library_version['revision'],
    'JAYLINK_VERSION_LIBRARY_AGE': library_version['age'],
    'JAYLINK_VERSION_LIBRARY': library_version_string,
  }
)

install_headers([
    version_h,
    'libjaylink.h',
  ],
  subdir: 'libjaylink'
)

jaylink = shared_library(
  'jaylink',
  sources,
  dependencies: [libusb, ws2_32],
  version: library_version_string,
  include_directories: include_dirs,
  install: true,
)

pkg = import('pkgconfig')

pkg.generate(
  jaylink,
  name: meson.project_name(),
  url: project_url,
  description: project_description,
  requires_private: [libusb],
)
