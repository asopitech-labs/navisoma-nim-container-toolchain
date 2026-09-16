// Native-layer smoke test for containerd_bridge.{h,cc} against a live daemon — run via
// native/containerd/test-shim.sh, not part of the shipped shim and not run by `nimble test`
// (that suite never needs a container/daemon). Exercises the shim's own C ABI exactly as the
// Nim binding (src/native/containerd_raw.nim) does, so it validates the actual shipped
// boundary rather than the raw generated stubs — this is the "native interop test" the ABI/
// ownership/cleanup/errors contract calls for per docs/architecture.md section 15, independent
// of anything Nim.
#include "containerd_bridge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int failures = 0;

static void check(nvsm_containerd_result* r, const char* step) {
  if (!nvsm_containerd_result_ok(r)) {
    size_t len = 0;
    const char* msg = nvsm_containerd_result_error_message(r, &len);
    std::fprintf(stderr, "FAIL %s: %.*s\n", step, static_cast<int>(len), msg);
    failures++;
  } else {
    std::printf("OK   %s\n", step);
  }
}

static const char* cstr(const std::string& s) { return s.c_str(); }

int main() {
  char* connErr = nullptr;
  nvsm_containerd_client* client =
      nvsm_containerd_connect("/run/containerd/containerd.sock", "default", &connErr);
  if (!client) {
    std::fprintf(stderr, "FAIL connect: %s\n", connErr ? connErr : "(no message)");
    return 1;
  }
  std::printf("OK   connect\n");

  const std::string svc = "nvsm-bridge-test";
  const std::string image = "docker.io/library/busybox:latest";

  auto* r1 = nvsm_containerd_resolve_image(client, image.c_str(), image.size());
  check(r1, "resolve_image");
  std::string resolvedId = image;
  if (nvsm_containerd_result_ok(r1)) {
    size_t len = 0;
    const char* v = nvsm_containerd_result_string_value(r1, &len);
    resolvedId.assign(v, len);
  }
  nvsm_containerd_result_release(r1);

  const char* cmd[] = {"sleep", "30"};
  size_t cmdLens[] = {5, 2};
  auto* r2 = nvsm_containerd_create_container(client, svc.c_str(), svc.size(),
                                               resolvedId.c_str(), resolvedId.size(),
                                               cmd, cmdLens, 2,
                                               nullptr, nullptr, nullptr, nullptr, 0);
  check(r2, "create_container");
  nvsm_containerd_result_release(r2);

  auto* r3 = nvsm_containerd_start_container(client, svc.c_str(), svc.size());
  check(r3, "start_container");
  nvsm_containerd_result_release(r3);

  const char* okTest[] = {"echo", "hello"};
  size_t okTestLens[] = {4, 5};
  auto* r4 = nvsm_containerd_exec_health_probe(client, svc.c_str(), svc.size(), okTest, okTestLens, 2, 5000);
  if (nvsm_containerd_result_ok(r4) && nvsm_containerd_result_exit_code(r4) == 0) {
    std::printf("OK   exec_health_probe(echo) exit=0\n");
  } else {
    size_t len = 0;
    const char* msg = nvsm_containerd_result_error_message(r4, &len);
    std::fprintf(stderr, "FAIL exec_health_probe(echo): ok=%d exit=%d msg=%.*s\n",
                 nvsm_containerd_result_ok(r4), nvsm_containerd_result_exit_code(r4),
                 static_cast<int>(len), msg);
    failures++;
  }
  nvsm_containerd_result_release(r4);

  const char* failTest[] = {"false"};
  size_t failTestLens[] = {5};
  auto* r5 = nvsm_containerd_exec_health_probe(client, svc.c_str(), svc.size(), failTest, failTestLens, 1, 5000);
  if (nvsm_containerd_result_ok(r5) && nvsm_containerd_result_exit_code(r5) == 1) {
    std::printf("OK   exec_health_probe(false) exit=1\n");
  } else {
    size_t len = 0;
    const char* msg = nvsm_containerd_result_error_message(r5, &len);
    std::fprintf(stderr, "FAIL exec_health_probe(false): ok=%d exit=%d msg=%.*s\n",
                 nvsm_containerd_result_ok(r5), nvsm_containerd_result_exit_code(r5),
                 static_cast<int>(len), msg);
    failures++;
  }
  nvsm_containerd_result_release(r5);

  // A probe that outlives its deadline must be reported as a failed probe (exit 124), not left
  // to hang the call or surface as a backend error — this is exec_health_probe's own deadline
  // enforcement, independent of the Nim executor's pacing (which the integration test exercises).
  const char* hangTest[] = {"sleep", "30"};
  size_t hangTestLens[] = {5, 2};
  auto* r5b = nvsm_containerd_exec_health_probe(client, svc.c_str(), svc.size(), hangTest, hangTestLens, 2, 1000);
  if (nvsm_containerd_result_ok(r5b) && nvsm_containerd_result_exit_code(r5b) == 124) {
    std::printf("OK   exec_health_probe(sleep 30, timeout 1s) exit=124\n");
  } else {
    size_t len = 0;
    const char* msg = nvsm_containerd_result_error_message(r5b, &len);
    std::fprintf(stderr, "FAIL exec_health_probe(sleep 30, timeout 1s): ok=%d exit=%d msg=%.*s\n",
                 nvsm_containerd_result_ok(r5b), nvsm_containerd_result_exit_code(r5b),
                 static_cast<int>(len), msg);
    failures++;
  }
  nvsm_containerd_result_release(r5b);

  auto* r6 = nvsm_containerd_stop_container(client, svc.c_str(), svc.size());
  check(r6, "stop_container");
  nvsm_containerd_result_release(r6);

  auto* r7 = nvsm_containerd_remove_container(client, svc.c_str(), svc.size());
  check(r7, "remove_container");
  nvsm_containerd_result_release(r7);

  nvsm_containerd_close(client);

  std::printf(failures == 0 ? "\nALL OK\n" : "\n%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
