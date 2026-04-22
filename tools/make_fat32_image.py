#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Sequence, Union

SECTOR_SIZE = 512
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1
ROOT_CLUSTER = 2
EOC = 0x0FFFFFFF
MEDIA_DESCRIPTOR = 0xF8


@dataclass
class FileNode:
    name: str
    data: bytes
    clusters: List[int] = field(default_factory=list)


@dataclass
class DirNode:
    name: str
    children: List[Union["DirNode", FileNode]] = field(default_factory=list)
    clusters: List[int] = field(default_factory=list)


def compute_layout(total_sectors: int) -> tuple[int, int, int]:
    sectors_per_fat = 1
    for _ in range(32):
        data_sectors = total_sectors - RESERVED_SECTORS - FAT_COUNT * sectors_per_fat
        if data_sectors <= 0:
            raise ValueError("invalid FAT32 layout: no data sectors")
        data_clusters = data_sectors // SECTORS_PER_CLUSTER
        required_fat_entries = data_clusters + 2
        new_sectors_per_fat = (required_fat_entries * 4 + (SECTOR_SIZE - 1)) // SECTOR_SIZE
        if new_sectors_per_fat == sectors_per_fat:
            break
        sectors_per_fat = new_sectors_per_fat
    else:
        raise ValueError("failed to converge FAT32 layout")

    fat_entries = (sectors_per_fat * SECTOR_SIZE) // 4
    data_sectors = total_sectors - RESERVED_SECTORS - FAT_COUNT * sectors_per_fat
    data_clusters = data_sectors // SECTORS_PER_CLUSTER
    total_clusters = data_clusters + 2
    if total_clusters > fat_entries:
        raise ValueError("FAT too small for data clusters")
    return sectors_per_fat, total_clusters, RESERVED_SECTORS + FAT_COUNT * sectors_per_fat


def is_valid_short_char(ch: str) -> bool:
    return ch.isalnum() or ch == "_"


def short_name_11(name: str) -> bytes:
    upper = name.upper()
    if upper in {".", ".."}:
        raise ValueError("dot entries must be generated internally")
    if upper.count(".") > 1:
        raise ValueError(f"invalid 8.3 name: {name}")

    if "." in upper:
        base, ext = upper.split(".", 1)
    else:
        base, ext = upper, ""

    if not base or len(base) > 8 or len(ext) > 3:
        raise ValueError(f"name does not fit 8.3 format: {name}")
    if not all(is_valid_short_char(ch) for ch in base + ext):
        raise ValueError(f"name contains unsupported FAT character: {name}")

    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def build_tree(path: Path, name: str = "") -> DirNode:
    node = DirNode(name=name)
    children = []
    for child in path.iterdir():
        if child.name == ".DS_Store" or child.name.startswith("._"):
            continue
        children.append(child)

    for child in sorted(children, key=lambda p: p.name.upper()):
        if child.is_dir():
            node.children.append(build_tree(child, child.name))
        elif child.is_file():
            node.children.append(FileNode(name=child.name, data=child.read_bytes()))
    return node


