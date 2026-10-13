# Reuse this version's maintained PyScript standard-library selection.
# The base manifest already freezes the browser's JS-driven asyncio scheduler.
# Be careful with conflicts with custom Python modules installed under /lib.
include("$(PORT_DIR)/variants/pyscript/manifest.py")
