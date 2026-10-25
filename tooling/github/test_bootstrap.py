"""bootstrap-repo.sh against a fake `gh` on PATH and a local bare `origin`."""

import os
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / "bootstrap-repo.sh"

FAKE_GH = textwrap.dedent(
    """\
    #!/usr/bin/env python3
    import os, subprocess, sys
    args = sys.argv[1:]
    with open(os.environ["FAKE_GH_LOG"], "a") as log:
        log.write(" ".join(args) + "\\n")
    if args[:2] == ["api", "user"]:
        print("EugeneHil"); sys.exit(0)
    if args[:2] == ["repo", "view"]:
        sys.exit(0 if os.environ.get("FAKE_GH_REPO_EXISTS") else 1)
    if args[:2] == ["repo", "create"]:
        subprocess.run(["git", "remote", "add", "origin", os.environ["FAKE_ORIGIN"]], check=True)
        sys.exit(0)
    if args[0] == "api" and "rulesets" in " ".join(args):
        if os.environ.get("FAKE_GH_RULESETS") == "forbidden":
            sys.stderr.write("gh: Upgrade to GitHub Pro or make this repository public to enable this feature. (HTTP 403)\\n")
            sys.exit(1)
        if "-X" not in args:
            print("[]")
        sys.exit(0)
    sys.exit(0)
    """
)


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


class BootstrapTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.bin = root / "bin"
        self.bin.mkdir()
        gh = self.bin / "gh"
        gh.write_text(FAKE_GH)
        gh.chmod(0o755)
        self.log = root / "gh.log"
        self.origin = root / "origin.git"
        git(root, "init", "--bare", "-b", "develop", str(self.origin))
        self.repo = root / "x"
        self.repo.mkdir()
        git(self.repo, "init", "-b", "develop")
        git(self.repo, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "--allow-empty", "-m", "chore: init")
        git(self.repo, "branch", "main")

    def tearDown(self):
        self.tmp.cleanup()

    def run_script(self, **env):
        full = dict(os.environ, PATH=f"{self.bin}{os.pathsep}{os.environ['PATH']}",
                    FAKE_GH_LOG=str(self.log), FAKE_ORIGIN=str(self.origin), **env)
        return subprocess.run(["bash", str(SCRIPT), "x", "test repo"], cwd=self.repo, env=full,
                              capture_output=True, text=True)

    def calls(self):
        return self.log.read_text()

    def test_creates_private_repo(self):
        run = self.run_script()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("repo create EugeneHil/x --private", self.calls())
        self.assertNotIn("--public", self.calls())

    def test_ruleset_forbidden_is_reported(self):
        run = self.run_script(FAKE_GH_RULESETS="forbidden")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("rulesets NOT applied", run.stdout)
        self.assertNotIn("rulesets applied", run.stdout)
        self.assertNotIn("-X POST repos/EugeneHil/x/rulesets", self.calls())

    def test_rulesets_applied_when_allowed(self):
        run = self.run_script()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(self.calls().count("-X POST repos/EugeneHil/x/rulesets"), 2)
        self.assertIn("rulesets applied", run.stdout)

    def test_reports_secret_scanning_unavailable(self):
        run = self.run_script()
        self.assertIn("secret scanning: not available for private repos on Free", run.stdout)
        self.assertNotIn("security_and_analysis", self.calls())

    def test_pushes_missing_branches(self):
        run = self.run_script()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(git(self.origin, "rev-parse", "develop"), git(self.repo, "rev-parse", "develop"))
        self.assertEqual(git(self.origin, "rev-parse", "main"), git(self.repo, "rev-parse", "main"))

    def test_skips_push_of_existing_branches(self):
        git(self.repo, "remote", "add", "origin", str(self.origin))
        git(self.repo, "push", "origin", "develop", "main")
        before = git(self.origin, "rev-parse", "develop")
        git(self.repo, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "--allow-empty", "-m", "chore: more")
        run = self.run_script(FAKE_GH_REPO_EXISTS="1")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(git(self.origin, "rev-parse", "develop"), before)

    def test_requires_name_and_description(self):
        run = subprocess.run(["bash", str(SCRIPT), "only-name"], cwd=self.repo, capture_output=True, text=True)
        self.assertEqual(run.returncode, 2)


if __name__ == "__main__":
    unittest.main()
