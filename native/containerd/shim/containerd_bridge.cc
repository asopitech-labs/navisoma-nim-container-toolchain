// Implementation of containerd_bridge.h. Talks to containerd purely over the generated gRPC/
// protobuf C++ stubs (native/containerd/generated/) — no shelling out to `ctr`, no
// reimplementation of gRPC/protobuf/registry/content-addressing itself.
//
// containerd-specific conventions this file depends on (verified empirically against a live
// pinned v2.3.5 daemon and/or against containerd's own source — see the comment at each site,
// not guessed):
//   - google.protobuf.Any type_url for a containerd-defined proto message (OCIRegistry,
//     ImageStore) is just that message's bare full proto name, e.g.
//     "containerd.types.transfer.OCIRegistry" — confirmed by probing the real Transfer service.
//   - google.protobuf.Any type_url for the OCI runtime-spec types (Spec, Process) is
//     "types.containerd.io/opencontainers/runtime-spec/<major>/<TypeName>" with JSON (not
//     protobuf) encoding — confirmed from containerd's core/runtime/typeurl.go registration.
//   - The rootfs "parent" snapshot key for a new container is the OCI "ChainID" of its image's
//     layer DiffIDs — confirmed from containerd's vendored
//     opencontainers/image-spec/identity/chainid.go: chainID[0]=diffID[0],
//     chainID[i]=sha256(chainID[i-1] + " " + diffID[i]).
//   - The "native" snapshotter is used throughout (not containerd's default "overlayfs") —
//     nested overlayfs-on-overlayfs mounts fail under the rootless-podman dev daemon this was
//     developed and tested against (native/containerd/dev-daemon.sh); "native" works everywhere
//     overlayfs would, just without reflink/copy-on-write speed.
#include "containerd_bridge.h"

#include <grpcpp/grpcpp.h>
#include <google/protobuf/empty.pb.h>
#include <nlohmann/json.hpp>

#include "services/transfer/v1/transfer.grpc.pb.h"
#include "services/containers/v1/containers.grpc.pb.h"
#include "services/images/v1/images.grpc.pb.h"
#include "services/content/v1/content.grpc.pb.h"
#include "services/tasks/v1/tasks.grpc.pb.h"
#include "services/snapshots/v1/snapshots.grpc.pb.h"
#include "types/transfer/registry.pb.h"
#include "types/transfer/imagestore.pb.h"
#include "types/mount.pb.h"
#include "types/platform.pb.h"

#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

using containerd::services::transfer::v1::Transfer;
using containerd::services::transfer::v1::TransferRequest;
using containerd::services::containers::v1::Containers;
using containerd::services::containers::v1::Container;
using containerd::services::containers::v1::CreateContainerRequest;
using containerd::services::containers::v1::DeleteContainerRequest;
using containerd::services::images::v1::Images;
using containerd::services::images::v1::GetImageRequest;
using containerd::services::images::v1::GetImageResponse;
using containerd::services::content::v1::Content;
using containerd::services::content::v1::ReadContentRequest;
using containerd::services::content::v1::ReadContentResponse;
using containerd::services::tasks::v1::Tasks;
using containerd::services::tasks::v1::CreateTaskRequest;
using containerd::services::tasks::v1::StartRequest;
using containerd::services::tasks::v1::KillRequest;
using containerd::services::tasks::v1::DeleteTaskRequest;
using containerd::services::tasks::v1::ExecProcessRequest;
using containerd::services::tasks::v1::WaitRequest;
using containerd::services::tasks::v1::WaitResponse;
using containerd::services::snapshots::v1::Snapshots;
using containerd::services::snapshots::v1::PrepareSnapshotRequest;
using containerd::services::snapshots::v1::PrepareSnapshotResponse;
using containerd::services::snapshots::v1::RemoveSnapshotRequest;
using containerd::types::transfer::OCIRegistry;
using containerd::types::transfer::ImageStore;
using containerd::types::Mount;

