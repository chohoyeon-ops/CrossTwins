"""Resolve a published release tag to asset and embedded firmware versions."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass


DEVICES = ("x3-x4", "sticky", "x4pro", "x4c", "papermono")
BASE_VERSION_PATTERN = re.compile(r"\d+\.\d+\.\d+")


@dataclass(frozen=True)
class ReleaseVersion:
    asset_version: str
    firmware_suffix: str


def resolve_release(tag: str, base_version: str, prerelease: bool) -> ReleaseVersion:
    """Validate a tag and return its asset version and embedded-version suffix."""
    if not BASE_VERSION_PATTERN.fullmatch(base_version):
        raise ValueError(f"invalid configured base version: {base_version!r}")

    version = tag[1:] if tag.startswith("v") else tag
    if version == base_version:
        if prerelease:
            raise ValueError("stable version tag cannot be marked as a prerelease")
        return ReleaseVersion(version, "")

    if version == f"{base_version}rc":
        if not prerelease:
            raise ValueError("upstream rc tag must be marked as a prerelease")
        return ReleaseVersion(version, "-rc")

    match = re.fullmatch(re.escape(base_version) + r"-ct\.([1-9][0-9]*)", version)
    if match:
        # GitHub publication status is independent of the ct identifier in SemVer.
        return ReleaseVersion(version, f"-ct.{match.group(1)}")

    raise ValueError(f"tag {tag!r} does not match base version {base_version!r}")


def asset_name(release: ReleaseVersion, device: str) -> str:
    if device not in DEVICES:
        raise ValueError(f"unsupported release device: {device!r}")
    return f"crosspoint-{release.asset_version}-{device}.bin"


def firmware_version(release: ReleaseVersion, base_version: str, short_sha: str) -> str:
    if not release.firmware_suffix:
        return base_version
    if not re.fullmatch(r"[0-9a-f]{7}", short_sha):
        raise ValueError("suffixed firmware requires a seven-character lowercase commit SHA")
    return f"{base_version}{release.firmware_suffix}+{short_sha}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--base-version", required=True)
    parser.add_argument("--prerelease", choices=("true", "false"), required=True)
    parser.add_argument("--device", choices=DEVICES)
    args = parser.parse_args()

    try:
        release = resolve_release(args.tag, args.base_version, args.prerelease == "true")
        outputs = [
            f"asset_version={release.asset_version}",
            f"firmware_suffix={release.firmware_suffix}",
        ]
        if args.device:
            outputs.append(f"asset_name={asset_name(release, args.device)}")
        print("\n".join(outputs))
    except ValueError as error:
        print(f"release version error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
