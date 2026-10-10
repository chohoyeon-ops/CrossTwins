import unittest
from pathlib import Path
import subprocess
import sys

from release_version import asset_name, firmware_version, resolve_release


BASE = "1.6.5"
SHA = "9986adb"


class ReleaseVersionTests(unittest.TestCase):
    def test_stable_upstream_tags_remain_valid(self):
        for tag in ("1.6.5", "v1.6.5"):
            with self.subTest(tag=tag):
                release = resolve_release(tag, BASE, prerelease=False)
                self.assertEqual(release.asset_version, BASE)
                self.assertEqual(release.firmware_suffix, "")
                self.assertEqual(firmware_version(release, BASE, SHA), BASE)

    def test_upstream_rc_tags_remain_valid(self):
        for tag in ("1.6.5rc", "v1.6.5rc"):
            with self.subTest(tag=tag):
                release = resolve_release(tag, BASE, prerelease=True)
                self.assertEqual(release.asset_version, "1.6.5rc")
                self.assertEqual(release.firmware_suffix, "-rc")
                self.assertEqual(firmware_version(release, BASE, SHA), "1.6.5-rc+9986adb")

    def test_cross_twins_tags_map_to_internal_versions_and_assets(self):
        for counter in (1, 2, 3):
            tag = f"v1.6.5-ct.{counter}"
            with self.subTest(tag=tag):
                release = resolve_release(tag, BASE, prerelease=True)
                self.assertEqual(release.asset_version, f"1.6.5-ct.{counter}")
                self.assertEqual(release.firmware_suffix, f"-ct.{counter}")
                self.assertEqual(
                    firmware_version(release, BASE, SHA), f"1.6.5-ct.{counter}+9986adb"
                )

    def test_all_release_assets_share_the_normalized_tag(self):
        release = resolve_release("v1.6.5-ct.1", BASE, prerelease=True)
        self.assertEqual(
            [asset_name(release, device) for device in ("x3-x4", "sticky", "x4pro", "x4c", "papermono")],
            [
                "crosspoint-1.6.5-ct.1-x3-x4.bin",
                "crosspoint-1.6.5-ct.1-sticky.bin",
                "crosspoint-1.6.5-ct.1-x4pro.bin",
                "crosspoint-1.6.5-ct.1-x4c.bin",
                "crosspoint-1.6.5-ct.1-papermono.bin",
            ],
        )

    def test_rejects_wrong_versions_suffixes_and_release_types(self):
        invalid = (
            ("v1.6.4-ct.1", True),
            ("v1.6.5-ct.0", True),
            ("v1.6.5-ct.01", True),
            ("v1.6.5-ct.one", True),
            ("v1.6.5-rc.1", True),
            ("V1.6.5-ct.1", True),
            ("vv1.6.5-ct.1", True),
            ("v1.6.5rc", False),
            ("v1.6.5", True),
        )
        for tag, prerelease in invalid:
            with self.subTest(tag=tag, prerelease=prerelease), self.assertRaises(ValueError):
                resolve_release(tag, BASE, prerelease)

    def test_rejects_unknown_device_and_invalid_sha(self):
        release = resolve_release("v1.6.5-ct.1", BASE, prerelease=True)
        with self.assertRaises(ValueError):
            asset_name(release, "unknown")
        with self.assertRaises(ValueError):
            firmware_version(release, BASE, "not-a-sha")

    def test_cross_twins_stable_publication_keeps_suffix_and_asset(self):
        for prerelease in (False, True):
            with self.subTest(prerelease=prerelease):
                release = resolve_release("v1.6.5-ct.3", BASE, prerelease)
                self.assertEqual(firmware_version(release, BASE, SHA), "1.6.5-ct.3+9986adb")
                self.assertEqual(asset_name(release, "x3-x4"), "crosspoint-1.6.5-ct.3-x3-x4.bin")
                result = subprocess.run(
                    [sys.executable, str(Path(__file__).with_name("release_version.py")),
                     "--tag", "v1.6.5-ct.3", "--base-version", BASE,
                     "--prerelease", str(prerelease).lower(), "--device", "x3-x4"],
                    check=True, capture_output=True, text=True,
                )
                self.assertIn("firmware_suffix=-ct.3", result.stdout.splitlines())
                self.assertIn("asset_name=crosspoint-1.6.5-ct.3-x3-x4.bin", result.stdout.splitlines())

    def test_release_workflow_selects_environment_from_tag_suffix(self):
        workflow = (Path(__file__).resolve().parents[1] / ".github/workflows/release.yml").read_text()
        expression = "steps.validate.outputs.firmware_suffix != '' && matrix.rc_env || matrix.release_env"
        self.assertEqual(workflow.count(expression), 3)
        self.assertNotIn("github.event.release.prerelease && matrix.rc_env", workflow)
        self.assertNotIn("--clobber", workflow)


if __name__ == "__main__":
    unittest.main()
