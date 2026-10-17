import importlib.util
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ble_controller', ROOT / 'scripts/patch_arduino_ble_controller.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class OffEnvironment:
    def BoardConfig(self): return {'build.mcu': 'esp32c3'}
    def get(self, key): return None
    def GetProjectOption(self, key, default):
        return 'CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y\nCONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=n'
    def PioPlatform(self): raise AssertionError('OFF build must not patch controller libraries')

class ControllerLink(unittest.TestCase):
    def test_off_override_skips_flash_archive(self):
        module.configure_controller(OffEnvironment())
    def test_preserves_other_linker_scripts(self):
        flags = ['-T', 'esp32c3.rom.ld', '-T', 'esp32c3.rom.bt_funcs.ld', '-Wl,--gc-sections', '-T']
        self.assertEqual(module.filter_flash_only_bt_linkflags(flags), ['-T', 'esp32c3.rom.ld', '-Wl,--gc-sections', '-T'])

if __name__ == '__main__': unittest.main()
