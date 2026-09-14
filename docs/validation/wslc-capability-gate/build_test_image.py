#!/usr/bin/env python3
"""
NAVISOMA / Issue #15 - independent OCI/docker-save archive loader-evidence
builder.

Not part of the NAVISOMA product, and not BuildKit, Docker, or WSLC: this
is a standalone script that talks only to a public OCI registry over plain
HTTPS (stdlib urllib) to fetch a small published image (docker.io
library/busybox), verify every blob it uses against its declared digest,
and re-package it as a docker-save-format tar under a new repo:tag of our
own choosing.

This produces real, independently-sourced, multi-file OCI-shaped archive
loader evidence: it proves WslcLoadSessionImageFromFile can consume an
archive carrying its own exact embedded image reference. It is explicitly
NOT Dockerfile/BuildKit build evidence -- no Dockerfile is built here, only
an already-published image's manifest/config/layers are fetched (each
verified against its own content digest) and re-tagged. From WSLC's C API
perspective the input contract is the same either way (bytes in the
docker-save shape), but this script's own provenance must not be described
as "a build" it did not perform.

Usage: python3 build_test_image.py <output.tar> <new-repo:tag> [source-ref]

  source-ref defaults to a pinned, immutable manifest digest (not a mutable
  tag like "latest"), so re-running this script later fetches the exact
  same bytes rather than silently tracking whatever "latest" points to by
  then. Override it (e.g. to point at a different pinned digest) only if
  you have re-verified the new digest belongs to the image you expect.
"""
import hashlib
import gzip
import json
import sys
import tarfile
import io
import urllib.request

SOURCE_IMAGE = "library/busybox"
# Pinned linux/amd64 manifest digest for library/busybox, resolved from the
# "latest" tag at the time this script was written and hardcoded here so
# the artifact this produces is reproducible regardless of what "latest"
# points to later. Verified via `Docker-Content-Digest` response header
# matching an independently computed sha256 of the manifest body.
SOURCE_REF = "sha256:1cfa4e2b09e127b9c4ed43578d3f3c18e7d44ea47b9ea98475c0cbe9086525f8"
REGISTRY = "https://registry-1.docker.io"
AUTH = "https://auth.docker.io/token"

MANIFEST_ACCEPT = ", ".join([
    "application/vnd.docker.distribution.manifest.v2+json",
    "application/vnd.oci.image.manifest.v1+json",
])


def http_get(url, token, accept="*/*"):
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {token}", "Accept": accept})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read()


def get_token():
    url = f"{AUTH}?service=registry.docker.io&scope=repository:{SOURCE_IMAGE}:pull"
    with urllib.request.urlopen(url, timeout=15) as resp:
        return json.load(resp)["token"]


def verify_digest(data, expected_digest, what):
    algo, expected_hex = expected_digest.split(":", 1)
    if algo != "sha256":
        raise SystemExit(f"unsupported digest algorithm for {what}: {algo}")
    actual_hex = hashlib.sha256(data).hexdigest()
    if actual_hex != expected_hex:
        raise SystemExit(f"{what} digest mismatch: expected {expected_hex} got {actual_hex}")
    return actual_hex


def main():
    if len(sys.argv) not in (3, 4):
        print(f"usage: {sys.argv[0]} <output.tar> <new-repo:tag> [source-ref]", file=sys.stderr)
        return 1
    out_path, new_ref = sys.argv[1], sys.argv[2]
    source_ref = sys.argv[3] if len(sys.argv) == 4 else SOURCE_REF

    token = get_token()

    # source_ref is a manifest digest (pinned, immutable) unless the caller
    # explicitly overrides it with a tag -- fetching by digest means the
    # registry's own content-addressing already guarantees we get exactly
    # that manifest, verified again below independently of the registry.
    manifest_bytes = http_get(f"{REGISTRY}/v2/{SOURCE_IMAGE}/manifests/{source_ref}", token, MANIFEST_ACCEPT)
    if source_ref.startswith("sha256:"):
        verify_digest(manifest_bytes, source_ref, "manifest")
    manifest_digest = "sha256:" + hashlib.sha256(manifest_bytes).hexdigest()
    manifest = json.loads(manifest_bytes)

    if manifest.get("mediaType", "").endswith("manifest.list.v2+json") or "manifests" in manifest:
        raise SystemExit(
            "source-ref resolved to a manifest list/index, not a single-platform manifest; "
            "pin a linux/amd64 child manifest digest instead")

    config_digest = manifest["config"]["digest"]
    layers = manifest["layers"]

    config_bytes = http_get(f"{REGISTRY}/v2/{SOURCE_IMAGE}/blobs/{config_digest}", token)
    config_hash = verify_digest(config_bytes, config_digest, "config blob")

    layer_entries = []  # (archive_dir_name, decompressed_layer_bytes, verified_digest)
    for layer in layers:
        compressed = http_get(f"{REGISTRY}/v2/{SOURCE_IMAGE}/blobs/{layer['digest']}", token)
        verify_digest(compressed, layer["digest"], f"layer blob {layer['digest']}")
        decompressed = gzip.decompress(compressed) if layer["mediaType"].endswith("gzip") else compressed
        dir_name = layer["digest"].split(":", 1)[1]
        layer_entries.append((dir_name, decompressed, layer["digest"]))

    repo, _, tag = new_ref.partition(":")
    tag = tag or "latest"

    docker_manifest = [{
        "Config": f"{config_hash}.json",
        "RepoTags": [f"{repo}:{tag}"],
        "Layers": [f"{d}/layer.tar" for d, _, _ in layer_entries],
    }]
    repositories = {repo: {tag: layer_entries[-1][0]}}

    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as tar:
        def add_bytes(name, data):
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            info.mtime = 0
            tar.addfile(info, io.BytesIO(data))

        add_bytes(f"{config_hash}.json", config_bytes)
        add_bytes("manifest.json", json.dumps(docker_manifest).encode())
        add_bytes("repositories", json.dumps(repositories).encode())
        for dir_name, layer_bytes, _ in layer_entries:
            add_bytes(f"{dir_name}/VERSION", b"1.0")
            add_bytes(f"{dir_name}/json", json.dumps({"id": dir_name}).encode())
            add_bytes(f"{dir_name}/layer.tar", layer_bytes)

    output_bytes = buf.getvalue()
    with open(out_path, "wb") as f:
        f.write(output_bytes)
    output_digest = hashlib.sha256(output_bytes).hexdigest()

    print(f"source: {SOURCE_IMAGE}@{source_ref}")
    print(f"  resolved manifest digest: {manifest_digest}")
    print(f"  config digest (verified): {config_digest}")
    for _, _, digest in layer_entries:
        print(f"  layer digest (verified): {digest}")
    print(f"new_ref baked into archive manifest.json: {repo}:{tag}")
    print(f"wrote {out_path} ({len(output_bytes)} bytes)")
    print(f"output artifact sha256: {output_digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
