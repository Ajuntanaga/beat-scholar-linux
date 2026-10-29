import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).parents[1] / "tools/migrate_factory_content.py"
SPEC = importlib.util.spec_from_file_location("migrate_factory_content", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class MigrationTests(unittest.TestCase):
    def test_copies_only_content_groups_and_writes_hash_manifest(self):
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            source = root_path / "source"
            destination = root_path / "destination"
            (source / "Presets/Rock").mkdir(parents=True)
            (source / "Preferences").mkdir()
            (source / "Presets/Rock/Test.json").write_text('{"name":"Test"}', encoding="utf-8")
            (source / "Preferences/License.json").write_text("excluded", encoding="utf-8")

            manifest = MODULE.migrate(source, destination)
            self.assertEqual(len(manifest["files"]), 1)
            self.assertTrue((destination / "Presets/Rock/Test.json").is_file())
            self.assertFalse((destination / "Preferences/License.json").exists())
            stored = json.loads((destination / "factory-manifest.json").read_text())
            self.assertEqual(stored["files"][0]["path"], "Presets/Rock/Test.json")
            self.assertEqual(len(stored["files"][0]["sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
