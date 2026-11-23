#!/usr/bin/env python3
"""Read-only structural parser for Asphalt 8 Android 9.2.0i BGIN resources.

Validates the Clara-family header, hash/offset index and common indexed-entity
envelope. Unknown index tails and unindexed regions are retained as opaque.
This is not a BGIN editor and does not modify APKs or game resources.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path

MAGIC, VERSION, CONST = 0x1AAA, 12, 0x9456A530

class BginError(ValueError):
    pass

def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]

def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]

def decode(data: bytes, filename: str = "<memory>") -> dict:
    if len(data) < 16:
        raise BginError(f"{filename}: too short")
    magic, version = struct.unpack_from("<HH", data, 0)
    constant = u32(data, 4)
    if (magic, version) != (MAGIC, VERSION):
        raise BginError(f"{filename}: unsupported marker/version")
    if constant != CONST:
        raise BginError(f"{filename}: unexpected header constant {constant:#x}")
    name_len = u16(data, 8)
    name_end = 10 + name_len
    if name_end + 6 > len(data):
        raise BginError(f"{filename}: truncated header")
    resource_name = data[10:name_end].decode("utf-8", "strict")
    index_size = u32(data, name_end)
    index_start = name_end + 4
    index_end = index_start + index_size
    if index_end + 2 > len(data):
        raise BginError(f"{filename}: index past EOF")
    bucket_count = u16(data, index_start)
    if 2 + bucket_count * 8 > index_size:
        raise BginError(f"{filename}: index entries exceed index size")
    records, seen = [], set()
    for i in range(bucket_count):
        index_hash, offset = struct.unpack_from("<II", data, index_start + 2 + 8 * i)
        if offset in seen:
            raise BginError(f"{filename}: duplicate offset {offset}")
        seen.add(offset)
        if offset + 10 > len(data):
            raise BginError(f"{filename}: indexed offset outside file: {offset}")
        class_hash, body_size = struct.unpack_from("<II", data, offset)
        body_start = offset + 8
        body_end = body_start + body_size
        if body_size < 2 or body_end > len(data):
            raise BginError(f"{filename}: bad record size at {offset}")
        object_name_len = u16(data, body_start)
        if 2 + object_name_len > body_size:
            raise BginError(f"{filename}: object name exceeds record body at {offset}")
        object_name = data[body_start + 2:body_start + 2 + object_name_len].decode("utf-8", "replace")
        records.append({
            "index": i, "index_hash": index_hash, "offset": offset,
            "class_hash": class_hash, "body_size": body_size,
            "name": object_name, "body_start": body_start,
            "body_end": body_end, "record_end": body_end,
        })
    records.sort(key=lambda item: item["offset"])
    for left, right in zip(records, records[1:]):
        if left["record_end"] > right["offset"]:
            raise BginError(f"{filename}: overlapping indexed records")
    gaps, cursor = [], index_end + 2
    for record in records:
        if cursor < record["offset"]:
            gaps.append({"start": cursor, "end": record["offset"], "size": record["offset"] - cursor})
        cursor = max(cursor, record["record_end"])
    if cursor < len(data):
        gaps.append({"start": cursor, "end": len(data), "size": len(data) - cursor})
    return {
        "format": "asphalt8-bgin-structural", "format_version": 1,
        "filename": filename, "file_size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "marker": magic, "version": version, "header_constant": constant,
        "resource_name": resource_name, "index_size": index_size,
        "bucket_count": bucket_count,
        "opaque_index_tail_hex": data[index_start + 2 + bucket_count * 8:index_end].hex(),
        "record_count": u16(data, index_end),
        "indexed_entity_count": len(records), "records": records,
        "unindexed_gaps": gaps,
    }

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = decode(args.input.read_bytes(), args.input.name)
    rendered = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.json:
        args.json.write_text(rendered, encoding="utf-8")
    else:
        print(rendered)
    print(f"OK {result['resource_name']}: {result['indexed_entity_count']} indexed entities / "
          f"{result['record_count']} total records; {len(result['unindexed_gaps'])} opaque gaps")

if __name__ == "__main__":
    main()
