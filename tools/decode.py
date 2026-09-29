#!/usr/bin/env python3
"""Annotate Node B's frame lines with signals from a RealDash CAN XML file.

    cat /dev/cu.usbmodemXXXX | python3 tools/decode.py            # live
    python3 tools/decode.py can_1432.log                          # from a log
    python3 tools/decode.py --changes can_1432.log                # only value changes

Input lines:  <ts_ms> <ID hex> [<dlc>] <bytes hex ...>   ('#' lines pass through)
Stdlib only. Signals in the XML are K25 and unverified on the K255.
"""
import argparse
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

DEFAULT_XML = Path(__file__).resolve().parent.parent / "reference" / "BMW_R1200GS_K25_CAN.xml"
LINE_RE = re.compile(r"^(\d+)\s+([0-9A-Fa-f]+)\s+\[(\d+)\]((?:\s+[0-9A-Fa-f]{2})*)\s*$")


class Signal:
    def __init__(self, el, comment):
        self.name = el.get("name") or comment or f"target{el.get('targetId')}"
        self.offset = int(el.get("offset", "0"))
        self.length = int(el.get("length", "1"))
        self.big_endian = el.get("endianness", "little").lower() == "big"
        self.startbit = int(el.get("startbit")) if "startbit" in el.attrib else None
        self.bitcount = int(el.get("bitcount", "0"))
        self.units = el.get("units") or el.get("unit") or ""
        self.enum = dict(p.split(":", 1) for p in el.get("enum", "").split(",") if ":" in p)
        self.expr = to_python(el.get("conversion"))

    def decode(self, data):
        raw = data[self.offset:self.offset + self.length]
        if len(raw) < self.length:
            return None
        v = int.from_bytes(raw, "big" if self.big_endian else "little")
        if self.startbit is not None:
            v = (v >> self.startbit) & ((1 << self.bitcount) - 1)
        if self.enum:
            return self.enum.get(str(v), f"?{v}")
        if self.expr is not None:
            try:
                v = eval(self.expr, {"floor": math.floor}, {"V": v})
            except Exception as e:  # noqa: BLE001 - show the problem inline, keep going
                return f"<{e}>"
        if isinstance(v, bool):
            v = int(v)
        if isinstance(v, float):
            v = round(v, 2)
        return f"{v}{self.units if self.units != 'bit' else ''}"


def to_python(expr):
    """RealDash conversion -> Python expression. Only V, numbers, + - * / ( ), Floor, =, ||, && are used."""
    if not expr:
        return None
    e = expr.replace("||", " or ").replace("&&", " and ").replace("Floor", "floor")
    e = re.sub(r"(?<![<>!=])=(?!=)", "==", e)
    return e


def load_signals(path):
    """-> {can_id: [Signal]}. The name comes from the 'name' attribute or the comment after the <value>."""
    parser = ET.XMLParser(target=ET.TreeBuilder(insert_comments=True))
    root = ET.parse(path, parser).getroot()
    frames = {}
    for frame in root.iter("frame"):
        can_id = int(frame.get("id"), 0)
        children = list(frame)
        for i, el in enumerate(children):
            if el.tag != "value":
                continue
            nxt = children[i + 1] if i + 1 < len(children) else None
            comment = nxt.text.strip() if nxt is not None and nxt.tag is ET.Comment else None
            frames.setdefault(can_id, []).append(Signal(el, comment))
    return frames


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", nargs="?", default="-", help="log file, or - for stdin (default)")
    ap.add_argument("--xml", default=DEFAULT_XML, help=f"RealDash XML (default: {DEFAULT_XML.name})")
    ap.add_argument("--changes", action="store_true", help="print only decoded values that changed")
    args = ap.parse_args()

    signals = load_signals(args.xml)
    last = {}
    src = sys.stdin if args.input == "-" else open(args.input, encoding="utf-8", errors="replace")

    for line in src:
        line = line.rstrip("\r\n")
        m = LINE_RE.match(line)
        if not m:
            if line.startswith("#") and not args.changes:
                print(line, flush=True)
            elif line.startswith("# source=") or line.startswith("# link"):
                print(line, flush=True)
            continue

        ts, can_id = m.group(1), int(m.group(2), 16)
        data = bytes.fromhex(m.group(4))
        decoded = [(s.name, s.decode(data)) for s in signals.get(can_id, [])]

        if args.changes:
            for name, val in decoded:
                key = (can_id, name)
                if last.get(key) != val:
                    print(f"{ts} {can_id:03X} {name}: {last.get(key, '-')} -> {val}", flush=True)
                    last[key] = val
        elif decoded:
            print(f"{line}  | " + ", ".join(f"{n}={v}" for n, v in decoded), flush=True)
        else:
            print(line, flush=True)


if __name__ == "__main__":
    main()
