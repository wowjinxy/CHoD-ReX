#!/usr/bin/env python3
"""
Extract Castlevania: Harmony of Despair game data from a legally obtained
Xbox 360 STFS package.

This script only unpacks the package container. It does not decrypt, patch, or
download game files.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Iterable


TITLE_ID = 0x58410A7A
HEADER_SIDECAR_SIZE = 0x971A
BLOCK_SIZE = 0x1000
HASH_ENTRY_SIZE = 0x18
HASH_TABLE_SIZE = 0x1000
BLOCKS_PER_HASH_LEVEL = (170, 28900, 4913000)
END_OF_CHAIN = 0xFFFFFF

VALID_MAGIC = {b"CON ", b"LIVE", b"PIRS"}


class ExtractionError(RuntimeError):
    pass


@dataclass
class StfsMetadata:
    magic: str
    header_size: int
    content_type: int
    title_id: int
    volume_type: int
    descriptor_length: int
    version: int
    flags: int
    file_table_block_count: int
    file_table_block_number: int
    total_block_count: int
    free_block_count: int
    data_file_count: int
    license_mask: int

    @property
    def read_only_format(self) -> bool:
        return (self.flags & 0x01) != 0

    @property
    def root_active_index(self) -> bool:
        return (self.flags & 0x02) != 0

    @property
    def blocks_per_hash_table(self) -> int:
        return 1 if self.read_only_format else 2


@dataclass
class DirectoryEntry:
    index: int
    name: str
    parent_index: int
    is_directory: bool
    start_block: int
    length: int
    allocated_blocks: int
    path: str = ""
    block_offsets: list[tuple[int, int]] = field(default_factory=list)


def read_u24_le(data: bytes) -> int:
    return data[0] | (data[1] << 8) | (data[2] << 16)


def round_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def safe_join(root: Path, relative_path: str) -> Path:
    parts = []
    for part in relative_path.replace("\\", "/").split("/"):
        if not part or part in (".", ".."):
            raise ExtractionError(f"Unsafe package path: {relative_path!r}")
        if ":" in part:
            raise ExtractionError(f"Unsafe package path: {relative_path!r}")
        parts.append(part)

    target = root.joinpath(*parts).resolve()
    root_resolved = root.resolve()
    if os.path.commonpath([str(root_resolved), str(target)]) != str(root_resolved):
        raise ExtractionError(f"Package path escapes output folder: {relative_path!r}")
    return target


def parse_metadata(header: bytes) -> StfsMetadata:
    if len(header) < HEADER_SIDECAR_SIZE:
        raise ExtractionError("Package is too small to contain an STFS header.")

    magic_raw = header[0:4]
    if magic_raw not in VALID_MAGIC:
        magic = magic_raw.decode("ascii", errors="replace")
        raise ExtractionError(f"Unsupported package magic {magic!r}. Expected CON, LIVE, or PIRS.")

    license_mask = 0
    for i in range(16):
        offset = 0x22C + (i * 0x10)
        license_bits = int.from_bytes(header[offset + 8 : offset + 12], "big")
        license_flags = int.from_bytes(header[offset + 12 : offset + 16], "big")
        if license_flags:
            license_mask |= license_bits

    return StfsMetadata(
        magic=magic_raw.decode("ascii"),
        header_size=int.from_bytes(header[0x340:0x344], "big"),
        content_type=int.from_bytes(header[0x344:0x348], "big"),
        title_id=int.from_bytes(header[0x360:0x364], "big"),
        volume_type=int.from_bytes(header[0x3A9:0x3AD], "big"),
        descriptor_length=header[0x379],
        version=header[0x37A],
        flags=header[0x37B],
        file_table_block_count=int.from_bytes(header[0x37C:0x37E], "little"),
        file_table_block_number=read_u24_le(header[0x37E:0x381]),
        total_block_count=int.from_bytes(header[0x395:0x399], "big"),
        free_block_count=int.from_bytes(header[0x399:0x39D], "big"),
        data_file_count=int.from_bytes(header[0x39D:0x3A1], "big"),
        license_mask=license_mask,
    )


class StfsPackage:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.file: BinaryIO | None = None
        self.metadata: StfsMetadata | None = None
        self._hash_cache: dict[int, bytes] = {}

    def __enter__(self) -> "StfsPackage":
        self.file = self.path.open("rb")
        header = self.file.read(HEADER_SIDECAR_SIZE)
        self.metadata = parse_metadata(header)
        self._validate_metadata()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.file:
            self.file.close()

    def _validate_metadata(self) -> None:
        meta = self.require_metadata()
        if meta.volume_type != 0:
            raise ExtractionError(
                f"This helper supports single-file STFS packages only; volume_type={meta.volume_type}."
            )
        if meta.descriptor_length != 0x24:
            raise ExtractionError(f"Unexpected STFS descriptor length: 0x{meta.descriptor_length:02X}.")
        if meta.data_file_count > 1:
            raise ExtractionError(
                f"Multi-file STFS/SVOD packages are not supported by this helper; data_file_count={meta.data_file_count}."
            )
        if meta.file_table_block_count <= 0:
            raise ExtractionError("Package does not contain a file table.")

    def require_file(self) -> BinaryIO:
        if self.file is None:
            raise ExtractionError("Package is not open.")
        return self.file

    def require_metadata(self) -> StfsMetadata:
        if self.metadata is None:
            raise ExtractionError("Package metadata has not been read.")
        return self.metadata

    def read_at(self, offset: int, size: int) -> bytes:
        f = self.require_file()
        f.seek(offset)
        data = f.read(size)
        if len(data) != size:
            raise ExtractionError(f"Unexpected end of package at 0x{offset:X}.")
        return data

    def block_to_offset(self, block_index: int) -> int:
        meta = self.require_metadata()
        logical_block = block_index
        base = BLOCKS_PER_HASH_LEVEL[0]
        for _ in range(3):
            logical_block += ((block_index + base) // base) * meta.blocks_per_hash_table
            if block_index < base:
                break
            base *= BLOCKS_PER_HASH_LEVEL[0]
        return round_up(meta.header_size, BLOCK_SIZE) + (logical_block << 12)

    def block_to_hash_block_number(self, block_index: int, hash_level: int) -> int:
        meta = self.require_metadata()
        block_step0 = BLOCKS_PER_HASH_LEVEL[0] + meta.blocks_per_hash_table
        block_step1 = BLOCKS_PER_HASH_LEVEL[1] + (
            (BLOCKS_PER_HASH_LEVEL[0] + 1) * meta.blocks_per_hash_table
        )

        if hash_level == 0:
            if block_index < BLOCKS_PER_HASH_LEVEL[0]:
                return 0

            block = (block_index // BLOCKS_PER_HASH_LEVEL[0]) * block_step0
            block += ((block_index // BLOCKS_PER_HASH_LEVEL[1]) + 1) * meta.blocks_per_hash_table
            if block_index < BLOCKS_PER_HASH_LEVEL[1]:
                return block
            return block + meta.blocks_per_hash_table

        if hash_level == 1:
            if block_index < BLOCKS_PER_HASH_LEVEL[1]:
                return block_step0

            block = (block_index // BLOCKS_PER_HASH_LEVEL[1]) * block_step1
            return block + meta.blocks_per_hash_table

        return block_step1

    def block_to_hash_offset(self, block_index: int, hash_level: int) -> int:
        meta = self.require_metadata()
        hash_block = self.block_to_hash_block_number(block_index, hash_level)
        return round_up(meta.header_size, BLOCK_SIZE) + (hash_block << 12)

    def hash_table_at(self, offset: int) -> bytes:
        table = self._hash_cache.get(offset)
        if table is None:
            table = self.read_at(offset, HASH_TABLE_SIZE)
            self._hash_cache[offset] = table
        return table

    def next_block(self, block_index: int) -> int:
        meta = self.require_metadata()
        secondary_offset = BLOCK_SIZE if meta.root_active_index else 0

        if meta.read_only_format:
            secondary_offset = 0
        elif meta.total_block_count > BLOCKS_PER_HASH_LEVEL[0]:
            if meta.total_block_count > BLOCKS_PER_HASH_LEVEL[1]:
                level2_offset = self.block_to_hash_offset(block_index, 2) + secondary_offset
                level2_table = self.hash_table_at(level2_offset)
                level2_record = (block_index // BLOCKS_PER_HASH_LEVEL[1]) % BLOCKS_PER_HASH_LEVEL[0]
                level2_info = struct.unpack_from(">I", level2_table, level2_record * HASH_ENTRY_SIZE + 0x14)[0]
                secondary_offset = BLOCK_SIZE if (level2_info & 0x40000000) else 0

            level1_offset = self.block_to_hash_offset(block_index, 1) + secondary_offset
            level1_table = self.hash_table_at(level1_offset)
            level1_record = (block_index // BLOCKS_PER_HASH_LEVEL[0]) % BLOCKS_PER_HASH_LEVEL[0]
            level1_info = struct.unpack_from(">I", level1_table, level1_record * HASH_ENTRY_SIZE + 0x14)[0]
            secondary_offset = BLOCK_SIZE if (level1_info & 0x40000000) else 0

        level0_offset = self.block_to_hash_offset(block_index, 0) + secondary_offset
        level0_table = self.hash_table_at(level0_offset)
        record = block_index % BLOCKS_PER_HASH_LEVEL[0]
        info = struct.unpack_from(">I", level0_table, record * HASH_ENTRY_SIZE + 0x14)[0]
        return info & 0xFFFFFF

    def read_file_table_blocks(self) -> Iterable[bytes]:
        meta = self.require_metadata()
        table_block = meta.file_table_block_number
        for _ in range(meta.file_table_block_count):
            if table_block == END_OF_CHAIN:
                break
            yield self.read_at(self.block_to_offset(table_block), BLOCK_SIZE)
            table_block = self.next_block(table_block)

    def parse_directory_entries(self) -> list[DirectoryEntry]:
        entries: list[DirectoryEntry] = []

        for block in self.read_file_table_blocks():
            for offset in range(0, BLOCK_SIZE, 0x40):
                raw = block[offset : offset + 0x40]
                if raw[0] == 0:
                    continue

                flags = raw[0x28]
                name_length = flags & 0x3F
                name = raw[0:40][:name_length].decode("utf-8", errors="replace")
                parent_index = int.from_bytes(raw[0x32:0x34], "big")
                entry = DirectoryEntry(
                    index=len(entries),
                    name=name,
                    parent_index=parent_index,
                    is_directory=(flags & 0x80) != 0,
                    start_block=read_u24_le(raw[0x2F:0x32]),
                    length=int.from_bytes(raw[0x34:0x38], "big"),
                    allocated_blocks=read_u24_le(raw[0x2C:0x2F]),
                )

                if parent_index == 0xFFFF:
                    entry.path = name
                elif parent_index < len(entries):
                    parent_path = entries[parent_index].path
                    entry.path = f"{parent_path}/{name}" if parent_path else name
                else:
                    raise ExtractionError(
                        f"Entry {name!r} references future parent index {parent_index}."
                    )

                if not entry.is_directory:
                    self.populate_block_offsets(entry)

                entries.append(entry)

        return entries

    def populate_block_offsets(self, entry: DirectoryEntry) -> None:
        remaining = entry.length
        block_index = entry.start_block
        while remaining > 0 and block_index != END_OF_CHAIN:
            block_size = min(BLOCK_SIZE, remaining)
            entry.block_offsets.append((self.block_to_offset(block_index), block_size))
            remaining -= block_size
            block_index = self.next_block(block_index)

        if remaining:
            raise ExtractionError(
                f"File {entry.path!r} ended {remaining} bytes early while following its block chain."
            )


def should_extract(path: str, patterns: list[str]) -> bool:
    if not patterns:
        return True
    normalized = path.replace("\\", "/")
    return any(fnmatch.fnmatchcase(normalized, pattern.replace("\\", "/")) for pattern in patterns)


def output_path_for_entry(path: str, orig_executables: bool) -> str:
    if not orig_executables:
        return path

    normalized = path.replace("\\", "/")
    lower = normalized.lower()
    if lower == "default.xex":
        return "default.xex.orig"
    if lower.startswith("data/player_dll/") and lower.endswith(".dll"):
        return f"{normalized}.orig"
    return path


def extract_entries(
    package: StfsPackage,
    entries: list[DirectoryEntry],
    output_root: Path,
    patterns: list[str],
    overwrite: bool,
    orig_executables: bool,
) -> tuple[int, int]:
    files_written = 0
    bytes_written = 0
    f = package.require_file()

    for entry in entries:
        if entry.is_directory or not should_extract(entry.path, patterns):
            continue

        target = safe_join(output_root, output_path_for_entry(entry.path, orig_executables))
        if target.exists() and not overwrite:
            raise ExtractionError(f"Refusing to overwrite existing file: {target}")

        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("wb") as out:
            for offset, size in entry.block_offsets:
                f.seek(offset)
                remaining = size
                while remaining:
                    chunk = f.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise ExtractionError(f"Unexpected end of package while reading {entry.path!r}.")
                    out.write(chunk)
                    remaining -= len(chunk)

        files_written += 1
        bytes_written += entry.length

    return files_written, bytes_written


def write_header_sidecar(package_path: Path, output_root: Path, title_id: int, overwrite: bool) -> Path:
    sidecar = output_root / f"{title_id:08X}.stfsheader"
    if sidecar.exists() and not overwrite:
        raise ExtractionError(f"Refusing to overwrite existing file: {sidecar}")

    output_root.mkdir(parents=True, exist_ok=True)
    with package_path.open("rb") as source, sidecar.open("wb") as out:
        data = source.read(HEADER_SIDECAR_SIZE)
        if len(data) != HEADER_SIDECAR_SIZE:
            raise ExtractionError("Package is too small to export the STFS header sidecar.")
        out.write(data)
    return sidecar


def default_output_path() -> Path:
    project_root = Path(__file__).resolve().parents[1]
    return project_root.parent / "Castlevania-Harmony-of-Despair"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract Castlevania: Harmony of Despair files from an Xbox 360 LIVE/PIRS/CON package.",
    )
    parser.add_argument("package", type=Path, help="Path to the STFS package file.")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=default_output_path(),
        help="Output game-data folder. Defaults to ../Castlevania-Harmony-of-Despair.",
    )
    parser.add_argument(
        "--only",
        action="append",
        default=[],
        metavar="GLOB",
        help="Extract only matching package paths, for example --only data/* --only default.xex.",
    )
    parser.add_argument("--list", action="store_true", help="List package files without extracting.")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite files that already exist.")
    parser.add_argument(
        "--orig-executables",
        action="store_true",
        help=(
            "Write default.xex and data/player_dll/*.dll as .orig files. "
            "Useful for developer dumps that keep expanded/decrypted executables under the active names."
        ),
    )
    parser.add_argument(
        "--no-header-sidecar",
        action="store_true",
        help="Do not write the small <title-id>.stfsheader metadata sidecar.",
    )
    parser.add_argument(
        "--allow-title-mismatch",
        action="store_true",
        help="Allow packages whose title id is not 58410A7A.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    package_path = args.package.expanduser().resolve()
    output_root = args.output.expanduser().resolve()

    if not package_path.is_file():
        print(f"error: package file not found: {package_path}", file=sys.stderr)
        return 2

    try:
        with StfsPackage(package_path) as package:
            meta = package.require_metadata()
            if meta.title_id != TITLE_ID and not args.allow_title_mismatch:
                raise ExtractionError(
                    f"Package title id is 0x{meta.title_id:08X}, expected 0x{TITLE_ID:08X}."
                )

            entries = package.parse_directory_entries()
            files = [entry for entry in entries if not entry.is_directory]
            selected = [entry for entry in files if should_extract(entry.path, args.only)]

            print(f"Package: {package_path}")
            print(f"Magic: {meta.magic.strip()}  TitleId: 0x{meta.title_id:08X}  ContentType: 0x{meta.content_type:08X}")
            print(f"Files: {len(files)} total, {len(selected)} selected")
            print(f"LicenseMask: 0x{meta.license_mask:08X}")

            if args.list:
                for entry in selected:
                    print(f"{entry.length:>10}  {entry.path}")
                return 0

            output_root.mkdir(parents=True, exist_ok=True)
            files_written, bytes_written = extract_entries(
                package=package,
                entries=entries,
                output_root=output_root,
                patterns=args.only,
                overwrite=args.overwrite,
                orig_executables=args.orig_executables,
            )

            sidecar = None
            if not args.no_header_sidecar:
                sidecar = write_header_sidecar(package_path, output_root, meta.title_id, args.overwrite)

            print(f"Output: {output_root}")
            print(f"Extracted: {files_written} files, {bytes_written:,} bytes")
            if sidecar:
                print(f"Header sidecar: {sidecar}")

    except (OSError, ExtractionError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
