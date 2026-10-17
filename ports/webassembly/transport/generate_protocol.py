#!/usr/bin/env python3
"""Generate the C and JS transport ABI declarations from one JSON schema.

Usage:
    python3 transport/generate_protocol.py
    python3 transport/generate_protocol.py --js-output /path/to/uploader/src/uploader/transport-protocol.js

The generated artifacts should be committed together and must use the same
protocol.json. Do not edit generated constants manually.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def constant(name: str) -> str:
    """Map JSON camelCase keys to upper-snake-case constant names."""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


def generate(schema: dict) -> tuple[str, str]:
    """Return the C header and ES module with identical ABI definitions."""
    fingerprint = hashlib.sha256(
        json.dumps(schema, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()
    regions = schema["regions"]
    header = [
        "/**",
        " * Generated from transport/protocol.json - DO NOT EDIT BY HAND",
        f" * Protocol fingerprint: {fingerprint}",
        " */",
        "#ifndef MICROPY_WASM_TRANSPORT_PROTOCOL_H",
        "#define MICROPY_WASM_TRANSPORT_PROTOCOL_H",
        f'#define TP_PROTOCOL_FINGERPRINT "{fingerprint}"',
        f'#define TP_PROTOCOL_HASH32 0x{fingerprint[:8]}u',
        f'#define TP_PROTOCOL_VERSION {schema["version"]}',
        f'#define TP_MAILBOX_MAGIC 0x{schema["mailboxMagic"]:08x}u',
        f'#define TP_REGION_COUNT {len(regions)}',
    ]
    for region in regions:
        key = constant(region["name"])
        header.extend([
            f'#define TP_REGION_{key} {region["id"]}',
            f'#define TP_REGION_SIZE_{key} {region["bytes"]}u',
        ])
    for section, values in [
        ("RING", schema["ring"]),
        ("TERMINAL", schema["terminal"]),
    ]:
        for name, value in values.items():
            header.append(f"#define TP_{section}_{constant(name)} {value}")
    header.append(f'#define TP_MAILBOX_WORDS {schema["mailbox"]["words"]}')
    for name, value in schema["mailbox"]["fields"].items():
        header.append(f"#define TP_FIELD_{constant(name)} {value}")
    for name, value in schema["mailbox"]["states"].items():
        header.append(f"#define TP_STATUS_{constant(name)} {value}")
    for family, operations in schema["opcodes"].items():
        for name, value in operations.items():
            header.append(f"#define TP_OPCODE_{family.upper()}_{constant(name)} {value}")
    for name, value in schema.get("protocolKinds", {}).items():
        header.append(f"#define TP_KIND_{constant(name)} {value}")
    for section, fields in schema.get("auxiliary", {}).items():
        for name, value in fields.items():
            header.append(f"#define TP_{constant(section)}_{constant(name)} {value}")
    header.extend(["#endif /* MICROPY_WASM_TRANSPORT_PROTOCOL_H */", ""])

    js = [
        "/**",
        " * Generated from transport/protocol.json - DO NOT EDIT BY HAND",
        " * The shared protocol is versioned independently of Python APIs",
        " * @module uploader/transport-protocol",
        " */",
        f"export const TRANSPORT_FINGERPRINT = '{fingerprint}';",
        f'export const TRANSPORT_HASH32 = 0x{fingerprint[:8]};',
        f'export const TRANSPORT_VERSION = {schema["version"]};',
        f'export const TRANSPORT_MAGIC = 0x{schema["mailboxMagic"]:08x};',
    ]
    objects = {
        "REGION": {constant(r["name"]): r["id"] for r in regions},
        "REGION_BYTES": {constant(r["name"]): r["bytes"] for r in regions},
        "RING": {constant(k): v for k, v in schema["ring"].items()},
        "TERMINAL": {constant(k): v for k, v in schema["terminal"].items()},
        "MAILBOX_FIELD": {constant(k): v for k, v in schema["mailbox"]["fields"].items()},
        "MAILBOX_STATUS": {constant(k): v for k, v in schema["mailbox"]["states"].items()},
        "USB_OPCODE": {constant(k): v for k, v in schema["opcodes"]["usb"].items()},
        "HID_OPCODE": {constant(k): v for k, v in schema["opcodes"]["hid"].items()},
        "KIND": {constant(k): v for k, v in schema.get("protocolKinds", {}).items()},
        **{
            constant(section): {constant(key): value for key, value in fields.items()}
            for section, fields in schema.get("auxiliary", {}).items()
        },
    }
    for name, values in objects.items():
        js.extend([
            "",
            "/**",
            " * Immutable protocol constants generated from the ABI schema",
            " *",
            " * @readonly",
            " */",
            f"export const TRANSPORT_{name} = Object.freeze({json.dumps(values, indent=4)});",
        ])
    js.extend(["", f"export const TRANSPORT_REGION_COUNT = {len(regions)};", ""])
    return "\n".join(header), "\n".join(js)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", type=Path, default=Path(__file__).with_name("protocol.json"))
    parser.add_argument("--header-output", type=Path, default=Path(__file__).with_name("protocol.h"))
    parser.add_argument("--js-output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = json.loads(args.schema.read_text(encoding="utf-8"))
    generated = generate(source)
    outputs = [(args.header_output, generated[0])]
    if args.js_output:
        outputs.append((args.js_output, generated[1]))
    for path, content in outputs:
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                parser.error(f"Generated protocol is out of date: {path}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
    print(f"Native protocol v{source['version']} verified" if args.check else
        f"Native protocol v{source['version']} generated")


if __name__ == "__main__":
    main()
