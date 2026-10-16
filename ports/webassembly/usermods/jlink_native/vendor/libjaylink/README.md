# libjaylink

libjaylink is a shared library written in C to access SEGGER J-Link and
compatible devices.


## Requirements

libjaylink requires the following packages:

 - GCC (>= 4.0) or Clang

 - pkg-config >= 0.23
 - libusb >= 1.0.9 (optional)
 - Doxygen (optional, only required for API documentation)

If you're building libjaylink from Git, the following packages are additionally required:

 - Git
 - Meson >= 0.53.0
 - Ninja >= 1.10.0

## Building and installing

In order to get the latest Git version of libjaylink, run the following command:

```sh
git clone https://gitlab.zapb.de/libjaylink/libjaylink.git
```

Next, enter the libjaylink directory:

```sh
cd libjaylink
```

Afterwards, setup Meson and compile libjaylink:

```sh
meson setup build
meson compile -C build
```

After compilation finishes without any errors, use the following command to install libjaylink:

```sh
meson install -C build
```

## Portability

libjaylink supports the following operating systems:

 - GNU/Linux
 - FreeBSD
 - OpenBSD
 - NetBSD
 - Microsoft Windows
   - Cygwin, MinGW and MSYS2
 - OS X

## Contribution

If you would like to contribute to this project, whether by adding new features or reporting bugs, please read the relevant sections in [HACKING](HACKING.md).

## Support

If you appreciate the project, feel free to donate on Liberapay:

[![Liberapay donation link](https://liberapay.com/assets/widgets/donate.svg)](https://liberapay.com/zapb/donate)


## Copyright and license

libjaylink is licensed under the terms of the GNU Lesser General Public License (LGPL) v2.1 or later.
See COPYING file for details.

## Website

<https://gitlab.zapb.de/libjaylink/libjaylink.git>
