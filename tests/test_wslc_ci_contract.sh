#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
workflow="$root_dir/.github/workflows/wslc-integration.yml"

require() {
  if ! grep -F -q "$1" "$workflow"; then
    echo "missing WSLC CI contract: $1" >&2
    exit 1
  fi
}

require "workflow_dispatch:"
require "schedule:"
require "navisoma-wslc"
require "NAVISOMA_WSLC_WSL_DISTRIBUTION"
require "NAVISOMA_WSLC_RUNNER_READY"
require "WSLC integration is unexecuted"
require "exit 1"
require "tests/integration/wslc/run.sh"
require "if: always()"

if grep -F -q "actions/runners" "$workflow"; then
  echo "WSLC CI must not require repository-administration runner inventory access" >&2
  exit 1
fi
