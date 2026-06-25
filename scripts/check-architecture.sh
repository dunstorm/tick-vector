#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if ! command -v rg >/dev/null 2>&1; then
  echo "architecture check requires ripgrep (rg)" >&2
  exit 1
fi

failures=0

check_no_matches() {
  local label="$1"
  local pattern="$2"
  shift 2

  local matches
  if matches=$(rg -n --glob '*.cpp' --glob '*.hpp' "$pattern" "$@" 2>/dev/null); then
    printf '\n%s\n%s\n' "$label" "$matches" >&2
    failures=1
  fi
}

check_no_matches \
  "src/app must not include core, adapter, or UI headers:" \
  '^#include "(core|adapters|ui)/' \
  src/app

check_no_matches \
  "src/core must not include adapter or UI headers:" \
  '^#include "(adapters|ui)/' \
  src/core

check_no_matches \
  "src/adapters must not include UI headers:" \
  '^#include "ui/' \
  src/adapters

check_no_matches \
  "UI must not include concrete adapter implementations:" \
  '^#include "adapters/(Rithmic|Simulated)' \
  src/ui

check_no_matches \
  "Generated protobuf headers must stay out of app, core, UI, and main:" \
  '^#include ".*\.pb\.h"' \
  src/app src/core src/ui src/main.cpp

check_no_matches \
  "Protobuf library headers must stay inside adapters:" \
  '<google/protobuf' \
  src/app src/core src/ui src/main.cpp

if matches=$(rg -n --glob '*.cpp' --glob '*.hpp' '^#include "adapters/' src/ui 2>/dev/null); then
  disallowed=$(printf '%s\n' "$matches" | rg -v 'TradingAdapterFactory\.hpp' || true)
  if [[ -n "$disallowed" ]]; then
    printf '\nUI may only include the adapter factory boundary from src/adapters:\n%s\n' "$disallowed" >&2
    failures=1
  fi
fi

if (( failures != 0 )); then
  printf '\nArchitecture checks failed.\n' >&2
  exit 1
fi

echo "Architecture checks passed."
