import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from infinite import setup_cli  # noqa: E402

ENTRY = {"command": "npx", "args": ["-y", "infinite-engine", "serve"]}


class ConfigureFile(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "c.json")

    def write(self, text):
        with open(self.path, "w") as f:
            f.write(text)

    def read(self):
        with open(self.path) as f:
            return json.load(f)

    def test_adds_and_keeps_other_servers_and_backs_up(self):
        self.write('{"mcpServers": {"other": {"command": "x"}}, "theme": 1}')
        self.assertEqual(setup_cli.configure_file(self.path, ENTRY)[0], "added")
        d = self.read()
        self.assertEqual(d["mcpServers"]["other"], {"command": "x"})
        self.assertEqual(d["mcpServers"]["infinite"], ENTRY)
        self.assertEqual(d["theme"], 1)
        self.assertTrue(os.path.exists(self.path + ".infinite-backup"))

    def test_creates_missing_file(self):
        self.assertEqual(setup_cli.configure_file(self.path, ENTRY)[0], "added")
        self.assertEqual(self.read(), {"mcpServers": {"infinite": ENTRY}})

    def test_refuses_jsonc_and_leaves_it_untouched(self):
        text = '{\n // keep me\n "mcpServers": {"other": {}},\n}'
        self.write(text)
        self.assertEqual(setup_cli.configure_file(self.path, ENTRY)[0], "skipped")
        self.assertEqual(open(self.path).read(), text)

    def test_different_entry_needs_force(self):
        self.write('{"mcpServers": {"infinite": {"command": "mine"}}}')
        self.assertEqual(setup_cli.configure_file(self.path, ENTRY)[0], "skipped")
        self.assertEqual(self.read()["mcpServers"]["infinite"], {"command": "mine"})
        self.assertEqual(setup_cli.configure_file(self.path, ENTRY, force=True)[0], "updated")
        self.assertEqual(self.read()["mcpServers"]["infinite"], ENTRY)

    def test_same_entry_unchanged_and_dry_run_writes_nothing(self):
        self.assertEqual(setup_cli.configure_file(self.path, ENTRY, dry_run=True)[0], "would add")
        self.assertFalse(os.path.exists(self.path))
        setup_cli.configure_file(self.path, ENTRY)
        self.assertEqual(setup_cli.configure_file(self.path, ENTRY)[0], "unchanged")


class Setup(unittest.TestCase):
    def test_only_installed_apps(self):
        home = tempfile.mkdtemp()
        os.makedirs(os.path.join(home, ".cursor"))
        res = setup_cli.setup(home=home, platform="linux", cwd=home)
        by = {r[0]: r[2] for r in res}
        self.assertEqual(by["cursor (global)"], "added")
        self.assertEqual(by["claude desktop"], "not found")
        self.assertEqual(by["windsurf"], "not found")
        self.assertFalse(os.path.exists(os.path.join(home, ".config")))


if __name__ == "__main__":
    unittest.main()
