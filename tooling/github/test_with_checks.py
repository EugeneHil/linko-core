import json
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "with_checks.py"
RULESETS = [HERE / "ruleset-develop.json", HERE / "ruleset-main.json"]


def run(path, *checks):
    out = subprocess.run(
        [sys.executable, "-I", "-B", str(SCRIPT), str(path), *checks],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return json.loads(out)


def contexts(ruleset):
    for rule in ruleset["rules"]:
        if rule["type"] == "required_status_checks":
            return [c["context"] for c in rule["parameters"]["required_status_checks"]]
    raise AssertionError("no required_status_checks rule")


class WithChecksTest(unittest.TestCase):
    def test_appends_new_checks(self):
        for path in RULESETS:
            self.assertEqual(contexts(run(path, "core-macos", "core-windows")), ["gitleaks", "core-macos", "core-windows"])

    def test_never_duplicates(self):
        for path in RULESETS:
            once = run(path, "core-macos", "core-windows")
            twice = run(path, "core-macos", "core-windows", "core-macos", "gitleaks")
            self.assertEqual(once, twice)

    def test_keeps_order(self):
        for path in RULESETS:
            self.assertEqual(contexts(run(path, "b", "a")), ["gitleaks", "b", "a"])

    def test_leaves_other_rules_untouched(self):
        for path in RULESETS:
            original = json.loads(path.read_text())
            changed = run(path, "core-macos")
            keep = lambda rs: [r for r in rs["rules"] if r["type"] != "required_status_checks"]
            self.assertEqual(keep(changed), keep(original))
            self.assertEqual(run(path), original)

    def test_rulesets_are_named_and_target_their_branch(self):
        names = {json.loads(p.read_text())["name"]: json.loads(p.read_text()) for p in RULESETS}
        self.assertEqual(set(names), {"protected-develop", "protected-main"})
        self.assertEqual(names["protected-develop"]["conditions"]["ref_name"]["include"], ["refs/heads/develop"])
        self.assertEqual(names["protected-main"]["conditions"]["ref_name"]["include"], ["refs/heads/main"])
        types = lambda rs: {r["type"] for r in rs["rules"]}
        self.assertIn("required_linear_history", types(names["protected-develop"]))
        self.assertNotIn("required_linear_history", types(names["protected-main"]))


if __name__ == "__main__":
    unittest.main()
