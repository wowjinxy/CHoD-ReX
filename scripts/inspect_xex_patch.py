#!/usr/bin/env python3
"""
Inspect Xbox 360 XEX delta patch files (.xexp / .dllp).

This is a decomposition helper for title updates. It parses patch metadata and
verifies whether a patch matches a base XEX module. It does not apply LZX delta
records yet; use it to identify patch scope and expected base modules first.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


XEX2_MAGIC = b"XEX2"
XEX_HEADER_FILE_FORMAT_INFO = 0x000003FF
XEX_HEADER_DELTA_PATCH_DESCRIPTOR = 0x000005FF
XEX_HEADER_EXECUTION_INFO = 0x00040006
XEX_MODULE_MODULE_PATCH = 0x00000010
XEX_MODULE_PATCH_FULL = 0x00000020
XEX_MODULE_PATCH_DELTA = 0x00000040
XEX_ENCRYPTION_NAMES = {0: "none", 1: "normal"}
XEX_COMPRESSION_NAMES = {0: "none", 1: "basic", 2: "normal", 3: "delta"}


class XexParseError(RuntimeError):
    pass


@dataclass
class XexVersion:
    major: int
    minor: int
    build: int
    qfe: int
    value: int

    @classmethod
    def from_value(cls, value: int) -> "XexVersion":
        return cls(
            major=value & 0xF,
            minor=(value >> 4) & 0xF,
            build=(value >> 8) & 0xFFFF,
            qfe=(value >> 24) & 0xFF,
            value=value,
        )

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.build}.{self.qfe}"


@dataclass
class DeltaRecord:
    old_addr: int
    new_addr: int
    uncompressed_len: int
    compressed_len: int
    kind: str
    file_offset: int


@dataclass
class XexSummary:
    path: str
    module_flags: int
    header_size: int
    security_offset: int
    header_count: int
    is_patch: bool
    image_size: int | None
    load_address: int | None
    rsa_signature_sha1: str | None
    encryption: str | None
    compression: str | None
    window_size: int | None
    execution_title_id: int | None
    execution_version: str | None
    execution_base_version: str | None


@dataclass
class PatchSummary:
    patch: XexSummary
    source_version: str
    target_version: str
    digest_source: str
    image_key_source: str
    size_of_target_headers: int
    delta_headers_source_offset: int
    delta_headers_source_size: int
    delta_headers_target_offset: int
    delta_image_source_offset: int
    delta_image_source_size: int
    delta_image_target_offset: int
    header_record: DeltaRecord
    base_path: str | None
    base_digest_match: bool | None
    base_digest: str | None
    body_records_note: str


def u16be(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "big")


def u32be(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "big")


def bytes_hex(data: bytes) -> str:
    return data.hex().upper()


def require_range(data: bytes, offset: int, size: int, what: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise XexParseError(f"{what} extends outside file/header: offset=0x{offset:X}, size=0x{size:X}")


def parse_xex_header(data: bytes, path: Path) -> tuple[XexSummary, dict[int, int]]:
    require_range(data, 0, 0x18, "XEX header")
    if data[0:4] != XEX2_MAGIC:
        raise XexParseError(f"{path} is not an XEX2 file.")

    module_flags = u32be(data, 0x04)
    header_size = u32be(data, 0x08)
    security_offset = u32be(data, 0x10)
    header_count = u32be(data, 0x14)
    require_range(data, 0, header_size, "XEX header_size")
    require_range(data, 0x18, header_count * 8, "XEX optional headers")

    opt_headers: dict[int, int] = {}
    for i in range(header_count):
        offset = 0x18 + (i * 8)
        key = u32be(data, offset)
        value_or_offset = u32be(data, offset + 4)
        opt_headers[key] = value_or_offset

    image_size = None
    load_address = None
    rsa_signature_sha1 = None
    if security_offset:
        require_range(data, security_offset, 0x184, "XEX security info")
        image_size = u32be(data, security_offset + 0x04)
        load_address = u32be(data, security_offset + 0x110)
        rsa_signature_sha1 = hashlib.sha1(data[security_offset + 0x08 : security_offset + 0x108]).hexdigest().upper()

    encryption = None
    compression = None
    window_size = None
    file_format_offset = opt_headers.get(XEX_HEADER_FILE_FORMAT_INFO)
    if file_format_offset is not None:
        require_range(data, file_format_offset, 0x10, "XEX file format info")
        info_size = u32be(data, file_format_offset)
        require_range(data, file_format_offset, info_size, "XEX file format info")
        encryption_value = u16be(data, file_format_offset + 0x04)
        compression_value = u16be(data, file_format_offset + 0x06)
        encryption = XEX_ENCRYPTION_NAMES.get(encryption_value, f"unknown:{encryption_value}")
        compression = XEX_COMPRESSION_NAMES.get(compression_value, f"unknown:{compression_value}")
        if compression_value in (2, 3) and info_size >= 0x20:
            window_size = u32be(data, file_format_offset + 0x08)

    execution_title_id = None
    execution_version = None
    execution_base_version = None
    execution_offset = opt_headers.get(XEX_HEADER_EXECUTION_INFO)
    if execution_offset is not None:
        require_range(data, execution_offset, 0x18, "XEX execution info")
        execution_version = str(XexVersion.from_value(u32be(data, execution_offset + 0x04)))
        execution_base_version = str(XexVersion.from_value(u32be(data, execution_offset + 0x08)))
        execution_title_id = u32be(data, execution_offset + 0x0C)

    summary = XexSummary(
        path=str(path),
        module_flags=module_flags,
        header_size=header_size,
        security_offset=security_offset,
        header_count=header_count,
        is_patch=(module_flags & (XEX_MODULE_MODULE_PATCH | XEX_MODULE_PATCH_FULL | XEX_MODULE_PATCH_DELTA)) != 0,
        image_size=image_size,
        load_address=load_address,
        rsa_signature_sha1=rsa_signature_sha1,
        encryption=encryption,
        compression=compression,
        window_size=window_size,
        execution_title_id=execution_title_id,
        execution_version=execution_version,
        execution_base_version=execution_base_version,
    )
    return summary, opt_headers


def parse_delta_record(data: bytes, offset: int) -> DeltaRecord:
    require_range(data, offset, 0x0C, "delta record")
    old_addr = u32be(data, offset)
    new_addr = u32be(data, offset + 0x04)
    uncompressed_len = u16be(data, offset + 0x08)
    compressed_len = u16be(data, offset + 0x0A)
    if old_addr == 0 and new_addr == 0 and uncompressed_len == 0 and compressed_len == 0:
        kind = "terminator"
    elif compressed_len == 0:
        kind = "zero"
    elif compressed_len == 1:
        kind = "copy"
    else:
        kind = "lzx_delta"
    return DeltaRecord(
        old_addr=old_addr,
        new_addr=new_addr,
        uncompressed_len=uncompressed_len,
        compressed_len=compressed_len,
        kind=kind,
        file_offset=offset,
    )


def inspect_patch(patch_path: Path, base_path: Path | None = None) -> PatchSummary:
    patch_data = patch_path.read_bytes()
    patch_summary, opt_headers = parse_xex_header(patch_data, patch_path)
    if not patch_summary.is_patch:
        raise XexParseError(f"{patch_path} is not marked as an XEX patch.")

    descriptor_offset = opt_headers.get(XEX_HEADER_DELTA_PATCH_DESCRIPTOR)
    if descriptor_offset is None:
        raise XexParseError(f"{patch_path} has no delta patch descriptor optional header.")
    require_range(patch_data, descriptor_offset, 0x58, "delta patch descriptor")

    descriptor_size = u32be(patch_data, descriptor_offset)
    require_range(patch_data, descriptor_offset, descriptor_size, "delta patch descriptor")

    target_version = XexVersion.from_value(u32be(patch_data, descriptor_offset + 0x04))
    source_version = XexVersion.from_value(u32be(patch_data, descriptor_offset + 0x08))
    digest_source = bytes_hex(patch_data[descriptor_offset + 0x0C : descriptor_offset + 0x20])
    image_key_source = bytes_hex(patch_data[descriptor_offset + 0x20 : descriptor_offset + 0x30])
    size_of_target_headers = u32be(patch_data, descriptor_offset + 0x30)
    delta_headers_source_offset = u32be(patch_data, descriptor_offset + 0x34)
    delta_headers_source_size = u32be(patch_data, descriptor_offset + 0x38)
    delta_headers_target_offset = u32be(patch_data, descriptor_offset + 0x3C)
    delta_image_source_offset = u32be(patch_data, descriptor_offset + 0x40)
    delta_image_source_size = u32be(patch_data, descriptor_offset + 0x44)
    delta_image_target_offset = u32be(patch_data, descriptor_offset + 0x48)
    header_record = parse_delta_record(patch_data, descriptor_offset + 0x4C)

    base_digest = None
    base_digest_match = None
    if base_path is not None and base_path.exists():
        base_data = base_path.read_bytes()
        base_summary, _ = parse_xex_header(base_data, base_path)
        base_digest = base_summary.rsa_signature_sha1
        base_digest_match = base_digest == digest_source

    if patch_summary.encryption == "none":
        body_records_note = (
            "Patch body is not encrypted; individual image delta records can be parsed in a follow-up pass."
        )
    else:
        body_records_note = (
            "Patch body is encrypted; use the SDK/XexModule apply path or a decrypt-capable follow-up tool "
            "to expand image delta records."
        )

    return PatchSummary(
        patch=patch_summary,
        source_version=str(source_version),
        target_version=str(target_version),
        digest_source=digest_source,
        image_key_source=image_key_source,
        size_of_target_headers=size_of_target_headers,
        delta_headers_source_offset=delta_headers_source_offset,
        delta_headers_source_size=delta_headers_source_size,
        delta_headers_target_offset=delta_headers_target_offset,
        delta_image_source_offset=delta_image_source_offset,
        delta_image_source_size=delta_image_source_size,
        delta_image_target_offset=delta_image_target_offset,
        header_record=header_record,
        base_path=str(base_path) if base_path else None,
        base_digest_match=base_digest_match,
        base_digest=base_digest,
        body_records_note=body_records_note,
    )


def base_candidates_for_patch(patch_path: Path, base_root: Path, tu_root: Path | None) -> list[Path]:
    if patch_path.name.lower() == "default.xexp":
        rel = Path("default.xex")
    elif patch_path.name.lower().endswith(".dllp"):
        if tu_root:
            try:
                rel = patch_path.relative_to(tu_root).with_name(patch_path.name[:-1])
            except ValueError:
                rel = Path("data/player_dll") / patch_path.name[:-1]
        else:
            rel = Path("data/player_dll") / patch_path.name[:-1]
    else:
        rel = Path(patch_path.name[:-1]) if patch_path.name.lower().endswith("p") else patch_path.name

    direct = base_root / rel
    candidates = [direct.with_name(direct.name + ".orig"), direct]
    return [candidate for candidate in candidates if candidate.exists()]


def find_patch_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    return sorted(
        p
        for p in path.rglob("*")
        if p.is_file() and (p.suffix.lower() == ".xexp" or p.name.lower().endswith(".dllp"))
    )


def print_text(summary: PatchSummary) -> None:
    patch = summary.patch
    print(f"Patch: {patch.path}")
    print(f"  flags: 0x{patch.module_flags:08X}  header_size: 0x{patch.header_size:X}")
    print(f"  encryption: {patch.encryption}  compression: {patch.compression}  window: {patch.window_size}")
    print(f"  source_version: {summary.source_version}")
    print(f"  target_version: {summary.target_version}")
    print(f"  digest_source: {summary.digest_source}")
    if summary.base_path:
        match = "MATCH" if summary.base_digest_match else "MISMATCH"
        print(f"  base: {summary.base_path}")
        print(f"  base_digest: {summary.base_digest} ({match})")
    print(
        "  header_copy: "
        f"source=0x{summary.delta_headers_source_offset:X}+0x{summary.delta_headers_source_size:X} "
        f"-> target=0x{summary.delta_headers_target_offset:X}; "
        f"target_headers=0x{summary.size_of_target_headers:X}"
    )
    print(
        "  image_copy: "
        f"source=0x{summary.delta_image_source_offset:X}+0x{summary.delta_image_source_size:X} "
        f"-> target=0x{summary.delta_image_target_offset:X}"
    )
    record = summary.header_record
    print(
        "  header_delta_record: "
        f"{record.kind} old=0x{record.old_addr:X} new=0x{record.new_addr:X} "
        f"uncompressed=0x{record.uncompressed_len:X} compressed=0x{record.compressed_len:X}"
    )
    print(f"  body: {summary.body_records_note}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inspect XEX title-update patch files.")
    parser.add_argument("path", type=Path, help="Patch file or TU Cache folder.")
    parser.add_argument("--base", type=Path, help="Specific base XEX to verify against.")
    parser.add_argument("--base-root", type=Path, help="Base extracted game root for automatic patch pairing.")
    parser.add_argument("--json", action="store_true", help="Emit JSON.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    target = args.path.expanduser().resolve()
    if not target.exists():
        print(f"error: not found: {target}", file=sys.stderr)
        return 2

    base_root = args.base_root.expanduser().resolve() if args.base_root else None
    base = args.base.expanduser().resolve() if args.base else None
    patch_files = find_patch_files(target)
    summaries = []

    try:
        for patch_file in patch_files:
            selected_base = base
            if selected_base is None and base_root is not None:
                candidates = base_candidates_for_patch(
                    patch_file,
                    base_root,
                    target if target.is_dir() else None,
                )
                selected_base = candidates[0] if candidates else None

            summaries.append(inspect_patch(patch_file, selected_base))
    except (OSError, XexParseError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps([asdict(summary) for summary in summaries], indent=2))
    else:
        for i, summary in enumerate(summaries):
            if i:
                print()
            print_text(summary)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
