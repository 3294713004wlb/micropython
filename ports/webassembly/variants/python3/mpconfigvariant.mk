# Enable Asyncify just as in the standard variant; do not alter pyscript.
include variants/standard/mpconfigvariant.mk
# Opt-in to the version-matched upstream micropython-lib set.
FROZEN_MANIFEST ?= variants/python3/manifest.py