def allocate_clusters(root: DirNode, total_clusters: int) -> None:
    next_cluster = ROOT_CLUSTER

    def alloc(count: int) -> List[int]:
        nonlocal next_cluster
        if count <= 0:
            return []
        first = next_cluster
        end = next_cluster + count
        if end > total_clusters:
            raise ValueError("image capacity exceeded while allocating clusters")
        next_cluster = end
        return list(range(first, end))

    def walk_dir(node: DirNode, is_root: bool) -> None:
        entry_count = len(node.children) + (0 if is_root else 2)
        needed_clusters = max(1, math.ceil(entry_count / (SECTOR_SIZE // 32)))
        node.clusters = alloc(needed_clusters)

        seen = set()
        for child in node.children:
            sn = short_name_11(child.name)
            if sn in seen:
                raise ValueError(f"duplicate short name in directory '{node.name or '/'}': {child.name}")
            seen.add(sn)

        for child in node.children:
            if isinstance(child, DirNode):
                walk_dir(child, is_root=False)
            else:
                cluster_count = math.ceil(len(child.data) / (SECTOR_SIZE * SECTORS_PER_CLUSTER))
                child.clusters = alloc(cluster_count)

    walk_dir(root, is_root=True)
    if not root.clusters or root.clusters[0] != ROOT_CLUSTER:
        raise ValueError("root directory cluster is not 2")


def set_fat_chain(fat: List[int], chain: Sequence[int]) -> None:
    if not chain:
        return
    for index, cluster in enumerate(chain):
        if cluster <= 1 or cluster >= len(fat):
            raise ValueError(f"cluster out of FAT range: {cluster}")
        fat[cluster] = EOC if index == len(chain) - 1 else chain[index + 1]


def pack_dir_entry(short_name: bytes, attr: int, first_cluster: int, size: int) -> bytes:
    hi = (first_cluster >> 16) & 0xFFFF
    lo = first_cluster & 0xFFFF
    return struct.pack(
        "<11sBBBHHHHHHHI",
        short_name,
        attr,
        0,
        0,
        0,
        0,
        0,
        hi,
        0,
        0,
        lo,
        size,
    )


def dot_entry(name: str, cluster: int) -> bytes:
    if name == ".":
        raw = b"." + b" " * 10
    elif name == "..":
        raw = b".." + b" " * 9
    else:
        raise ValueError("invalid dot entry")
    return pack_dir_entry(raw, 0x10, cluster, 0)


def write_cluster_chain(image: bytearray, data_lba: int, chain: Sequence[int], data: bytes) -> None:
    if not chain:
        return
    cluster_size = SECTOR_SIZE * SECTORS_PER_CLUSTER
    total_bytes = len(chain) * cluster_size
    if len(data) > total_bytes:
        raise ValueError("cluster chain too small for data")

    offset = 0
    for cluster in chain:
        lba = data_lba + (cluster - 2) * SECTORS_PER_CLUSTER
        image_offset = lba * SECTOR_SIZE
        chunk = data[offset:offset + cluster_size]
        image[image_offset:image_offset + len(chunk)] = chunk
        offset += cluster_size


def write_directories(image: bytearray, data_lba: int, node: DirNode, parent_cluster: int | None, is_root: bool) -> None:
    cluster_bytes = len(node.clusters) * SECTOR_SIZE * SECTORS_PER_CLUSTER
    payload = bytearray(cluster_bytes)
    offset = 0

    if not is_root:
        payload[offset:offset + 32] = dot_entry(".", node.clusters[0])
        offset += 32
        payload[offset:offset + 32] = dot_entry("..", parent_cluster if parent_cluster is not None else ROOT_CLUSTER)
        offset += 32

    for child in node.children:
        short = short_name_11(child.name)
        if isinstance(child, DirNode):
            first_cluster = child.clusters[0]
            attr = 0x10
            size = 0
        else:
            first_cluster = child.clusters[0] if child.clusters else 0
            attr = 0x20
            size = len(child.data)

        payload[offset:offset + 32] = pack_dir_entry(short, attr, first_cluster, size)
        offset += 32

    if offset < len(payload):
        payload[offset] = 0x00

    write_cluster_chain(image, data_lba, node.clusters, bytes(payload))

    for child in node.children:
        if isinstance(child, DirNode):
            write_directories(image, data_lba, child, node.clusters[0], is_root=False)
        else:
            write_cluster_chain(image, data_lba, child.clusters, child.data)


def build_boot_sector(total_sectors: int, sectors_per_fat: int, volume_label: str) -> bytes:
    label = volume_label.upper().encode("ascii")[:11].ljust(11, b" ")
    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"MSWIN4.1"
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, 0)
    struct.pack_into("<H", boot, 19, 0)
    boot[21] = MEDIA_DESCRIPTOR
    struct.pack_into("<H", boot, 22, 0)
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 255)
    struct.pack_into("<I", boot, 28, 0)
    struct.pack_into("<I", boot, 32, total_sectors)
    struct.pack_into("<I", boot, 36, sectors_per_fat)
    struct.pack_into("<H", boot, 40, 0)
    struct.pack_into("<H", boot, 42, 0)
    struct.pack_into("<I", boot, 44, ROOT_CLUSTER)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, 0x534F4E46)
    boot[71:82] = label
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"
    return bytes(boot)


def build_fsinfo_sector() -> bytes:
    fsinfo = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    return bytes(fsinfo)


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a simple FAT32 disk image from a source directory")
    parser.add_argument("--src", required=True, help="Source directory to place at FAT root")
    parser.add_argument("--out", required=True, help="Output image path")
    parser.add_argument("--size-mb", type=int, required=True, help="Image size in MiB")
    parser.add_argument("--volume-label", default="FUNNYOS", help="FAT volume label")
    args = parser.parse_args()

    src = Path(args.src)
    out = Path(args.out)
    if not src.is_dir():
        raise ValueError(f"source directory does not exist: {src}")
    if args.size_mb <= 0:
        raise ValueError("size must be positive")

    total_sectors = (args.size_mb * 1024 * 1024) // SECTOR_SIZE
    sectors_per_fat, total_clusters, data_lba = compute_layout(total_sectors)

    root = build_tree(src)
    allocate_clusters(root, total_clusters)

    image = bytearray(total_sectors * SECTOR_SIZE)
    boot = build_boot_sector(total_sectors, sectors_per_fat, args.volume_label)
    fsinfo = build_fsinfo_sector()

    image[0:SECTOR_SIZE] = boot
    image[SECTOR_SIZE:2 * SECTOR_SIZE] = fsinfo

    backup_boot_lba = 6
    backup_fsinfo_lba = 7
    image[backup_boot_lba * SECTOR_SIZE:(backup_boot_lba + 1) * SECTOR_SIZE] = boot
    image[backup_fsinfo_lba * SECTOR_SIZE:(backup_fsinfo_lba + 1) * SECTOR_SIZE] = fsinfo

    fat_entries = (sectors_per_fat * SECTOR_SIZE) // 4
    fat = [0] * fat_entries
    fat[0] = 0x0FFFFFF8
    fat[1] = EOC

    def register_chains(node: DirNode) -> None:
        set_fat_chain(fat, node.clusters)
        for child in node.children:
            if isinstance(child, DirNode):
                register_chains(child)
            else:
                set_fat_chain(fat, child.clusters)

    register_chains(root)

    fat_blob = bytearray(sectors_per_fat * SECTOR_SIZE)
    for idx, value in enumerate(fat):
        struct.pack_into("<I", fat_blob, idx * 4, value)

    first_fat_lba = RESERVED_SECTORS
    for fat_index in range(FAT_COUNT):
        lba = first_fat_lba + fat_index * sectors_per_fat
        start = lba * SECTOR_SIZE
        end = start + len(fat_blob)
        image[start:end] = fat_blob

    write_directories(image, data_lba, root, parent_cluster=None, is_root=True)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
