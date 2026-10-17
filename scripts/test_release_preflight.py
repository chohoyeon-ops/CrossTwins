import copy
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from release_preflight import (
    DEVICES, PLATFORM, PROFILES, REPOSITORY, SDK_SHA, context, partition_limit,
    seal, sha256, validate_context, verify_image, write_json,
)

SHA = "111a10aa295922637754f01a053f39421047e200"
VERSION = "1.6.5-ct.3+111a10a"
ELF = b"\x7fELF" + b"test fixture"
CSV = b"app0,app,ota_0,0x10000,0x640000,\napp1,app,ota_1,0x650000,0x640000,\n"


def fixture(device="x3-x4", version=VERSION, project="CrossTwins", marker=None):
    descriptor = bytearray(256)
    struct.pack_into("<I", descriptor, 0, 0xABCD5432)
    descriptor[16:16 + len(version)] = version.encode()
    descriptor[48:48 + len(project)] = project.encode()
    descriptor[144:176] = hashlib.sha256(ELF).digest()
    board = PROFILES[device][2] if marker is None else marker
    payload = bytes(descriptor) + b"CROSSPOINT-BOARD-V1:" + board.encode() + b";\0"
    payload += b"\0" * (-len(payload) % 4)
    header = bytearray(24)
    header[0:2] = b"\xe9\x01"
    struct.pack_into("<H", header, 12, PROFILES[device][1])
    header[23] = 1
    data = bytes(header) + struct.pack("<II", 0x3C000020, len(payload)) + payload
    checksum = 0xEF
    for byte in payload:
        checksum ^= byte
    data += b"\0" * ((len(data) | 15) - len(data)) + bytes([checksum])
    return data + hashlib.sha256(data).digest()


def table():
    entries = b"".join(struct.pack("<HBBII16sI", 0x50AA, 0, sub, offset, 0x640000, name, 0)
                       for sub, offset, name in ((0x10, 0x10000, b"app0"), (0x11, 0x650000, b"app1")))
    return entries + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest() + b"\xff" * 32


def ctx(local=False):
    return context(REPOSITORY, "v1.6.5-ct.3", SHA, SHA, SDK_SHA, "1.6.5", "1234", "2", local)


