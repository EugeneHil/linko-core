"""Tests for the Linko design-token generator.

Contract (LNK-12): tokens.json sections, tray control sizes and generator
behavior. Stdlib only; run with:

    python3 -I -B -m unittest discover -s design-tokens -v
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import generate  # noqa: E402  (path set up above)

TOKENS_JSON = HERE / "tokens.json"
GENERATE_PY = HERE / "generate.py"
TOKENS_SWIFT = HERE / "Tokens.swift"

SECTION_NAMES = ("colors", "typography", "spacing", "radii", "controlSizes")
COLOR_NAMES = (
    "accent",
    "background",
    "secondaryBackground",
    "label",
    "secondaryLabel",
    "separator",
    "incomingBubble",
    "outgoingBubble",
)
HEX_COLOR_RE = re.compile(r"^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$")
SPACING_GRID = {4, 8, 12, 16, 24, 32}


def load_tokens() -> dict:
    with TOKENS_JSON.open(encoding="utf-8") as stream:
        return json.load(stream)


def run_generate(*args: str) -> subprocess.CompletedProcess:
    """Run generate.py in a clean interpreter, capturing everything."""
    return subprocess.run(
        [sys.executable, "-I", str(GENERATE_PY), *args],
        capture_output=True,
        text=True,
    )


class TokensJsonTest(unittest.TestCase):
    def test_tokens_json_parses(self) -> None:
        data = load_tokens()
        self.assertIsInstance(data, dict)
        for section in SECTION_NAMES:
            self.assertIn(section, data, f"missing section {section!r}")
            self.assertIsInstance(data[section], dict)
            self.assertTrue(data[section], f"section {section!r} is empty")

    def test_colors_all_have_light_and_dark(self) -> None:
        colors = load_tokens()["colors"]
        self.assertEqual(set(colors), set(COLOR_NAMES))
        for name, value in colors.items():
            self.assertEqual(
                set(value), {"light", "dark"}, f"color {name!r} needs light/dark"
            )
            for scheme in ("light", "dark"):
                self.assertRegex(
                    value[scheme], HEX_COLOR_RE, f"{name}.{scheme} is not a hex color"
                )

    def test_spacing_on_four_point_grid(self) -> None:
        spacing = load_tokens()["spacing"]
        values = list(spacing.values())
        self.assertEqual(set(values), SPACING_GRID)
        for value in values:
            self.assertIsInstance(value, int)
            self.assertEqual(value % 4, 0, f"{value} is not on the 4-pt grid")

    def test_tray_rules_present(self) -> None:
        sizes = load_tokens()["controlSizes"]
        self.assertGreaterEqual(sizes["button"], 24)
        self.assertGreaterEqual(sizes["gap"], 8)
        self.assertGreaterEqual(sizes["screenEdge"], 12)


class GenerateTest(unittest.TestCase):
    def _generate_to(self, directory: Path) -> Path:
        out = directory / "Tokens.swift"
        result = run_generate("--swift-out", str(out))
        self.assertEqual(result.returncode, 0, result.stderr)
        return out

    def test_generate_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            first = self._generate_to(out_dir / "one")
            second = self._generate_to(out_dir / "two")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            # The committed artifact must be exactly what the generator emits.
            self.assertEqual(first.read_bytes(), TOKENS_SWIFT.read_bytes())

    def test_check_fails_on_stale_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            stale = Path(tmp) / "Tokens.swift"
            stale.write_text("// stale output\n", encoding="utf-8")
            result = run_generate("--check", "--swift-out", str(stale))
            self.assertEqual(result.returncode, 1)
            self.assertIn("stale", result.stdout)
            self.assertIn("-// stale output", result.stdout)

    def test_check_passes_on_fresh_output(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fresh = self._generate_to(Path(tmp))
            result = run_generate("--check", "--swift-out", str(fresh))
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_unknown_section_rejected(self) -> None:
        with self.assertRaises(generate.TokenError):
            generate.validate_tokens(
                {
                    "colors": {},
                    "typography": {},
                    "spacing": {},
                    "radii": {},
                    "controlSizes": {},
                    "notASection": {},
                }
            )

    def test_swift_output_contains_both_schemes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            text = self._generate_to(Path(tmp)).read_text(encoding="utf-8")
        self.assertIn("enum Tokens", text)
        self.assertIn("enum Colors", text)
        self.assertIn("enum Light", text)
        self.assertIn("enum Dark", text)
        for enum_name in ("Spacing", "Radii", "ControlSizes", "Typography"):
            self.assertIn(f"enum {enum_name}", text)
        for name in COLOR_NAMES:
            self.assertEqual(text.count(f"static let {name} ="), 2)


if __name__ == "__main__":
    unittest.main()
