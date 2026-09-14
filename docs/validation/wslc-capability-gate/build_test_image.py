#!/usr/bin/env python3
"""
NAVISOMA / Issue #15 - independent OCI/docker-save archive builder.

Not part of the NAVISOMA product, and not BuildKit, Docker, or WSLC: this
is a standalone script that talks only to a public OCI registry over plain
HTTPS (stdlib urllib) to fetch a small published image (docker.io
library/busybox) and re-package it as a docker-save-format tar under a new
repo:tag of our own choosing. It exists so the WSLC capability probe can
exercise WslcLoadSessionImageFromFile against a real, independently
produced, multi-file OCI-shaped archive carrying its own exact image
reference -- not a raw single-file rootfs tar, and not anything built by
a tool this spike is validating.

Usage: python3 build_test_image.py <output.tar> <new-repo:tag>
"""
import hashlib
import json
import sys
import tarfile
import io
import urllib.request

SOURCE_IMAGE = "library/busybox"
SOURCE_TAG = "latest"
REGISTRY = "https://registry-1.docker.io"
AUTH = "https://auth.docker.io/token"

MANIFEST_ACCEPT = ", ".join([
    "application/vnd.docker.distribution.manifest.list.v2+json",
    "application/vnd.docker.distribution.manifest.v2+json",
    "application/vnd.oci.image.index.v1+json",
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


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <output.tar> <new-repo:tag>", file=sys.stderr)
        return 1
    out_path, new_ref = sys.argv[1], sys.argv[2]

    token = get_token()

    manifest_bytes = http_get(f"{REGISTRY}/v2/{SOURCE_IMAGE}/manifests/{SOURCE_TAG}", token, MANIFEST_ACCEPT)
    manifest = json.loads(manifest_bytes)

    media_type = manifest.get("mediaType", "")
    if media_type.endswith("manifest.list.v2+json") or media_type.endswith("image.index.v1+json"):
        chosen = None
        for m in manifest["manifests"]:
            plat = m.get("platform", {})
            if plat.get("architecture") == "amd64" and plat.get("os") == "linux":
                chosen = m
                break
        if not chosen:
            raise SystemExit("no linux/amd64 entry in manifest list")
        digest = chosen["digest"]
        manifest_bytes = http_get(
            f"{REGISTRY}/v2/{SOURCE_IMAGE}/manifests/{digest}", token,
            "application/vnd.docker.distribution.manifest.v2+json, application/vnd.oci.image.manifest.v1+json")
        manifest = json.loads(manifest_bytes)

    config_digest = manifest["config"]["digest"]
    layers = manifest["layers"]

    # Fetched byte-for-byte and never re-serialized, so its own sha256 still
    # matches config_digest -- this is the config file's required name.
    config_bytes = http_get(f"{REGISTRY}/v2/{SOURCE_IMAGE}/blobs/{config_digest}", token)
    actual_config_hash = hashlib.sha256(config_bytes).hexdigest()
    expected_hash = config_digest.split(":", 1)[1]
    if actual_config_hash != expected_hash:
        raise SystemExit(f"config digest mismatch: expected {expected_hash} got {actual_config_hash}")

    layer_entries = []  # (archive_dir_name, decompressed_layer_bytes)
    for layer in layers:
        compressed = http_get(f"{REGISTRY}/v2/{SOURCE_IMAGE}/blobs/{layer['digest']}", token)
        import gzip
        decompressed = gzip.decompress(compressed) if layer["mediaType"].endswith("gzip") else compressed
        dir_name = layer["digest"].split(":", 1)[1]
        layer_entries.append((dir_name, decompressed))

    repo, _, tag = new_ref.partition(":")
    tag = tag or "latest"

    docker_manifest = [{
        "Config": f"{expected_hash}.json",
        "RepoTags": [f"{repo}:{tag}"],
        "Layers": [f"{d}/layer.tar" for d, _ in layer_entries],
    }]
    repositories = {repo: {tag: layer_entries[-1][0]}}

    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as tar:
        def add_bytes(name, data):
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))

        add_bytes(f"{expected_hash}.json", config_bytes)
        add_bytes("manifest.json", json.dumps(docker_manifest).encode())
        add_bytes("repositories", json.dumps(repositories).encode())
        for dir_name, layer_bytes in layer_entries:
            add_bytes(f"{dir_name}/VERSION", b"1.0")
            add_bytes(f"{dir_name}/json", json.dumps({"id": dir_name}).encode())
            add_bytes(f"{dir_name}/layer.tar", layer_bytes)

    with open(out_path, "wb") as f:
        f.write(buf.getvalue())

    print(f"wrote {out_path}: source={SOURCE_IMAGE}:{SOURCE_TAG} config={config_digest} "
          f"layers={len(layer_entries)} new_ref={repo}:{tag}")
    print(f"sha256({out_path}) will be printed by the caller")
    return 0


if __name__ == "__main__":
    sys.exit(main())
