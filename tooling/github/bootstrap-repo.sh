#!/usr/bin/env bash
# Publish a local Linko repo to GitHub as a PRIVATE repository and apply the
# shared settings. Idempotent: safe to re-run. Run from the repo root after the
# bootstrap commit exists on `develop` and a local `main` branch points at it.
#
# Private repositories on the GitHub Free plan may not support rulesets, and
# never get secret scanning / push protection. The script never claims a
# protection it could not apply: the summary states what was and was not set.
# Optional trailing arguments are extra required status checks appended to the
# default `gitleaks` check in every ruleset.
# Usage: <linko-core>/tooling/github/bootstrap-repo.sh <name> "<description>" [check ...]
set -euo pipefail

if [ $# -lt 2 ]; then
  echo "usage: $0 <name> \"<description>\" [check ...]" >&2
  exit 2
fi

name=$1
description=$2
shift 2
here=$(CDPATH='' cd -- "$(dirname "$0")" && pwd -P)
login=$(gh api user -q .login)
repo="$login/$name"

if gh repo view "$repo" >/dev/null 2>&1; then
  git remote get-url origin >/dev/null 2>&1 || git remote add origin "https://github.com/$repo.git"
else
  gh repo create "$repo" --private --description "$description" --source . --remote origin
fi
# Push only branches the remote lacks: protected branches must never be pushed to directly.
for branch in develop main; do
  git ls-remote --exit-code --heads origin "$branch" >/dev/null 2>&1 || git push -u origin "$branch"
done

gh repo edit "$repo" \
  --default-branch develop \
  --delete-branch-on-merge \
  --enable-squash-merge \
  --enable-merge-commit \
  --enable-rebase-merge=false >/dev/null
echo "default branch: develop; merge methods: squash (develop), merge commits (release PRs)"

if ! existing=$(gh api "repos/$repo/rulesets" 2>/dev/null); then
  echo "rulesets NOT applied (GitHub plan): branch rules are enforced locally by the guard hook"
else
  for file in "$here"/ruleset-*.json; do
    rs_name=$(python3 -I -B -c 'import json,sys; print(json.load(open(sys.argv[1]))["name"])' "$file")
    id=$(printf '%s' "$existing" | python3 -I -B -c '
import json, sys
for rs in json.load(sys.stdin):
    if rs.get("name") == sys.argv[1]:
        print(rs["id"]); break' "$rs_name")
    if [ -n "$id" ]; then
      python3 -I -B "$here/with_checks.py" "$file" "$@" | gh api -X PUT "repos/$repo/rulesets/$id" --input - >/dev/null
    else
      python3 -I -B "$here/with_checks.py" "$file" "$@" | gh api -X POST "repos/$repo/rulesets" --input - >/dev/null
    fi
  done
  echo "rulesets applied: protected-develop, protected-main"
fi

echo "secret scanning: not available for private repos on Free (guard hook, pre-commit gitleaks and CI gitleaks remain)"
echo "bootstrapped https://github.com/$repo"
