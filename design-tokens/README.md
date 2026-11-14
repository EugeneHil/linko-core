# Design tokens

Single source of truth for the Linko design system (spec §4.8): `tokens.json`.
`generate.py` renders the committed Swift artifact `Tokens.swift` that the apps
consume (`enum Tokens` with `Colors.Light` / `Colors.Dark`, `Spacing`, `Radii`,
`ControlSizes`, `Typography`). Never edit `Tokens.swift` by hand — change
`tokens.json` and regenerate.

## Usage

```sh
python3 design-tokens/generate.py            # write design-tokens/Tokens.swift
python3 design-tokens/generate.py --check    # exit 1 + diff when stale
python3 design-tokens/generate.py --swift-out PATH
python3 -I -B -m unittest discover -s design-tokens -v   # tests
```

`scripts/ci-local.sh` runs `generate.py --check` so a stale `Tokens.swift`
fails local CI. Python 3, stdlib only, compatible with stock macOS Python 3.9.
Output is deterministic byte-for-byte: every key iteration is sorted.

## tokens.json

Top-level sections (anything else is rejected): `colors`, `typography`,
`spacing`, `radii`, `controlSizes`.

- **colors** — semantic names, each `{"light": "#…", "dark": "#…"}`:
  `accent`, `background`, `secondaryBackground`, `label`, `secondaryLabel`,
  `separator`, `incomingBubble`, `outgoingBubble`. Values follow Apple's
  system palette: iOS blue accent `#007AFF` (light) / `#0A84FF` (dark),
  system greys (`secondaryBackground` = systemGray6, `incomingBubble` =
  systemGray5). Alpha colors keep Apple's alpha as an 8-digit hex
  (`#RRGGBBAA`, e.g. `secondaryLabel` = label at 60%); the generator emits
  `Color(red:green:blue:opacity:)`. `outgoingBubble` currently equals the
  accent blue on purpose (iMessage-style), the tokens can diverge later.
- **typography** — `fontSize` (Apple text styles: caption 12 … largeTitle 34),
  `fontWeight` (CSS scale 400/500/600/700, mapped to `Font.Weight` cases),
  `lineHeight` (multipliers: tight 1.12, normal 1.21, relaxed 1.33).
- **spacing** — 4-pt grid `4, 8, 12, 16, 24, 32` with numeric names
  `s4`…`s32` (decision: grid values are self-documenting and avoid inventing
  per-step semantics).
- **radii** — `small` 8, `medium` 12, `large` 16, `full` 9999 (sentinel for
  capsule shapes; prefer `Capsule()` when it fits).
- **controlSizes** — call tray rules from spec §4.7: `button` ≥ 24 pt,
  `gap` ≥ 8 pt, `screenEdge` ≥ 12 pt. Current values: 24 / 8 / 12.

Adding a token: edit `tokens.json`, run `generate.py`, commit both the JSON
and the regenerated `Tokens.swift`. Keys must be valid Swift identifier parts
(camelCase) since they become `static let` member names.
