#!/usr/bin/env bash
# push_to_github.sh
# Creates a private GitHub repo under the specified owner/repo and pushes the current
# local repository to it. Uses the 'gh' CLI if available and authenticated.

set -euo pipefail

REPO="fyp/raytracer"
VISIBILITY="private"
BRANCH="main"

# Check for gh
if command -v gh >/dev/null 2>&1; then
  echo "gh CLI found. Will try to create repository ${REPO} as ${VISIBILITY}."
  # create repo (if exists, gh will error; we capture and continue)
  if gh repo view "$REPO" >/dev/null 2>&1; then
    echo "Repository ${REPO} already exists. Skipping creation."
  else
    echo "Creating repository ${REPO}..."
    gh repo create "$REPO" --private --confirm || true
  fi
  # add remote and push
  git remote remove origin 2>/dev/null || true
  git remote add origin "https://github.com/${REPO}.git"
  git branch -M ${BRANCH}
  echo "Pushing ${BRANCH} to origin..."
  git push -u origin ${BRANCH}
  echo "Pushing tags..."
  git push --tags || true
  echo "Done. Repository should be available at https://github.com/${REPO}"
else
  cat <<EOF
The 'gh' CLI is not available. To push to GitHub privately, run these manual commands:

1) Create a private repo on GitHub (web UI) named 'raytracer' under the 'fyp' account (or your account).
   Make sure the repo is private.

2) Then run these commands locally in this project root:

   git remote add origin https://github.com/${REPO}.git
   git branch -M ${BRANCH}
   git push -u origin ${BRANCH}
   git push --tags

EOF
fi
