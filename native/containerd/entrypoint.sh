#!/bin/sh
# Nested cgroup v2 requires the cgroup a process lives in to be free of "member" processes
# before that cgroup's controllers can be delegated to children (cgroup v2's no-internal-process
# rule). Rootless podman gives this container a private cgroup namespace whose root cgroup we'd
# otherwise sit in directly as PID 1 — so containerd's own task/snapshot cgroups (created per
# container by runc) fail with "cannot enter cgroupv2 ... with domain controllers -- it is in an
# invalid state". Moving ourselves into a child cgroup first leaves the namespace root empty and
# lets runc create/delegate child cgroups normally. Standard trick for containerd/Docker-in-container.
set -e
if [ -w /sys/fs/cgroup/cgroup.procs ]; then
  mkdir -p /sys/fs/cgroup/init
  echo $$ > /sys/fs/cgroup/init/cgroup.procs
  # Root is now free of member processes, so its controllers can be delegated to children
  # (runc creates per-task cgroups under here) — without this, controller files like
  # cpu.weight never appear in child cgroups and runc's cgroup setup fails.
  for c in $(cat /sys/fs/cgroup/cgroup.controllers); do
    echo "+$c" > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
  done
fi
exec containerd --config /etc/containerd/config.toml
