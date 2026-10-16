project(
  'libjaylink', 'c',
  version: '0.5.0',
  license: ' LGPL-2.1-or-later',
  meson_version: '>= 0.53.0',
  default_options: [
    'c_std=gnu99',
    'warning_level=3',
    'werror=true',
  ]
)

project_description = 'Library to access J-Link devices'
project_url = 'https://gitlab.zapb.de/libjaylink/libjaylink.git'

cc = meson.get_compiler('c')
build_system = build_machine.system()

option_usb = get_option('usb')

include_dirs = include_directories('libjaylink')

libusb = dependency(
  'libusb-1.0',
  version: '>=1.0.16',
  required: option_usb,
)

ws2_32 = cc.find_library('ws2_32', required: build_system == 'windows')

have_usb = libusb.found()

if have_usb
  add_project_arguments('-DHAVE_LIBUSB', language: 'c')
endif

version = meson.project_version()
version_array = version.split('.')
major_version = version_array[0].to_int()
minor_version = version_array[1].to_int()
version_micro = version_array[2].to_int()

package_version = {
  'major': version_array[0],
  'minor': version_array[1],
  'micro': version_array[2],
}

git = find_program('git', required: false)

if git.found()
  git_tag = run_command([git, 'describe', '--dirty'], check: false).stdout().strip()

  if git_tag != ''
    package_version_string = git_tag
  else
    package_version_string = version
  endif
else
  package_version_string = version
endif

# Libtool interface version of libjaylink. This is not the same as the package
# version. For information about the versioning system of libtool, see:
# http://www.gnu.org/software/libtool/manual/libtool.html#Libtool-versioning

library_version = {
  'current': 2,
  'revision': 2,
  'age': 2,
}

library_version_string = '@0@.@1@.@2@'.format(
  library_version['current'] - library_version['age'],
  library_version['age'],
  library_version['revision'])

subdir('libjaylink')

summary({
    'Package version': package_version_string,
    'Library version': library_version_string,
  },
  section: 'Project details'
)

summary({
    'USB': have_usb,
    'TCP': true
  },
  section: 'Enabled transports',
  bool_yn: true
)
