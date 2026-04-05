#!/usr/bin/env bash

SCRIPT_DIR=$(dirname "$0")
ROOT_DIR="$SCRIPT_DIR/.."

set -e

pushd "$ROOT_DIR" > /dev/null

echo

# Create .venv from the uv.lock (updates .venv to match the lockfile)
#uv sync --locked

# Refreshes uv.lock from the current project metadata in pyproject.toml.
uv lock
# Create .venv from the updated uv.lock (updates .venv to match the lockfile)
uv sync --locked

# Then use: -DPython3_EXECUTABLE=$ROOT_DIR/.venv/bin/python3

popd > /dev/null

set +e
