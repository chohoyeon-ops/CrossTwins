import contextlib
import io
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[1]

class Env(dict):
    def Append(self, **values):
        self.update(values)

class DevelopmentVersion(unittest.TestCase):
    def inject(self, branch, profile='default'):
        initial = Env(PIOENV='gh_release', PROJECT_DIR=str(ROOT))
        ns = runpy.run_path(str(ROOT / 'scripts/git_branch.py'), init_globals={'Import': lambda name: None, 'env': initial})
        fn = ns['inject_version']
        fn.__globals__.update(get_base_version=lambda _: '1.6.5', get_git_branch=lambda _: branch, get_git_short_sha=lambda _: 'abcdef12')
        env = Env(PIOENV=profile, PROJECT_DIR=str(ROOT))
        with contextlib.redirect_stdout(io.StringIO()):
            fn(env)
        return dict(env.get('CPPDEFINES', []))

    def test_long_experiment_keeps_sha_and_fits_descriptor(self):
        version = self.inject('experiment/ble-page-turner')['CROSSPOINT_VERSION'].replace('\\"', '')
        self.assertLessEqual(len(version.encode('utf-8')), 31)
        self.assertTrue(version.endswith('-abcdef12'))

    def test_unicode_branch_fits_and_off_profile_has_same_version(self):
        branch = '실험/' * 15
        version = self.inject(branch)['CROSSPOINT_VERSION'].replace('\\"', '')
        self.assertLessEqual(len(version.encode('utf-8')), 31)
        self.assertEqual(self.inject(branch), self.inject(branch, 'default_ble_off'))

    def test_release_is_untouched(self):
        self.assertEqual(self.inject('experiment/ble-page-turner', 'gh_release'), {})

if __name__ == '__main__':
    unittest.main()
