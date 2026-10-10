"""Read-only release provenance, ESP image verification and artifact sealing.

No GitHub write API, tag creation, release upload or firmware flashing exists here.
"""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

from release_version import DEVICES, asset_name, firmware_version, resolve_release

REPOSITORY = "meat-holic/CrossTwins"
SDK_SHA = "539da34db67a546cad961525d7e9b6364dfa2fe4"
PLATFORM = "https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip"
PROFILES = {
    "x3-x4": ("gh_release_rc", 5, "x4"),
    "sticky": ("sticky-gh_release_rc", 9, "sticky"),
    "x4pro": ("x4pro-gh_release_rc", 9, "x4pro"),
    "x4c": ("x4c-gh_release_rc", 9, "x4c"),
    "papermono": ("papermono-gh_release_rc", 9, "papermono"),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    require(not path.exists(), f"refusing overwrite: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")


def context(repository, tag, commit, workflow_sha, sdk, base, run_id, attempt, local=False):
    require(repository == REPOSITORY, "wrong repository")
    require(bool(re.fullmatch(r"[0-9a-f]{40}", commit)), "commit must be a full lowercase SHA")
    require(commit == workflow_sha, "workflow revision and target SHA must match")
    require(sdk == SDK_SHA, "SDK gitlink differs from approved revision")
    require(bool(re.fullmatch(r"[1-9][0-9]*", run_id)), "invalid run ID")
    require(bool(re.fullmatch(r"[1-9][0-9]*", attempt)), "invalid run attempt")
    match = re.fullmatch(r"v" + re.escape(base) + r"-ct\.([1-9][0-9]*)", tag)
    require(match is not None and int(match[1]) >= 3, "only canonical ct.3+ tags may be prepared")
    release = resolve_release(tag, base, False)
    version = firmware_version(release, base, commit[:7])
    require(len(version.encode("ascii")) < 32, "version exceeds descriptor field")
    return {
        "schema_version": 1, "repository": repository, "release_tag": tag,
        "commit_sha": commit, "workflow_sha": workflow_sha, "sdk_gitlink_sha": sdk,
        "base_version": base, "prerelease": False, "run_id": run_id, "run_attempt": attempt,
        "mode": "local" if local else "github-actions",
        "firmware_version": version,
        "firmware_suffix": release.firmware_suffix, "platform": PLATFORM,
    }


def validate_context(value):
    require(value["prerelease"] is False and type(value["schema_version"]) is int,
            "invalid context field types")
    expected = context(value["repository"], value["release_tag"], value["commit_sha"],
                       value["workflow_sha"], value["sdk_gitlink_sha"], value["base_version"],
                       value["run_id"], value["run_attempt"], value["mode"] == "local")
    require(value == expected, "context contract mismatch")


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def prepare(args):
    require(args.prerelease == "false", "this preflight prepares regular CrossTwins releases only")
    require(git("rev-parse", "HEAD") == args.commit_sha, "checkout HEAD mismatch")
    link = git("ls-tree", "HEAD", "freeink-sdk").split()
    require(len(link) == 4 and link[:2] == ["160000", "commit"], "missing SDK gitlink")
    require(git("-C", "freeink-sdk", "rev-parse", "HEAD") == link[2], "SDK checkout mismatch")
    require(not git("-C", "freeink-sdk", "status", "--porcelain"), "SDK working tree is dirty")
    if not args.local:
        require(not git("status", "--porcelain"), "release checkout is dirty")
    ini = configparser.ConfigParser(interpolation=None)
    ini.read("platformio.ini", encoding="utf-8")
    require(ini["base"]["platform"] == PLATFORM, "platform/toolchain contract changed")
    require(ini["base"]["board_build.partitions"] == "partitions.csv", "partition source changed")
    value = context(args.repository, args.release_tag, args.commit_sha, args.workflow_sha,
                    link[2], ini["crosspoint"]["version"], args.run_id, args.run_attempt, args.local)
    write_json(args.output, value)
    print(f"firmware_suffix={value['firmware_suffix']}")
    print(f"short_sha={value['commit_sha'][:7]}")


def c_string(data):
    end = data.find(b"\0")
    require(end >= 0 and not any(data[end:]), "noncanonical descriptor string")
    return data[:end].decode("ascii")


def verify_image(data, elf, device, version, limit):
    require(device in PROFILES, "unknown device")
    require(len(data) <= limit and len(data) >= 320, "image size outside OTA limit")
    require(data[0] == 0xE9 and 1 <= data[1] <= 16, "not an ESP app image")
    require(struct.unpack_from("<H", data, 12)[0] == PROFILES[device][1], "wrong chip ID")
    require(data[23] == 1, "appended image SHA-256 is required")
    pos, checksum, segments = 24, 0xEF, []
    for _ in range(data[1]):
        require(pos + 8 <= len(data), "truncated segment header")
        address, length = struct.unpack_from("<II", data, pos)
        pos += 8
        require(length % 4 == 0 and pos + length <= len(data), "invalid segment length")
        payload = data[pos:pos + length]
        segments.append((address, payload))
        for byte in payload:
            checksum ^= byte
        pos += length
    checksum_pos = pos | 15
    require(checksum_pos + 33 == len(data), "trailing data, merged image or truncated footer")
    require(not any(data[pos:checksum_pos]), "invalid image padding")
    require(data[checksum_pos] == checksum, "ESP checksum mismatch")
    require(hashlib.sha256(data[:checksum_pos + 1]).digest() == data[checksum_pos + 1:],
            "ESP validation hash mismatch")
    address, descriptor = segments[0]
    require(0x3C000000 <= address < 0x3E000000 and len(descriptor) >= 256,
            "application descriptor must start the first DROM segment")
    require(struct.unpack_from("<I", descriptor)[0] == 0xABCD5432, "not app-only firmware")
    require(c_string(descriptor[16:48]) == version, "descriptor version mismatch")
    require(c_string(descriptor[48:80]) == "CrossTwins", "descriptor project mismatch")
    require(elf[:4] == b"\x7fELF", "invalid ELF file")
    require(descriptor[144:176] == hashlib.sha256(elf).digest(), "descriptor ELF hash mismatch")
    payload = b"".join(p for _, p in segments)
    require(payload.count(b"CROSSPOINT-BOARD-V1:") == 1, "duplicate or malformed board markers")
    marker = re.findall(rb"CROSSPOINT-BOARD-V1:([^;\x00]{1,23});", payload)
    require(marker == [PROFILES[device][2].encode("ascii")], "missing, duplicate or wrong board marker")
    return {"chip_id": PROFILES[device][1], "board_marker": marker[0].decode("ascii"),
            "project": "CrossTwins", "version": version, "elf_sha256": sha256(elf),
            "size": len(data), "sha256": sha256(data), "ota_limit": limit,
            "app_only": True, "checksum_valid": True, "image_hash_valid": True,
            "elf_hash_valid": True, "verified": True}


def partition_limit(table, csv):
    # ESP-IDF partition entries are 32 bytes; 0xEBEB introduces their MD5.
    slots, pos = {}, 0
    while pos + 32 <= len(table) and table[pos:pos + 2] == b"\xaa\x50":
        _, kind, subtype, offset, size, name, flags = struct.unpack_from("<HBBII16sI", table, pos)
        if kind == 0:
            require(subtype in (0x10, 0x11) and flags == 0, "unexpected app partition")
            require(subtype not in slots, "duplicate OTA partition")
            slots[subtype] = (offset, size, name.split(b"\0")[0].decode("ascii"))
        pos += 32
    require(table[pos:pos + 16] == b"\xeb\xeb" + b"\xff" * 14, "partition MD5 missing")
    require(table[pos + 16:pos + 32] == hashlib.md5(table[:pos]).digest(), "partition MD5 mismatch")
    require(all(b == 0xFF for b in table[pos + 32:]), "invalid partition trailing bytes")
    expected = {}
    for line in csv.decode("utf-8").splitlines():
        fields = [f.strip() for f in line.split("#", 1)[0].split(",")]
        if len(fields) >= 5 and fields[1] == "app":
            require(fields[2] in ("ota_0", "ota_1"), "unexpected CSV app partition")
            subtype = 0x10 if fields[2] == "ota_0" else 0x11
            require(subtype not in expected, "duplicate CSV OTA partition")
            expected[subtype] = (
                int(fields[3], 0), int(fields[4], 0), fields[0])
    require(slots == expected and len(slots) == 2, "compiled OTA slots differ from source CSV")
    require(slots == {0x10: (0x10000, 0x640000, "app0"),
                      0x11: (0x650000, 0x640000, "app1")}, "approved partition layout changed")
    return min(slot[1] for slot in slots.values())


def validate_device(args):
    ctx = json.loads(args.context.read_text(encoding="utf-8"))
    validate_context(ctx)
    require(git("rev-parse", "HEAD") == ctx["commit_sha"], "build checkout HEAD mismatch")
    require(git("-C", "freeink-sdk", "rev-parse", "HEAD") == ctx["sdk_gitlink_sha"],
            "build SDK checkout mismatch")
    if ctx["mode"] == "github-actions":
        require(not git("diff", "HEAD"), "tracked sources changed during release build")
    name = asset_name(resolve_release(ctx["release_tag"], ctx["base_version"], False), args.device)
    require(args.binary.name == name, "asset filename mismatch")
    require(args.environment == PROFILES[args.device][0], "release build environment mismatch")
    table, csv = args.partitions.read_bytes(), args.partition_csv.read_bytes()
    result = verify_image(args.binary.read_bytes(), args.elf.read_bytes(), args.device,
                          ctx["firmware_version"], partition_limit(table, csv))
    result.update({"device": args.device, "asset_name": name, "environment": args.environment,
                   "partitions_sha256": sha256(table), "partition_csv_sha256": sha256(csv)})
    write_json(args.output, {"context": ctx, "artifact": result})


def seal(ctx, directory):
    validate_context(ctx)
    expected_names, results = set(), []
    for device in DEVICES:
        result = json.loads((directory / f"{device}.json").read_text(encoding="utf-8"))
        require(result["context"] == ctx, "mixed run/attempt/commit context")
        item = result["artifact"]
        name = asset_name(resolve_release(ctx["release_tag"], ctx["base_version"], False), device)
        require(item["device"] == device and item["asset_name"] == name, "wrong device/filename")
        require(item["environment"] == PROFILES[device][0] and item["chip_id"] == PROFILES[device][1]
                and item["board_marker"] == PROFILES[device][2], "wrong device contract")
        require(item["version"] == ctx["firmware_version"] and item["project"] == "CrossTwins",
                "wrong descriptor contract")
        require(all(item[k] is True for k in ("verified", "app_only", "checksum_valid",
                                             "image_hash_valid", "elf_hash_valid")), "unverified artifact")
        raw = (directory / name).read_bytes()
        require(item["size"] == len(raw) <= item["ota_limit"] == 0x640000
                and item["sha256"] == sha256(raw), "raw binary changed after verification")
        results.append(item)
        expected_names.update((name, f"{device}.json"))
    require({p.name for p in directory.iterdir()} == expected_names, "missing or extra bundle files")
    require(len({r["partition_csv_sha256"] for r in results}) == 1, "mixed partition sources")
    return {"schema_version": 1, "context": ctx, "artifacts": results,
            "verification": "passed", "release_ready": ctx["mode"] == "github-actions"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare")
    for name in ("repository", "release-tag", "commit-sha", "workflow-sha", "run-id", "run-attempt"):
        p.add_argument("--" + name, required=True)
    p.add_argument("--prerelease", choices=("false", "true"), required=True)
    p.add_argument("--local", action="store_true", help="mark local evidence as not release-ready")
    p.add_argument("--output", type=Path, required=True)
    p = commands.add_parser("validate")
    p.add_argument("--device", choices=DEVICES, required=True)
    for name in ("context", "binary", "elf", "partitions", "partition-csv", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--environment", required=True)
    p = commands.add_parser("seal")
    for name in ("context", "directory", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            prepare(args)
        elif args.command == "validate":
            validate_device(args)
        else:
            ctx = json.loads(args.context.read_text(encoding="utf-8"))
            require(git("rev-parse", "HEAD") == ctx["commit_sha"], "seal checkout HEAD mismatch")
            write_json(args.output, seal(ctx, args.directory))
    except (ValueError, KeyError, OSError, UnicodeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"release preflight failed: {error}\n")


if __name__ == "__main__":
    main()