// A tiny self-contained SHA-256 (used only for the content-addressable ChainID computation
// below) — deliberately not reusing OpenSSL/BoringSSL symbols transitively linked in via gRPC:
// those are private implementation details of gRPC's own build, not a stable API this project
// should depend on. Defined at the bottom of this file.
std::string sha256Hex(const std::string& data);

namespace {

constexpr const char* kSnapshotter = "native";
constexpr const char* kRuntime = "io.containerd.runc.v2";
constexpr const char* kSpecTypeUrl = "types.containerd.io/opencontainers/runtime-spec/1/Spec";
constexpr const char* kProcessTypeUrl = "types.containerd.io/opencontainers/runtime-spec/1/Process";
constexpr const char* kPlatformOS = "linux";
constexpr const char* kPlatformArch = "amd64";
// backend.nim's execHealthProbe takes only a command, no environment (BackendPort is a fixed,
// narrow interface — see its own doc comment) — a health probe has no access to the container's
// actual configured env, so it needs a standard PATH of its own to resolve bare executable
// names like "curl" or "echo" at all.
constexpr const char* kDefaultPathEnv = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

} // namespace

struct nvsm_containerd_client {
  std::string ns;
  std::shared_ptr<grpc::Channel> channel;
  std::unique_ptr<Transfer::Stub> transfer;
  std::unique_ptr<Containers::Stub> containers;
  std::unique_ptr<Images::Stub> images;
  std::unique_ptr<Content::Stub> content;
  std::unique_ptr<Tasks::Stub> tasks;
  std::unique_ptr<Snapshots::Stub> snapshots;
  int execCounter = 0;
};

struct nvsm_containerd_result {
  bool ok = false;
  std::string error;
  std::string stringValue;
  int exitCode = 0;
};

