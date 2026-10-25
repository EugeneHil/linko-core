# Shared repository tooling

Authored here, copied into `linko-apple` and `linko-server` by `sync-shared.sh`.

| Path | Purpose |
| --- | --- |
| `claude/guard.py` | Claude Code `PreToolUse` hook: blocks access to secrets and signing material, commits/pushes on `main`/`develop`, force pushes and skipped hooks; protects itself. Fails closed. Tests: `python3 -I -B -m unittest discover -s tooling/claude` |
| `repo/gitignore.base` | Shared `.gitignore` block (appended once) |
| `repo/.pre-commit-config.yaml` | gitleaks, Conventional Commits (`commit-msg`), basic hygiene hooks |
| `repo/.gitleaks.toml` | gitleaks config: default rules, no path allowlist |
| `repo/pull_request_template.md` | PR template |
| `repo/CLAUDE.base.md` | Shared rules for `CLAUDE.md` |
| `github/workflows/gitleaks.yml` | CI secret scan of the full history (`gitleaks` check), checksum-verified binary |
| `github/ruleset-*.json`, `github/with_checks.py` | Branch rulesets for `develop` and `main`, with extra required checks appended per repo |
| `github/bootstrap-repo.sh` | Creates the private GitHub repo, pushes `develop`/`main`, sets merge settings, applies rulesets when the plan allows and reports what was not applied |
| `github/dependabot.yml` | Weekly GitHub Actions updates |
| `sync-shared.sh` | Copies all of the above into another repo (idempotent) |

Tests for the GitHub tooling: `python3 -I -B -m unittest discover -s tooling/github`.

## Usage

```sh
tooling/sync-shared.sh ../linko-apple
pre-commit install            # installs the pre-commit and commit-msg hooks
tooling/github/bootstrap-repo.sh linko-core "Linko core: protocol, call state machine, design tokens"
```