class PreflightContracts(unittest.TestCase):
    def test_exact_input_and_future_ct(self):
        validate_context(ctx())
        self.assertEqual(ctx()["firmware_version"], VERSION)
        self.assertEqual(context(REPOSITORY, "v1.6.5-ct.4", SHA, SHA, SDK_SHA,
                                 "1.6.5", "1234", "2")["firmware_suffix"], "-ct.4")
        for counter in (5, 6):
            current = context(REPOSITORY, f"v1.6.5-ct.{counter}", SHA, SHA, SDK_SHA,
                              "1.6.5", "1234", "2")
            validate_context(current)
            self.assertEqual(current["firmware_suffix"], f"-ct.{counter}")
            self.assertEqual(current["firmware_version"], f"1.6.5-ct.{counter}+{SHA[:7]}")

    def test_sdk_pin_matches_gitlink_and_rejects_previous_revision(self):
        root = Path(__file__).resolve().parents[1]
        link = subprocess.check_output(["git", "ls-tree", "HEAD", "freeink-sdk"],
                                       cwd=root, text=True).split()
        self.assertEqual(link[:2], ["160000", "commit"])
        self.assertEqual(SDK_SHA, link[2])
        with self.assertRaisesRegex(ValueError, "SDK gitlink differs from approved revision"):
            context(REPOSITORY, "v1.6.5-ct.5", SHA, SHA,
                    "539da34db67a546cad961525d7e9b6364dfa2fe4", "1.6.5", "1", "1")

    def test_invalid_and_historical_inputs(self):
        for tag in ("v1.6.5-ct.1", "v1.6.5-ct.2", "1.6.5-ct.3", "v1.6.5-ct.03",
                    "v1.6.5", "v1.6.5-ct.3;echo bad", "v1.6.4-ct.3"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                context(REPOSITORY, tag, SHA, SHA, SDK_SHA, "1.6.5", "1", "1")
        values = ["other/repo", "v1.6.5-ct.3", SHA, SHA, SDK_SHA, "1.6.5", "1", "1"]
        for position, value in ((0, "other/repo"), (2, SHA[:7]), (2, SHA.upper()),
                                (3, "a" * 40), (4, "a" * 40), (6, "0"), (7, "-1")):
            args = values.copy()
            args[0] = REPOSITORY
            args[position] = value
            with self.subTest(position=position, value=value), self.assertRaises(ValueError):
                context(*args)

    def test_context_tampering_is_rejected(self):
        for field, value in (("prerelease", True), ("prerelease", 0), ("firmware_version", "1.6.5"),
                             ("platform", "unapproved"), ("mode", "fake"), ("extra", 1)):
            c = ctx()
            c[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_context(c)

    def test_descriptor_version_length_is_bounded(self):
        with self.assertRaises(ValueError):
            context(REPOSITORY, "v1.6.5-ct." + "9" * 30, SHA, SHA, SDK_SHA, "1.6.5", "1", "1")

    def test_prepare_cli_local_and_rejected_prerelease(self):
        root = Path(__file__).resolve().parents[1]
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "context.json"
            command = [sys.executable, str(root / "scripts/release_preflight.py"), "prepare",
                       "--repository", REPOSITORY, "--release-tag", "v1.6.5-ct.3",
                       "--commit-sha", head, "--workflow-sha", head, "--run-id", "1",
                       "--run-attempt", "1", "--output", str(output), "--local"]
            failed = subprocess.run(command + ["--prerelease", "true"], cwd=root, capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertFalse(output.exists())
            passed = subprocess.run(command + ["--prerelease", "false"], cwd=root, capture_output=True, text=True)
            self.assertEqual(passed.returncode, 0, passed.stderr)
            self.assertEqual(json.loads(output.read_text())["mode"], "local")
            self.assertIn("firmware_suffix=-ct.3", passed.stdout)

    def test_workflow_permissions_provenance_and_bundle_gate(self):
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/release.yml").read_text()
        self.assertEqual(workflow.count("ref: ${{ inputs.commit_sha }}"), 4)
        self.assertIn("needs: [prepare, checks, build-release]", workflow)
        self.assertIn("--workflow-sha \"$GITHUB_SHA\"", workflow)
        self.assertLess(workflow.index('test "$COMMIT_SHA" = "$GITHUB_SHA"'),
                        workflow.index("actions/checkout@v6"))
        self.assertIn("--run-attempt \"$GITHUB_RUN_ATTEMPT\"", workflow)
        self.assertIn("pattern: verified-*-${{ github.run_id }}-${{ github.run_attempt }}", workflow)
        self.assertNotIn("contents: write", workflow)
        self.assertNotIn("GH_TOKEN", workflow)
        self.assertNotIn("types: [published]", workflow)
        self.assertNotIn("overwrite:", workflow)
        for environment, _, _ in PROFILES.values():
            self.assertIn("env: " + environment, workflow)

    def test_all_device_image_contracts(self):
        for device in DEVICES:
            with self.subTest(device=device):
                self.assertTrue(verify_image(fixture(device), ELF, device, VERSION, 0x640000)["verified"])

    def test_image_checksum_hash_footer_and_bounds_fail_closed(self):
        image = fixture()
        invalid = [image[:-1], image + b"\0", b"\xff" * 0x10000 + image,
                   image[:300] + bytes([image[300] ^ 1]) + image[301:],
                   image[:-1] + bytes([image[-1] ^ 1])]
        for raw in invalid:
            with self.subTest(length=len(raw)), self.assertRaises(ValueError):
                verify_image(raw, ELF, "x3-x4", VERSION, 0x640000)
        with self.assertRaises(ValueError):
            verify_image(image, ELF, "x3-x4", VERSION, len(image) - 1)

    def test_valid_integrity_still_rejects_wrong_identity(self):
        for raw in (fixture(version="1.6.5-ct.2+ota-test"), fixture(project="crosspoint-reader"),
                    fixture(marker="x4c"), fixture("x4c"), fixture(marker="")):
            with self.subTest(raw=raw[:24]), self.assertRaises(ValueError):
                verify_image(raw, ELF, "x3-x4", VERSION, 0x640000)
        with self.assertRaises(ValueError):
            verify_image(fixture(), ELF + b"changed", "x3-x4", VERSION, 0x640000)

    def test_partition_source_integrity_and_layout(self):
        self.assertEqual(partition_limit(table(), CSV), 0x640000)
        for binary, csv in ((table()[:-33], CSV), (table()[:5] + b"!" + table()[6:], CSV),
                            (table(), CSV.replace(b"0x640000", b"0x630000")),
                            (table() + b"x", CSV)):
            with self.subTest(binary=binary[:8]), self.assertRaises(ValueError):
                partition_limit(binary, csv)

    def test_no_json_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.json"
            write_json(path, ctx())
            with self.assertRaises(ValueError):
                write_json(path, {})


class BundleContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for device in DEVICES:
            name = f"crosspoint-1.6.5-ct.3-{device}.bin"
            raw = fixture(device)
            (self.root / name).write_bytes(raw)
            result = verify_image(raw, ELF, device, VERSION, 0x640000)
            result.update(device=device, asset_name=name, environment=PROFILES[device][0],
                          partitions_sha256=sha256(table()), partition_csv_sha256=sha256(CSV))
            write_json(self.root / f"{device}.json", {"context": ctx(), "artifact": result})

    def tearDown(self):
        self.temp.cleanup()

    def test_seal_exact_same_run_all_five(self):
        result = seal(ctx(), self.root)
        self.assertTrue(result["release_ready"])
        self.assertEqual(len(result["artifacts"]), 5)

    def test_missing_extra_and_raw_tampering(self):
        target = self.root / "crosspoint-1.6.5-ct.3-x3-x4.bin"
        raw = target.read_bytes()
        target.write_bytes(raw + b"tampered")
        with self.assertRaises(ValueError):
            seal(ctx(), self.root)
        target.write_bytes(raw)
        (self.root / "extra.bin").write_bytes(raw)
        with self.assertRaises(ValueError):
            seal(ctx(), self.root)
        (self.root / "extra.bin").unlink()
        target.unlink()
        with self.assertRaises(OSError):
            seal(ctx(), self.root)

    def test_mixed_attempt_context_and_failed_result(self):
        path = self.root / "x3-x4.json"
        original = json.loads(path.read_text())
        for field, value in (("run_attempt", "1"), ("commit_sha", "a" * 40)):
            altered = copy.deepcopy(original)
            altered["context"][field] = value
            path.write_text(json.dumps(altered))
            with self.assertRaises(ValueError):
                seal(ctx(), self.root)
        altered = copy.deepcopy(original)
        altered["artifact"]["elf_hash_valid"] = False
        path.write_text(json.dumps(altered))
        with self.assertRaises(ValueError):
            seal(ctx(), self.root)

    def test_local_bundle_is_never_release_ready(self):
        for device in DEVICES:
            path = self.root / f"{device}.json"
            result = json.loads(path.read_text())
            result["context"] = ctx(local=True)
            path.write_text(json.dumps(result))
        self.assertFalse(seal(ctx(local=True), self.root)["release_ready"])

    def test_seal_cli_writes_local_manifest_without_overwrite(self):
        repo = Path(__file__).resolve().parents[1]
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
        local = context(REPOSITORY, "v1.6.5-ct.3", head, head, SDK_SHA, "1.6.5", "1", "1", True)
        version = local["firmware_version"]
        for device in DEVICES:
            path = self.root / f"{device}.json"
            result = json.loads(path.read_text())
            raw = fixture(device, version=version)
            (self.root / result["artifact"]["asset_name"]).write_bytes(raw)
            result["context"] = local
            result["artifact"].update(verify_image(raw, ELF, device, version, 0x640000))
            path.write_text(json.dumps(result))
        source = self.root.parent / "context.json"
        source.write_text(json.dumps(local))
        output = self.root / "manifest.json"
        command = [sys.executable, str(repo / "scripts/release_preflight.py"), "seal",
                   "--context", str(source), "--directory", str(self.root), "--output", str(output)]
        result = subprocess.run(command, cwd=repo, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = output.read_bytes()
        self.assertFalse(json.loads(manifest)["release_ready"])
        self.assertEqual(len(json.loads(manifest)["artifacts"]), 5)
        repeated = subprocess.run(command, cwd=repo, capture_output=True, text=True)
        self.assertNotEqual(repeated.returncode, 0)
        self.assertEqual(output.read_bytes(), manifest)


if __name__ == "__main__":
    unittest.main()
