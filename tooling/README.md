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

## Secret guards — verified

Checked on 2026-10-25 with a fake AWS-style key (`AKIA` + 16 letters) in `leak.txt`:

| Layer | Result |
| --- | --- |
| Claude Code guard hook | reading `leak.pem` blocked: `Blocked by linko guard: access to secret file …`, exit 2 |
| pre-commit gitleaks | commit aborted: `Detect hardcoded secrets … Failed`, rule `aws-access-token` |
| commit-msg (Conventional Commits) | `wip stuff` rejected |
| CI gitleaks (hooks bypassed, pushed, PR opened) | `gitleaks` check failed with `aws-access-token`; PR closed, branch deleted |

Not available: GitHub secret scanning and push protection, and server-side branch protection (rulesets and classic protection both answer HTTP 403 "Upgrade to GitHub Pro or make this repository public"). Branch rules are therefore enforced by the guard hook and by merging only with `gh pr merge` after green checks. A fake secret pushed to a pull request stays reachable through the pull request's refs on GitHub; only fake keys are used for such tests.

## Usage

```sh
tooling/sync-shared.sh ../linko-apple
pre-commit install            # installs the pre-commit and commit-msg hooks
tooling/github/bootstrap-repo.sh linko-core "Linko core: protocol, call state machine, design tokens"
```