namespace {

std::unique_ptr<grpc::ClientContext> newCtx(const nvsm_containerd_client* c) {
  auto ctx = std::make_unique<grpc::ClientContext>();
  ctx->AddMetadata("containerd-namespace", c->ns);
  return ctx;
}

nvsm_containerd_result* failResult(const std::string& msg) {
  auto* r = new nvsm_containerd_result();
  r->ok = false;
  r->error = msg;
  return r;
}

nvsm_containerd_result* okResult() {
  auto* r = new nvsm_containerd_result();
  r->ok = true;
  return r;
}

bool isNotFound(const grpc::Status& status) {
  return status.error_code() == grpc::StatusCode::NOT_FOUND;
}

// Reads an entire content-store blob by digest via the streaming Content.Read RPC.
std::string readAllContent(nvsm_containerd_client* client, const std::string& digest) {
  ReadContentRequest req;
  req.set_digest(digest);
  auto ctx = newCtx(client);
  auto reader = client->content->Read(ctx.get(), req);
  std::string out;
  ReadContentResponse chunk;
  while (reader->Read(&chunk)) {
    out.append(chunk.data());
  }
  auto status = reader->Finish();
  if (!status.ok()) {
    throw std::runtime_error("content read " + digest + ": " + status.error_message());
  }
  return out;
}

struct ImageRootfs {
  std::string chainId;
  json config; // the config blob's top-level "config" object (Entrypoint/Cmd/Env/WorkingDir/User)
};

// Resolves an image reference already present in the image store (per resolve_image) down to
// the parent snapshot key (chain ID) new containers should Prepare from, and the image's
// default process configuration. See containerd's vendored
// opencontainers/image-spec/identity/chainid.go for the chain ID algorithm this mirrors, and
// core/images/image.go's RootFS()/Config() for the manifest/config resolution this mirrors —
// every client (containerd's own Go client included) does this walk itself; there is no RPC
// that returns a ready-to-use parent snapshot key directly.
ImageRootfs resolveImageRootfs(nvsm_containerd_client* client, const std::string& imageRef) {
  GetImageRequest getReq;
  getReq.set_name(imageRef);
  auto ctx1 = newCtx(client);
  GetImageResponse getResp;
  auto status = client->images->Get(ctx1.get(), getReq, &getResp);
  if (!status.ok()) {
    throw std::runtime_error("images.get: " + status.error_message());
  }

  std::string manifestDigest = getResp.image().target().digest();
  std::string topMediaType = getResp.image().target().media_type();
  std::string topBytes = readAllContent(client, manifestDigest);

  json manifestJson;
  if (topMediaType == "application/vnd.oci.image.index.v1+json" ||
      topMediaType == "application/vnd.docker.distribution.manifest.list.v2+json") {
    json index = json::parse(topBytes);
    std::string chosenDigest;
    for (const auto& m : index.at("manifests")) {
      auto platform = m.value("platform", json::object());
      if (platform.value("os", "") == kPlatformOS &&
          platform.value("architecture", "") == kPlatformArch) {
        chosenDigest = m.at("digest").get<std::string>();
        break;
      }
    }
    if (chosenDigest.empty()) {
      throw std::runtime_error("no " + std::string(kPlatformOS) + "/" + kPlatformArch +
                                " manifest in image index for " + imageRef);
    }
    manifestJson = json::parse(readAllContent(client, chosenDigest));
  } else {
    manifestJson = json::parse(topBytes);
  }

  std::string configDigest = manifestJson.at("config").at("digest").get<std::string>();
  json configBlob = json::parse(readAllContent(client, configDigest));

  std::vector<std::string> diffIds;
  for (const auto& d : configBlob.at("rootfs").at("diff_ids")) {
    diffIds.push_back(d.get<std::string>());
  }
  if (diffIds.empty()) {
    throw std::runtime_error("image " + imageRef + " has no rootfs layers");
  }

  // ChainID: chain[0] = diffIds[0]; chain[i] = sha256("<chain[i-1]> <diffIds[i]>") — the
  // opencontainers/image-spec/identity algorithm (see the file-level comment above).
  std::string chain = diffIds[0];
  for (size_t i = 1; i < diffIds.size(); i++) {
    chain = "sha256:" + sha256Hex(chain + " " + diffIds[i]);
  }

  ImageRootfs result;
  result.chainId = chain;
  result.config = configBlob.value("config", json::object());
  return result;
}

// Builds the JSON body for a google.protobuf.Any wrapping an OCI runtime-spec `Process` or
// `Spec` (containerd decodes these as JSON per the typeurl registration noted above, not as
// protobuf — do not protobuf-serialize this).
json buildProcessJson(const std::vector<std::string>& args, const std::vector<std::string>& env,
                      const std::string& cwd) {
  json p;
  p["terminal"] = false;
  p["user"] = {{"uid", 0}, {"gid", 0}};
  p["args"] = args;
  p["env"] = env;
  p["cwd"] = cwd.empty() ? "/" : cwd;
  p["noNewPrivileges"] = true;
  return p;
}

json buildSpecJson(const std::string& hostname, const std::vector<std::string>& args,
                    const std::vector<std::string>& env, const std::string& cwd) {
  json spec;
  spec["ociVersion"] = "1.0.2";
  spec["hostname"] = hostname;
  spec["process"] = buildProcessJson(args, env, cwd);
  spec["root"] = {{"path", "rootfs"}, {"readonly", false}};
  spec["mounts"] = json::array({
      json{{"destination", "/proc"}, {"type", "proc"}, {"source", "proc"}},
      json{{"destination", "/dev"}, {"type", "tmpfs"}, {"source", "tmpfs"},
           {"options", json::array({"nosuid", "strictatime", "mode=755", "size=65536k"})}},
      json{{"destination", "/dev/pts"}, {"type", "devpts"}, {"source", "devpts"},
           {"options", json::array({"nosuid", "noexec", "newinstance", "ptmxmode=0666", "mode=0620"})}},
      json{{"destination", "/dev/shm"}, {"type", "tmpfs"}, {"source", "shm"},
           {"options", json::array({"nosuid", "noexec", "nodev", "mode=1777", "size=65536k"})}},
      json{{"destination", "/sys"}, {"type", "sysfs"}, {"source", "sysfs"},
           {"options", json::array({"nosuid", "noexec", "nodev", "ro"})}},
  });
  spec["linux"] = {
      {"namespaces", json::array({
                          json{{"type", "pid"}},
                          json{{"type", "ipc"}},
                          json{{"type", "uts"}},
                          json{{"type", "mount"}},
                      })},
  };
  return spec;
}

std::vector<std::string> mergeArgs(const json& imageConfig, const std::vector<std::string>& command) {
  std::vector<std::string> args;
  if (imageConfig.contains("Entrypoint") && imageConfig.at("Entrypoint").is_array()) {
    for (const auto& a : imageConfig.at("Entrypoint")) args.push_back(a.get<std::string>());
  }
  if (!command.empty()) {
    args.insert(args.end(), command.begin(), command.end());
  } else if (imageConfig.contains("Cmd") && imageConfig.at("Cmd").is_array()) {
    for (const auto& a : imageConfig.at("Cmd")) args.push_back(a.get<std::string>());
  }
  return args;
}

std::vector<std::string> mergeEnv(const json& imageConfig,
                                   const std::vector<std::pair<std::string, std::string>>& env) {
  std::vector<std::string> result;
  if (imageConfig.contains("Env") && imageConfig.at("Env").is_array()) {
    for (const auto& e : imageConfig.at("Env")) result.push_back(e.get<std::string>());
  }
  for (const auto& kv : env) {
    result.push_back(kv.first + "=" + kv.second);
  }
  return result;
}

} // namespace

