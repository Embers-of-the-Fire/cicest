#!/usr/bin/env bash
set -euo pipefail

nix --version
nix run .#tests --print-build-logs

