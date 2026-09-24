#!/usr/bin/env bash
# Docker Compose v2+ is the behavioral oracle for #18's fixed health-gated subset.
# NAVISOMA's matching behavior is covered by tests/test_executor.nim and the
# real containerd/WSLC scenarios; this script proves the oracle independently.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HEALTHY="$ROOT/tests/integration/containerd/healthy.compose.yaml"
UNHEALTHY="$ROOT/tests/integration/containerd/unhealthy.compose.yaml"
HEALTHY_PROJECT="navisoma-diff-healthy-$$"
UNHEALTHY_PROJECT="navisoma-diff-unhealthy-$$"

version="$(docker compose version 2>&1)" || {
  echo "SKIP: Docker Compose v2+ is unavailable" >&2
  exit 77
}
if [[ "$version" != Docker\ Compose\ version\ v[2-9]* ]]; then
  echo "SKIP: requires Docker Compose v2+, got: $version" >&2
  exit 77
fi
up_help="$(docker compose up --help)"
if [[ "$up_help" != *--wait* ]]; then
  echo "SKIP: Docker Compose v2+ lacks 'up --wait'" >&2
  exit 77
fi

cleanup() {
  docker compose -p "$HEALTHY_PROJECT" -f "$HEALTHY" down --volumes --remove-orphans >/dev/null 2>&1 || true
  docker compose -p "$UNHEALTHY_PROJECT" -f "$UNHEALTHY" down --volumes --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker compose -p "$HEALTHY_PROJECT" -f "$HEALTHY" up --detach --wait --wait-timeout 30
db_id="$(docker compose -p "$HEALTHY_PROJECT" -f "$HEALTHY" ps -q db)"
api_id="$(docker compose -p "$HEALTHY_PROJECT" -f "$HEALTHY" ps -q api)"
[[ -n "$db_id" && -n "$api_id" ]] || { echo "FAIL: healthy services did not start" >&2; exit 1; }
[[ "$(docker inspect -f '{{.State.Health.Status}}' "$db_id")" == healthy ]] || {
  echo "FAIL: db did not become healthy" >&2; exit 1;
}
[[ "$(docker inspect -f '{{.State.Running}}' "$api_id")" == true ]] || {
  echo "FAIL: api did not start" >&2; exit 1;
}
db_healthy_at="$(docker inspect -f '{{(index .State.Health.Log 0).End}}' "$db_id")"
api_started_at="$(docker inspect -f '{{.State.StartedAt}}' "$api_id")"
db_healthy_time="${db_healthy_at%%+*}"
db_healthy_time="${db_healthy_time%%Z}"
db_healthy_time="${db_healthy_time/T/ }"
api_started_time="${api_started_at%%+*}"
api_started_time="${api_started_time%%Z}"
[[ "$api_started_time" > "$db_healthy_time" ]] || {
  echo "FAIL: api started before db became healthy" >&2; exit 1;
}

if docker compose -p "$UNHEALTHY_PROJECT" -f "$UNHEALTHY" up --detach --wait --wait-timeout 30; then
  echo "FAIL: unhealthy dependency unexpectedly succeeded" >&2
  exit 1
fi
api_id="$(docker compose -p "$UNHEALTHY_PROJECT" -f "$UNHEALTHY" ps -aq api)"
if [[ -n "$api_id" && "$(docker inspect -f '{{.State.Running}}' "$api_id")" == true ]]; then
  echo "FAIL: api started despite unhealthy db" >&2
  exit 1
fi

echo "OK: Docker Compose health-gate oracle"
