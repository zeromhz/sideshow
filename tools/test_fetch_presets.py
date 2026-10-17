"""Run the real installer with local fixture packs; only Git/network is stubbed."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class FetchPresetsTests(unittest.TestCase):
    def test_reinstall_preserves_curated_directories_and_links(self):
        script = Path(__file__).resolve().parents[1] / "setup/fetch_presets.sh"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            packs = root / "packs"
            preset_pack = packs / "projectM-presets-rpi5"
            texture_pack = packs / "milkdrop-texture-pack"
            for pack in (preset_pack, texture_pack):
                (pack / ".git").mkdir(parents=True)
                (pack / "textures").mkdir()
            (preset_pack / "presets/creamofthecrop/Custom").mkdir(parents=True)
            (preset_pack / "presets/creamofthecrop/Fresh").mkdir()
            for folder in ("isoscelesmashup", "BLTC", "EnD"):
                (preset_pack / "presets" / folder).mkdir()
            bin_dir = root / "bin"
            bin_dir.mkdir()
            git = bin_dir / "git"
            git.write_text('#!/bin/sh\ncase "$2" in\n*projectM-presets-rpi5) echo 60eaf82081cd247c466829a47713eeb19dc0d1fa;;\n*) echo 6368812f27bc747b517218fbf89d21d59afce4d9;;\nesac\n')
            git.chmod(0o755)
            for playlist, child in (("default", "presets"), ("custom", "Custom"),
                                    ("isosceles", "isoscelesmashup"), ("bltc", "BLTC")):
                directory = root / "presets" / playlist
                directory.mkdir(parents=True)
                (directory / child).write_text("curated data")
            custom_link = root / "presets/en-d"
            custom_link.symlink_to("../my-custom-pack")
            managed_link = root / "presets/fresh"
            managed_link.symlink_to("../packs/old-target")
            env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"])
            for _ in range(2):
                subprocess.run(["bash", str(script), str(root)], env=env,
                               check=True, capture_output=True)
                for playlist, child in (("default", "presets"), ("custom", "Custom"),
                                        ("isosceles", "isoscelesmashup"), ("bltc", "BLTC")):
                    file = root / "presets" / playlist / child
                    self.assertFalse(file.is_symlink())
                    self.assertEqual(file.read_text(), "curated data")
                self.assertEqual(os.readlink(custom_link), "../my-custom-pack")
                self.assertEqual(os.readlink(managed_link),
                                 "../packs/projectM-presets-rpi5/presets/creamofthecrop/Fresh")
                self.assertTrue(managed_link.is_dir())