std::string sha256Hex(const std::string& data) {
  static const uint32_t k[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

  std::string msg = data;
  uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back(static_cast<char>(0x00));
  for (int i = 7; i >= 0; i--) msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xff));

  auto rotr = [](uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); };

  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = (static_cast<uint8_t>(msg[chunk + i * 4]) << 24) |
             (static_cast<uint8_t>(msg[chunk + i * 4 + 1]) << 16) |
             (static_cast<uint8_t>(msg[chunk + i * 4 + 2]) << 8) |
             (static_cast<uint8_t>(msg[chunk + i * 4 + 3]));
    }
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
      uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = s0 + maj;
      hh = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int i = 0; i < 8; i++) out << std::setw(8) << h[i];
  return out.str();
}

extern "C" {

int nvsm_containerd_result_ok(const nvsm_containerd_result* r) { return r->ok ? 1 : 0; }

const char* nvsm_containerd_result_error_message(const nvsm_containerd_result* r, size_t* out_len) {
  if (out_len) *out_len = r->error.size();
  return r->error.c_str();
}

const char* nvsm_containerd_result_string_value(const nvsm_containerd_result* r, size_t* out_len) {
  if (out_len) *out_len = r->stringValue.size();
  return r->stringValue.c_str();
}

int nvsm_containerd_result_exit_code(const nvsm_containerd_result* r) { return r->exitCode; }

void nvsm_containerd_result_release(nvsm_containerd_result* r) { delete r; }

void nvsm_containerd_free_string(char* s) { delete[] s; }

nvsm_containerd_client* nvsm_containerd_connect(const char* socket_path,
                                                 const char* containerd_namespace,
                                                 char** out_error) {
  try {
    auto* client = new nvsm_containerd_client();
    client->ns = containerd_namespace ? containerd_namespace : "default";
    std::string target = std::string("unix://") + (socket_path ? socket_path : "");
    client->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    client->transfer = Transfer::NewStub(client->channel);
    client->containers = Containers::NewStub(client->channel);
    client->images = Images::NewStub(client->channel);
    client->content = Content::NewStub(client->channel);
    client->tasks = Tasks::NewStub(client->channel);
    client->snapshots = Snapshots::NewStub(client->channel);
    return client;
  } catch (const std::exception& e) {
    if (out_error) {
      std::string msg = e.what();
      char* buf = new char[msg.size() + 1];
      std::memcpy(buf, msg.c_str(), msg.size() + 1);
      *out_error = buf;
    }
    return nullptr;
  }
}

void nvsm_containerd_close(nvsm_containerd_client* client) { delete client; }

nvsm_containerd_result* nvsm_containerd_resolve_image(nvsm_containerd_client* client,
                                                        const char* image_ref, size_t image_ref_len) {
  try {
    std::string ref(image_ref, image_ref_len);

    OCIRegistry src;
    src.set_reference(ref);
    ImageStore dst;
    dst.set_name(ref);
    // Without an UnpackConfiguration, Transfer only pulls manifest/config/layer *content* into
    // the content store — it does not unpack layers into the snapshotter create_container's
    // Snapshots.Prepare needs a parent for. Asking for the unpack here (server-side, standard
    // containerd behavior — this is exactly what `ctr images pull --unpack` / `ctr run` trigger)
    // means the chain ID this file computes in resolveImageRootfs() below is guaranteed to
    // already exist as a committed snapshot by the time create_container runs.
    auto* unpack = dst.add_unpacks();
    unpack->set_snapshotter(kSnapshotter);
    unpack->mutable_platform()->set_os(kPlatformOS);
    unpack->mutable_platform()->set_architecture(kPlatformArch);

    TransferRequest req;
    auto* srcAny = req.mutable_source();
    srcAny->set_type_url("containerd.types.transfer.OCIRegistry");
    srcAny->set_value(src.SerializeAsString());
    auto* dstAny = req.mutable_destination();
    dstAny->set_type_url("containerd.types.transfer.ImageStore");
    dstAny->set_value(dst.SerializeAsString());

    auto ctx = newCtx(client);
    google::protobuf::Empty resp;
    auto status = client->transfer->Transfer(ctx.get(), req, &resp);
    if (!status.ok()) {
      return failResult("transfer: " + status.error_message());
    }
    auto* r = okResult();
    r->stringValue = ref;
    return r;
  } catch (const std::exception& e) {
    return failResult(e.what());
  }
}

nvsm_containerd_result* nvsm_containerd_create_container(
    nvsm_containerd_client* client,
    const char* service_name, size_t service_name_len,
    const char* resolved_image_id, size_t resolved_image_id_len,
    const char* const* command, const size_t* command_lens, size_t command_len,
    const char* const* env_keys, const size_t* env_key_lens,
    const char* const* env_values, const size_t* env_value_lens, size_t env_len) {
  try {
    std::string id(service_name, service_name_len);
    std::string imageRef(resolved_image_id, resolved_image_id_len);

    std::vector<std::string> cmd;
    for (size_t i = 0; i < command_len; i++) cmd.emplace_back(command[i], command_lens[i]);
    std::vector<std::pair<std::string, std::string>> env;
    for (size_t i = 0; i < env_len; i++) {
      env.emplace_back(std::string(env_keys[i], env_key_lens[i]),
                        std::string(env_values[i], env_value_lens[i]));
    }

    ImageRootfs rootfs = resolveImageRootfs(client, imageRef);

    PrepareSnapshotRequest prepReq;
    prepReq.set_snapshotter(kSnapshotter);
    prepReq.set_key(id);
    prepReq.set_parent(rootfs.chainId);
    auto ctx1 = newCtx(client);
    PrepareSnapshotResponse prepResp;
    auto status = client->snapshots->Prepare(ctx1.get(), prepReq, &prepResp);
    if (!status.ok()) {
      return failResult("snapshots.prepare: " + status.error_message());
    }

    std::vector<std::string> args = mergeArgs(rootfs.config, cmd);
    if (args.empty()) {
      return failResult("service '" + id + "' has no command and the image has no default CMD/ENTRYPOINT");
    }
    std::vector<std::string> envStrings = mergeEnv(rootfs.config, env);
    std::string cwd = rootfs.config.value("WorkingDir", "");
    json specJson = buildSpecJson(id, args, envStrings, cwd);
    std::string specBytes = specJson.dump();

    Container container;
    container.set_id(id);
    container.set_image(imageRef);
    container.mutable_runtime()->set_name(kRuntime);
    container.mutable_spec()->set_type_url(kSpecTypeUrl);
    container.mutable_spec()->set_value(specBytes);
    container.set_snapshotter(kSnapshotter);
    container.set_snapshot_key(id);

    CreateContainerRequest createReq;
    *createReq.mutable_container() = container;
    auto ctx2 = newCtx(client);
    containerd::services::containers::v1::CreateContainerResponse createResp;
    status = client->containers->Create(ctx2.get(), createReq, &createResp);
    if (!status.ok()) {
      return failResult("containers.create: " + status.error_message());
    }

    CreateTaskRequest taskReq;
    taskReq.set_container_id(id);
    // An empty stdio path is not "no redirection" — it leaves stdout/stderr disconnected, and a
    // process that writes to a disconnected fd can itself exit non-zero (observed empirically:
    // busybox `echo` on an exec with empty stdio exited 1). MVP doesn't capture logs, so send
    // output to /dev/null rather than leave it unset.
    taskReq.set_stdout("/dev/null");
    taskReq.set_stderr("/dev/null");
    for (const auto& m : prepResp.mounts()) {
      *taskReq.add_rootfs() = m;
    }
    auto ctx3 = newCtx(client);
    containerd::services::tasks::v1::CreateTaskResponse taskResp;
    status = client->tasks->Create(ctx3.get(), taskReq, &taskResp);
    if (!status.ok()) {
      return failResult("tasks.create: " + status.error_message());
    }

    return okResult();
  } catch (const std::exception& e) {
    return failResult(e.what());
  }
}

nvsm_containerd_result* nvsm_containerd_start_container(nvsm_containerd_client* client,
                                                          const char* service_name, size_t service_name_len) {
  try {
    StartRequest req;
    req.set_container_id(std::string(service_name, service_name_len));
    auto ctx = newCtx(client);
    containerd::services::tasks::v1::StartResponse resp;
    auto status = client->tasks->Start(ctx.get(), req, &resp);
    if (!status.ok()) {
      return failResult("tasks.start: " + status.error_message());
    }
    return okResult();
  } catch (const std::exception& e) {
    return failResult(e.what());
  }
}

nvsm_containerd_result* nvsm_containerd_exec_health_probe(
    nvsm_containerd_client* client,
    const char* service_name, size_t service_name_len,
    const char* const* test, const size_t* test_lens, size_t test_len) {
  try {
    std::string id(service_name, service_name_len);
    std::vector<std::string> args;
    for (size_t i = 0; i < test_len; i++) args.emplace_back(test[i], test_lens[i]);

    std::string execId = "probe-" + std::to_string(++client->execCounter);
    json processJson = buildProcessJson(args, {kDefaultPathEnv}, "/");

    ExecProcessRequest execReq;
    execReq.set_container_id(id);
    execReq.set_exec_id(execId);
    execReq.set_stdout("/dev/null");
    execReq.set_stderr("/dev/null");
    execReq.mutable_spec()->set_type_url(kProcessTypeUrl);
    execReq.mutable_spec()->set_value(processJson.dump());

    auto ctx1 = newCtx(client);
    google::protobuf::Empty execResp;
    auto status = client->tasks->Exec(ctx1.get(), execReq, &execResp);
    if (!status.ok()) {
      return failResult("tasks.exec: " + status.error_message());
    }

    StartRequest startReq;
    startReq.set_container_id(id);
    startReq.set_exec_id(execId);
    auto ctx2 = newCtx(client);
    containerd::services::tasks::v1::StartResponse startResp;
    status = client->tasks->Start(ctx2.get(), startReq, &startResp);
    if (!status.ok()) {
      return failResult("tasks.start(exec): " + status.error_message());
    }

    WaitRequest waitReq;
    waitReq.set_container_id(id);
    waitReq.set_exec_id(execId);
    auto ctx3 = newCtx(client);
    WaitResponse waitResp;
    status = client->tasks->Wait(ctx3.get(), waitReq, &waitResp);
    if (!status.ok()) {
      return failResult("tasks.wait(exec): " + status.error_message());
    }

    containerd::services::tasks::v1::DeleteProcessRequest delReq;
    delReq.set_container_id(id);
    delReq.set_exec_id(execId);
    auto ctx4 = newCtx(client);
    containerd::services::tasks::v1::DeleteResponse delResp;
    client->tasks->DeleteProcess(ctx4.get(), delReq, &delResp); // best-effort; exit code already captured

    auto* r = okResult();
    r->exitCode = static_cast<int>(waitResp.exit_status());
    return r;
  } catch (const std::exception& e) {
    return failResult(e.what());
  }
}

nvsm_containerd_result* nvsm_containerd_stop_container(nvsm_containerd_client* client,
                                                         const char* service_name, size_t service_name_len) {
  try {
    std::string id(service_name, service_name_len);

    KillRequest killReq;
    killReq.set_container_id(id);
    killReq.set_signal(9); // SIGKILL — the MVP's stop semantics are "stop now", not graceful.
    killReq.set_all(true);
    auto ctx1 = newCtx(client);
    google::protobuf::Empty killResp;
    auto status = client->tasks->Kill(ctx1.get(), killReq, &killResp);
    if (!status.ok()) {
      if (isNotFound(status)) return okResult(); // nothing running to stop
      return failResult("tasks.kill: " + status.error_message());
    }

    WaitRequest waitReq;
    waitReq.set_container_id(id);
    auto ctx2 = newCtx(client);
    WaitResponse waitResp;
    status = client->tasks->Wait(ctx2.get(), waitReq, &waitResp);
    if (!status.ok() && !isNotFound(status)) {
      return failResult("tasks.wait: " + status.error_message());
    }
    return okResult();
  } catch (const std::exception& e) {
    return failResult(e.what());
  }
}

nvsm_containerd_result* nvsm_containerd_remove_container(nvsm_containerd_client* client,
                                                           const char* service_name, size_t service_name_len) {
  try {
    std::string id(service_name, service_name_len);

    DeleteTaskRequest delTaskReq;
    delTaskReq.set_container_id(id);
    auto ctx1 = newCtx(client);
    containerd::services::tasks::v1::DeleteResponse delTaskResp;
    auto status = client->tasks->Delete(ctx1.get(), delTaskReq, &delTaskResp);
    if (!status.ok() && !isNotFound(status)) {
      return failResult("tasks.delete: " + status.error_message());
    }

    DeleteContainerRequest delContReq;
    delContReq.set_id(id);
    auto ctx2 = newCtx(client);
    google::protobuf::Empty delContResp;
    status = client->containers->Delete(ctx2.get(), delContReq, &delContResp);
    if (!status.ok() && !isNotFound(status)) {
      return failResult("containers.delete: " + status.error_message());
    }

    RemoveSnapshotRequest delSnapReq;
    delSnapReq.set_snapshotter(kSnapshotter);
    delSnapReq.set_key(id);
    auto ctx3 = newCtx(client);
    google::protobuf::Empty delSnapResp;
    status = client->snapshots->Remove(ctx3.get(), delSnapReq, &delSnapResp);
    if (!status.ok() && !isNotFound(status)) {
      return failResult("snapshots.remove: " + status.error_message());
    }

    return okResult();
  } catch (const std::exception& e) {
    return failResult(e.what());
  }
}

} // extern "C"
