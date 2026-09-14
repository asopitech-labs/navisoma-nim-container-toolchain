/*
 * NAVISOMA / Issue #15 - disposable WSLC C API capability probe.
 *
 * This program is NOT part of the NAVISOMA product. It exists only to call
 * the pinned WSLC SDK C API (wslcsdk.h / wslcsdk.lib / wslcsdk.dll from the
 * Microsoft.WSL.Containers 2.9.9 NuGet package) directly, on a real Windows
 * + WSL Containers host, and report per-capability PASS/FAIL/SKIP lines.
 *
 * It deliberately does not use the `wslc`/`container` CLI for anything the
 * capability table depends on: CLI success is not treated as C API success.
 *
 * Every resource this probe creates (session, named volume, container,
 * tagged/imported images) is torn down before the process exits, and the
 * probe is designed to be run twice in a row with fixed, deterministic
 * names so a second run only succeeds if the first run's cleanup was
 * complete.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * wslcsdk.h (from the pinned Microsoft.WSL.Containers 2.9.9 package) is
 * written against MSVC/WDK headers. These portability shims are ours, not
 * the vendor's; wslcsdk.h itself is used byte-for-byte unmodified.
 */
#ifndef EXTERN_C_START
#ifdef __cplusplus
#define EXTERN_C_START extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C_START
#define EXTERN_C_END
#endif
#endif

#ifndef __callback
#define __callback
#endif

#include "wslcsdk.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "ole32.lib")

/* ---- fixed, deterministic probe identifiers (see docs/validation/wslc-capability-gate.md) ---- */
static const wchar_t *kSessionName = L"navisoma-wslc-probe";
static const wchar_t *kStoragePath = L"C:\\Users\\asopitech\\AppData\\Local\\Temp\\navisoma-wslc-probe\\storage";
static const char *kPullImage = "docker.io/library/alpine:3.19";
static const char *kTaggedRepo = "navisoma-probe-worker";
static const char *kTaggedTag = "ci";
static const char *kTaggedRef = "navisoma-probe-worker:ci";
static const char *kImportRef = "navisoma-probe-import:test";
static const char *kVolumeName = "navisoma-wslc-probe-vol";
static const char *kContainerName = "navisoma-wslc-probe-c1";
static const char *kPeerContainerName = "navisoma-wslc-probe-c2";
static const char *kImportContainerName = "navisoma-wslc-probe-import";
static const unsigned short kHostPort = 18080;
static const unsigned short kPeerPort = 9000;

static int gFailures = 0;

static void logResult(const char *capability, int pass, const char *detail) {
    printf("CHECK %-28s %-4s %s\n", capability, pass ? "PASS" : "FAIL", detail ? detail : "");
    fflush(stdout);
    if (!pass) {
        gFailures++;
    }
}

static void logSkip(const char *capability, const char *reason) {
    printf("CHECK %-28s SKIP %s\n", capability, reason ? reason : "");
    fflush(stdout);
}

static void freeSdkString(PWSTR s) {
    if (s) {
        CoTaskMemFree(s);
    }
}

static void describeHr(HRESULT hr, PWSTR errMsg, char *buf, size_t bufSize) {
    if (errMsg) {
        snprintf(buf, bufSize, "hr=0x%08lX msg=%ls", (unsigned long)hr, errMsg);
    } else {
        snprintf(buf, bufSize, "hr=0x%08lX", (unsigned long)hr);
    }
}

/* Reads a WSLC process IO handle to completion into a growable buffer. */
typedef struct IoReadCtx {
    HANDLE handle;
    char *buf;
    size_t len;
    size_t cap;
} IoReadCtx;

static DWORD WINAPI ioReaderThread(LPVOID param) {
    IoReadCtx *ctx = (IoReadCtx *)param;
    if (ctx->handle == NULL || ctx->handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    char tmp[4096];
    DWORD read = 0;
    while (ReadFile(ctx->handle, tmp, sizeof(tmp), &read, NULL) && read > 0) {
        if (ctx->len + read + 1 > ctx->cap) {
            size_t newCap = (ctx->cap == 0 ? 4096 : ctx->cap * 2);
            while (newCap < ctx->len + read + 1) newCap *= 2;
            char *grown = (char *)realloc(ctx->buf, newCap);
            if (!grown) break;
            ctx->buf = grown;
            ctx->cap = newCap;
        }
        memcpy(ctx->buf + ctx->len, tmp, read);
        ctx->len += read;
        ctx->buf[ctx->len] = '\0';
    }
    return 0;
}

/*
 * Extracts the value of a top-level `"key":"value"` string field from a
 * WslcInspectContainer JSON blob. wslcsdk.h documents no schema for this
 * data, so the key name used below ("IPAddress") was confirmed by printing
 * a real inspect payload during development, not assumed -- WSLC's
 * inspect output is docker-inspect-shaped:
 * `"NetworkSettings":{"Networks":{"bridge":{"Gateway":"172.17.0.1",...,
 * "IPAddress":"172.17.0.3",...}}}`. A naive "first dotted-quad in the
 * text" scan would have matched the bridge's Gateway instead of the
 * container's own address, since "Gateway" sorts before "IPAddress" in
 * this payload -- this happened during development and is exactly why
 * this looks up the named field instead.
 */
static int extractJsonStringField(const char *json, const char *key, char *out, size_t outSize) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (!p) {
        return 0;
    }
    p += strlen(pattern);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outSize) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

typedef struct OneShotResult {
    int created, started, waitedOk, exitOk;
    int exitCode;
    char stdoutBuf[512];
    char stderrBuf[512];
} OneShotResult;

/*
 * Creates a container from `image` with networking disabled, runs `argv` as
 * its init process to completion, captures stdout/stderr/exit code, then
 * stops/deletes/releases the container. Used to prove an image handed to
 * WSLC by a path other than pull (tag, import) is actually runnable, not
 * just accepted by the API and immediately discarded.
 */
static void runOneShotAndCleanup(WslcSession session, const char *containerName, const char *image,
                                  const char *const *argvList, size_t argc, OneShotResult *out) {
    ZeroMemory(out, sizeof(*out));

    WslcContainerSettings cs __attribute__((aligned(8)));
    ZeroMemory(&cs, sizeof(cs));
    HRESULT hr = WslcInitContainerSettings(image, &cs);
    if (FAILED(hr)) {
        return;
    }
    (void)WslcSetContainerSettingsName(&cs, containerName);
    (void)WslcSetContainerSettingsNetworkingMode(&cs, WSLC_CONTAINER_NETWORKING_MODE_NONE);

    WslcProcessSettings proc __attribute__((aligned(8)));
    ZeroMemory(&proc, sizeof(proc));
    hr = WslcInitProcessSettings(&proc);
    if (FAILED(hr)) {
        return;
    }
    (void)WslcSetProcessSettingsCmdLine(&proc, argvList, argc);
    (void)WslcSetContainerSettingsInitProcess(&cs, &proc);

    WslcContainer container = NULL;
    PWSTR err = NULL;
    hr = WslcCreateContainer(session, &cs, &container, &err);
    freeSdkString(err);
    err = NULL;
    out->created = SUCCEEDED(hr);
    if (!out->created) {
        return;
    }

    err = NULL;
    hr = WslcStartContainer(container, WSLC_CONTAINER_START_FLAG_ATTACH, &err);
    freeSdkString(err);
    err = NULL;
    out->started = SUCCEEDED(hr);

    if (out->started) {
        WslcProcess initProcess = NULL;
        hr = WslcGetContainerInitProcess(container, &initProcess);
        if (SUCCEEDED(hr) && initProcess) {
            HANDLE hOut = NULL, hErr = NULL, hExit = NULL;
            (void)WslcGetProcessIOHandle(initProcess, WSLC_PROCESS_IO_HANDLE_STDOUT, &hOut);
            (void)WslcGetProcessIOHandle(initProcess, WSLC_PROCESS_IO_HANDLE_STDERR, &hErr);
            (void)WslcGetProcessExitEvent(initProcess, &hExit);

            IoReadCtx outCtx = {hOut, NULL, 0, 0};
            IoReadCtx errCtx = {hErr, NULL, 0, 0};
            HANDLE tOut = CreateThread(NULL, 0, ioReaderThread, &outCtx, 0, NULL);
            HANDLE tErr = CreateThread(NULL, 0, ioReaderThread, &errCtx, 0, NULL);

            DWORD waitRes = hExit ? WaitForSingleObject(hExit, 15000) : WAIT_TIMEOUT;
            if (tOut) {
                WaitForSingleObject(tOut, 3000);
                CloseHandle(tOut);
            }
            if (tErr) {
                WaitForSingleObject(tErr, 3000);
                CloseHandle(tErr);
            }

            out->waitedOk = (waitRes == WAIT_OBJECT_0);
            INT32 exitCode = -1;
            HRESULT exitHr = WslcGetProcessExitCode(initProcess, &exitCode);
            out->exitOk = SUCCEEDED(exitHr);
            out->exitCode = (int)exitCode;
            if (outCtx.buf) {
                snprintf(out->stdoutBuf, sizeof(out->stdoutBuf), "%s", outCtx.buf);
                free(outCtx.buf);
            }
            if (errCtx.buf) {
                snprintf(out->stderrBuf, sizeof(out->stderrBuf), "%s", errCtx.buf);
                free(errCtx.buf);
            }
            (void)WslcReleaseProcess(initProcess);
        }

        err = NULL;
        hr = WslcStopContainer(container, WSLC_SIGNAL_SIGTERM, 5, &err);
        freeSdkString(err);
        err = NULL;
    }

    err = NULL;
    hr = WslcDeleteContainer(container, WSLC_DELETE_CONTAINER_FLAG_FORCE, &err);
    freeSdkString(err);
    err = NULL;
    (void)WslcReleaseContainer(container);
}

typedef struct ExecResult {
    int execOk, waitedOk, exitOk;
    int exitCode;
    char stdoutBuf[512];
    char stderrBuf[512];
} ExecResult;

/*
 * Runs `argvList` as a new exec'd process inside an already-running
 * container and captures stdout/stderr/exit code. Factored out so the
 * same mechanism can be exercised more than once against one container,
 * per docs/validation/work-instruction-policy.md's rule that a recurring
 * operation must prove a second invocation, not just one.
 */
static void execInRunningContainer(WslcContainer container, const char *const *argvList, size_t argc, ExecResult *out) {
    ZeroMemory(out, sizeof(*out));

    WslcProcessSettings proc __attribute__((aligned(8)));
    ZeroMemory(&proc, sizeof(proc));
    HRESULT hr = WslcInitProcessSettings(&proc);
    if (FAILED(hr)) {
        return;
    }
    (void)WslcSetProcessSettingsCmdLine(&proc, argvList, argc);

    WslcProcess execProcess = NULL;
    PWSTR err = NULL;
    hr = WslcCreateContainerProcess(container, &proc, &execProcess, &err);
    freeSdkString(err);
    err = NULL;
    out->execOk = SUCCEEDED(hr);
    if (!out->execOk) {
        return;
    }

    HANDLE hOut = NULL, hErr = NULL, hExit = NULL;
    (void)WslcGetProcessIOHandle(execProcess, WSLC_PROCESS_IO_HANDLE_STDOUT, &hOut);
    (void)WslcGetProcessIOHandle(execProcess, WSLC_PROCESS_IO_HANDLE_STDERR, &hErr);
    (void)WslcGetProcessExitEvent(execProcess, &hExit);

    IoReadCtx outCtx = {hOut, NULL, 0, 0};
    IoReadCtx errCtx = {hErr, NULL, 0, 0};
    HANDLE tOut = CreateThread(NULL, 0, ioReaderThread, &outCtx, 0, NULL);
    HANDLE tErr = CreateThread(NULL, 0, ioReaderThread, &errCtx, 0, NULL);

    DWORD waitRes = hExit ? WaitForSingleObject(hExit, 15000) : WAIT_TIMEOUT;
    if (tOut) {
        WaitForSingleObject(tOut, 3000);
        CloseHandle(tOut);
    }
    if (tErr) {
        WaitForSingleObject(tErr, 3000);
        CloseHandle(tErr);
    }

    out->waitedOk = (waitRes == WAIT_OBJECT_0);
    INT32 exitCode = -1;
    HRESULT exitHr = WslcGetProcessExitCode(execProcess, &exitCode);
    out->exitOk = SUCCEEDED(exitHr);
    out->exitCode = (int)exitCode;
    if (outCtx.buf) {
        snprintf(out->stdoutBuf, sizeof(out->stdoutBuf), "%s", outCtx.buf);
        free(outCtx.buf);
    }
    if (errCtx.buf) {
        snprintf(out->stderrBuf, sizeof(out->stderrBuf), "%s", errCtx.buf);
        free(errCtx.buf);
    }
    (void)WslcReleaseProcess(execProcess);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    HRESULT hr;
    PWSTR err = NULL;
    char detail[1024];

    /* The WSLC SDK is COM/WinRT-backed; every call fails with CO_E_NOTINITIALIZED
     * (0x800401F0) without this on the calling thread. */
    HRESULT comHr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(comHr)) {
        printf("FATAL: CoInitializeEx failed hr=0x%08lX\n", (unsigned long)comHr);
        return 1;
    }

    /* ---------------- 1. missing components + service version ---------------- */
    WslcComponentFlags missing = WSLC_COMPONENT_FLAG_NONE;
    hr = WslcGetMissingComponents(&missing);
    if (SUCCEEDED(hr)) {
        snprintf(detail, sizeof(detail), "missingFlags=0x%08lX (0=nothing missing)", (unsigned long)missing);
        logResult("missing_components", 1, detail);
    } else {
        describeHr(hr, NULL, detail, sizeof(detail));
        logResult("missing_components", 0, detail);
    }

    WslcVersion ver = {0};
    hr = WslcGetVersion(&ver);
    if (SUCCEEDED(hr)) {
        snprintf(detail, sizeof(detail), "sdk_runtime_version=%u.%u.%u", ver.major, ver.minor, ver.revision);
        logResult("service_version", 1, detail);
    } else {
        describeHr(hr, NULL, detail, sizeof(detail));
        logResult("service_version", 0, detail);
    }

    /* ---------------- 2. session create ---------------- */
    WslcSessionSettings sessionSettings __attribute__((aligned(8)));
    ZeroMemory(&sessionSettings, sizeof(sessionSettings));
    hr = WslcInitSessionSettings(kSessionName, kStoragePath, &sessionSettings);
    if (FAILED(hr)) {
        describeHr(hr, NULL, detail, sizeof(detail));
        logResult("session_create", 0, detail);
        printf("FATAL: cannot init session settings, aborting probe\n");
        CoUninitialize();
        return 1;
    }
    (void)WslcSetSessionSettingsTimeout(&sessionSettings, 60000);

    WslcSession session = NULL;
    err = NULL;
    hr = WslcCreateSession(&sessionSettings, &session, &err);
    int sessionOk = SUCCEEDED(hr);
    describeHr(hr, err, detail, sizeof(detail));
    logResult("session_create", sessionOk, detail);
    freeSdkString(err);
    err = NULL;

    if (!sessionOk) {
        printf("FATAL: session create failed, aborting probe\n");
        CoUninitialize();
        return 1;
    }

    /* ---------------- 3. image pull (small known image) ---------------- */
    int pullOk = 0;
    {
        WslcPullImageOptions pullOpts;
        ZeroMemory(&pullOpts, sizeof(pullOpts));
        pullOpts.uri = kPullImage;
        err = NULL;
        hr = WslcPullSessionImage(session, &pullOpts, &err);
        pullOk = SUCCEEDED(hr);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("image_pull", pullOk, detail);
        freeSdkString(err);
        err = NULL;
    }

    if (pullOk) {
        WslcImageInfo *images = NULL;
        uint32_t count = 0;
        hr = WslcListSessionImages(session, &images, &count);
        if (SUCCEEDED(hr)) {
            snprintf(detail, sizeof(detail), "sessionImageCount=%u", count);
            logResult("image_list", 1, detail);
            if (images) CoTaskMemFree(images);
        } else {
            describeHr(hr, NULL, detail, sizeof(detail));
            logResult("image_list", 0, detail);
        }
    } else {
        logSkip("image_list", "pull failed");
    }

    /* ---------------- 4. build handoff, path A: tag the pulled image (build-output naming -> run) ---------------- */
    int tagOk = 0;
    if (pullOk) {
        WslcTagImageOptions tagOpts;
        tagOpts.image = kPullImage;
        tagOpts.repo = kTaggedRepo;
        tagOpts.tag = kTaggedTag;
        err = NULL;
        hr = WslcTagSessionImage(session, &tagOpts, &err);
        tagOk = SUCCEEDED(hr);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("image_tag_handoff", tagOk, detail);
        freeSdkString(err);
        err = NULL;
    } else {
        logSkip("image_tag_handoff", "pull failed");
    }

    /* ---------------- 4b. build handoff, path B: import externally-produced image bytes ---------------- */
    /*
     * The fixture tar is a minimal real Linux rootfs (a single statically
     * linked busybox binary at /bin/busybox, fetched over plain HTTPS from
     * busybox.net independently of any container runtime or CLI under
     * test) so the imported image can actually be created, started, and
     * exec'd from below -- not just accepted and immediately deleted.
     */
    {
        const wchar_t *tarPath = L"C:\\Users\\asopitech\\AppData\\Local\\Temp\\navisoma-wslc-probe\\import-fixture.tar";
        WslcImportImageOptions importOpts;
        ZeroMemory(&importOpts, sizeof(importOpts));
        err = NULL;
        hr = WslcImportSessionImageFromFile(session, kImportRef, tarPath, &importOpts, &err);
        int importOk = SUCCEEDED(hr);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("image_import_handoff", importOk, detail);
        freeSdkString(err);
        err = NULL;

        if (importOk) {
            static const char *importArgv[] = {
                "/bin/busybox", "sh", "-c",
                "echo navisoma-import-stdout-marker; echo navisoma-import-stderr-marker 1>&2; exit 0"};
            OneShotResult res;
            runOneShotAndCleanup(session, kImportContainerName, kImportRef, importArgv,
                                  sizeof(importArgv) / sizeof(importArgv[0]), &res);
            int runOk = res.created && res.started && res.waitedOk && res.exitOk && res.exitCode == 0 &&
                        strstr(res.stdoutBuf, "navisoma-import-stdout-marker") != NULL &&
                        strstr(res.stderrBuf, "navisoma-import-stderr-marker") != NULL;
            snprintf(detail, sizeof(detail),
                     "created=%d started=%d waitedOk=%d exitOk=%d exitCode=%d stdout=[%s] stderr=[%s]", res.created,
                     res.started, res.waitedOk, res.exitOk, res.exitCode, res.stdoutBuf, res.stderrBuf);
            logResult("image_import_run_verify", runOk, detail);

            err = NULL;
            hr = WslcDeleteSessionImage(session, kImportRef, &err);
            freeSdkString(err);
            err = NULL;
        } else {
            logSkip("image_import_run_verify", "import failed");
        }
    }

    /* ---------------- 5. named volume (session VHD volume) ---------------- */
    int volumeOk = 0;
    {
        WslcVhdRequirements vhd;
        ZeroMemory(&vhd, sizeof(vhd));
        vhd.name = kVolumeName;
        vhd.sizeBytes = 64ULL * 1024 * 1024;
        vhd.type = WSLC_VHD_TYPE_DYNAMIC;
        vhd.flags = WSLC_VHD_REQ_FLAG_NONE;
        err = NULL;
        hr = WslcCreateSessionVhdVolume(session, &vhd, &err);
        volumeOk = SUCCEEDED(hr);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("volume_create", volumeOk, detail);
        freeSdkString(err);
        err = NULL;
    }

    /* ---------------- 6. container create + network + port + volume ---------------- */
    WslcContainer container = NULL;
    int containerCreated = 0;
    int containerStarted = 0;
    const char *runImage = tagOk ? kTaggedRef : kPullImage;

    if (pullOk) {
        WslcContainerSettings cs __attribute__((aligned(8)));
        ZeroMemory(&cs, sizeof(cs));
        hr = WslcInitContainerSettings(runImage, &cs);
        if (SUCCEEDED(hr)) {
            (void)WslcSetContainerSettingsName(&cs, kContainerName);
            (void)WslcSetContainerSettingsNetworkingMode(&cs, WSLC_CONTAINER_NETWORKING_MODE_BRIDGED);
            (void)WslcSetContainerSettingsFlags(&cs, WSLC_CONTAINER_FLAG_NONE);

            WslcContainerPortMapping portMap;
            ZeroMemory(&portMap, sizeof(portMap));
            portMap.windowsPort = kHostPort;
            portMap.containerPort = 80;
            portMap.protocol = WSLC_PORT_PROTOCOL_TCP;
            portMap.windowsAddress = NULL;
            (void)WslcSetContainerSettingsPortMappings(&cs, &portMap, 1);

            WslcContainerNamedVolume namedVol;
            namedVol.name = kVolumeName;
            namedVol.containerPath = "/mnt/probe-vol";
            namedVol.readOnly = FALSE;
            (void)WslcSetContainerSettingsNamedVolumes(&cs, &namedVol, volumeOk ? 1 : 0);

            WslcProcessSettings initProc __attribute__((aligned(8)));
            ZeroMemory(&initProc, sizeof(initProc));
            hr = WslcInitProcessSettings(&initProc);
            if (SUCCEEDED(hr)) {
                static const char *initArgv[] = {
                    "/bin/sh", "-c",
                    "echo navisoma-stdout-marker; "
                    "echo navisoma-stderr-marker 1>&2; "
                    "mkdir -p /mnt/probe-vol; "
                    "echo navisoma-volume-marker > /mnt/probe-vol/marker.txt; "
                    "i=0; while [ $i -lt 40 ]; do echo navisoma-port-ok | nc -l -p 80; i=$((i+1)); done"
                };
                (void)WslcSetProcessSettingsCmdLine(&initProc, initArgv, sizeof(initArgv) / sizeof(initArgv[0]));
                (void)WslcSetContainerSettingsInitProcess(&cs, &initProc);
            }

            err = NULL;
            hr = WslcCreateContainer(session, &cs, &container, &err);
            containerCreated = SUCCEEDED(hr);
            describeHr(hr, err, detail, sizeof(detail));
            logResult("container_create", containerCreated, detail);
            freeSdkString(err);
            err = NULL;
        } else {
            describeHr(hr, NULL, detail, sizeof(detail));
            logResult("container_create", 0, detail);
        }
    } else {
        logSkip("container_create", "pull failed");
    }

    if (containerCreated) {
        err = NULL;
        hr = WslcStartContainer(container, WSLC_CONTAINER_START_FLAG_ATTACH, &err);
        containerStarted = SUCCEEDED(hr);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("container_start", containerStarted, detail);
        freeSdkString(err);
        err = NULL;
    } else {
        logSkip("container_start", "create failed");
    }

    WslcProcess initProcess = NULL;
    if (containerStarted) {
        hr = WslcGetContainerInitProcess(container, &initProcess);
        if (FAILED(hr)) {
            describeHr(hr, NULL, detail, sizeof(detail));
            logResult("container_init_process_handle", 0, detail);
            initProcess = NULL;
        } else {
            logResult("container_init_process_handle", 1, "");
        }
    }

    /* ---------------- 6b. network: published TCP port end-to-end ---------------- */
    if (containerStarted) {
        Sleep(2000); /* let the listener come up inside the container */
        WSADATA wsaData;
        int wsaOk = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
        int portOk = 0;
        if (wsaOk) {
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s != INVALID_SOCKET) {
                struct sockaddr_in addr;
                ZeroMemory(&addr, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_port = htons(kHostPort);
                inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
                if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                    char rbuf[128];
                    ZeroMemory(rbuf, sizeof(rbuf));
                    int n = recv(s, rbuf, sizeof(rbuf) - 1, 0);
                    if (n > 0 && strstr(rbuf, "navisoma-port-ok") != NULL) {
                        portOk = 1;
                    }
                    snprintf(detail, sizeof(detail), "connected=1 bytes=%d payload=%.*s", n, n > 0 ? n : 0, rbuf);
                } else {
                    snprintf(detail, sizeof(detail), "connect failed wsaerr=%d", WSAGetLastError());
                }
                closesocket(s);
            } else {
                snprintf(detail, sizeof(detail), "socket() failed wsaerr=%d", WSAGetLastError());
            }
            WSACleanup();
        } else {
            snprintf(detail, sizeof(detail), "WSAStartup failed");
        }
        logResult("published_port_tcp", portOk, detail);
    } else {
        logSkip("published_port_tcp", "container not started");
    }

    /*
     * ---------------- 6c. process stdout/stderr + exit status, invoked
     * twice (exec into running container) ----------------
     * Two independent WslcCreateContainerProcess calls against the same
     * running container, each with its own distinct stdout/stderr markers,
     * so "recurring" is demonstrated as a second real invocation of the
     * mechanism rather than inferred from a single call.
     */
    if (containerStarted) {
        static const char *catArgv[] = {
            "/bin/sh", "-c", "cat /mnt/probe-vol/marker.txt; echo navisoma-exec-stderr-marker 1>&2"};
        ExecResult r1;
        execInRunningContainer(container, catArgv, sizeof(catArgv) / sizeof(catArgv[0]), &r1);
        logResult("process_exec", r1.execOk, "");

        if (r1.execOk) {
            int volumeReadOk = strstr(r1.stdoutBuf, "navisoma-volume-marker") != NULL;
            int stderrOk = strstr(r1.stderrBuf, "navisoma-exec-stderr-marker") != NULL;
            snprintf(detail, sizeof(detail), "waitedOk=%d exitOk=%d exitCode=%d stdout=[%s] stderr=[%s]", r1.waitedOk,
                     r1.exitOk, r1.exitCode, r1.stdoutBuf, r1.stderrBuf);
            logResult("process_stdio_exit_status",
                      (r1.waitedOk && r1.exitOk && r1.exitCode == 0 && stderrOk), detail);
            logResult("named_volume_mount", volumeReadOk, volumeReadOk ? "marker read back through exec" : "marker not observed");

            static const char *secondArgv[] = {
                "/bin/sh", "-c", "echo navisoma-exec2-stdout-marker; echo navisoma-exec2-stderr-marker 1>&2"};
            ExecResult r2;
            execInRunningContainer(container, secondArgv, sizeof(secondArgv) / sizeof(secondArgv[0]), &r2);
            int r2StdoutOk = strstr(r2.stdoutBuf, "navisoma-exec2-stdout-marker") != NULL;
            int r2StderrOk = strstr(r2.stderrBuf, "navisoma-exec2-stderr-marker") != NULL;
            snprintf(detail, sizeof(detail), "execOk=%d waitedOk=%d exitOk=%d exitCode=%d stdout=[%s] stderr=[%s]",
                     r2.execOk, r2.waitedOk, r2.exitOk, r2.exitCode, r2.stdoutBuf, r2.stderrBuf);
            logResult("process_exec_second_invocation",
                      (r2.execOk && r2.waitedOk && r2.exitOk && r2.exitCode == 0 && r2StdoutOk && r2StderrOk), detail);
        } else {
            logSkip("process_stdio_exit_status", "exec failed");
            logSkip("named_volume_mount", "exec failed");
            logSkip("process_exec_second_invocation", "first exec failed");
        }
    } else {
        logSkip("process_exec", "container not started");
        logSkip("process_stdio_exit_status", "container not started");
        logSkip("named_volume_mount", "container not started");
        logSkip("process_exec_second_invocation", "container not started");
    }

    /*
     * ---------------- 6d. network: intra-session service-to-service
     * connectivity ----------------
     * #14 case 1 needs a project's services to reach each other over one
     * named network, not just a published host port. WSLC has no named
     * network object (see the doc's note), so the claim under test is that
     * one BRIDGED-mode session already gives every container in it mutual
     * reachability. Proven here with a second container in the SAME
     * session that c1 reaches over its container IP with no host port
     * involved.
     */
    if (containerStarted) {
        WslcContainerSettings cs2 __attribute__((aligned(8)));
        ZeroMemory(&cs2, sizeof(cs2));
        hr = WslcInitContainerSettings(runImage, &cs2);
        WslcContainer peer = NULL;
        int peerCreated = 0, peerStarted = 0;
        if (SUCCEEDED(hr)) {
            (void)WslcSetContainerSettingsName(&cs2, kPeerContainerName);
            (void)WslcSetContainerSettingsNetworkingMode(&cs2, WSLC_CONTAINER_NETWORKING_MODE_BRIDGED);

            WslcProcessSettings peerInit __attribute__((aligned(8)));
            ZeroMemory(&peerInit, sizeof(peerInit));
            hr = WslcInitProcessSettings(&peerInit);
            if (SUCCEEDED(hr)) {
                char peerCmd[128];
                snprintf(peerCmd, sizeof(peerCmd),
                         "i=0; while [ $i -lt 40 ]; do echo navisoma-peer-ok | nc -l -p %u; i=$((i+1)); done",
                         kPeerPort);
                const char *peerArgv[] = {"/bin/sh", "-c", peerCmd};
                (void)WslcSetProcessSettingsCmdLine(&peerInit, peerArgv, 3);
                (void)WslcSetContainerSettingsInitProcess(&cs2, &peerInit);
            }

            err = NULL;
            hr = WslcCreateContainer(session, &cs2, &peer, &err);
            peerCreated = SUCCEEDED(hr);
            freeSdkString(err);
            err = NULL;
        }

        if (peerCreated) {
            err = NULL;
            hr = WslcStartContainer(peer, WSLC_CONTAINER_START_FLAG_ATTACH, &err);
            peerStarted = SUCCEEDED(hr);
            freeSdkString(err);
            err = NULL;
        }

        char peerIp[32] = {0};
        int gotIp = 0;
        if (peerStarted) {
            Sleep(1500); /* let the peer's listener come up */
            PSTR inspect = NULL;
            hr = WslcInspectContainer(peer, &inspect);
            if (SUCCEEDED(hr) && inspect) {
                gotIp = extractJsonStringField(inspect, "IPAddress", peerIp, sizeof(peerIp));
                CoTaskMemFree(inspect);
            }
        }
        logResult("peer_container_ip_inspect", gotIp, gotIp ? peerIp : "no IPv4 substring found in inspect data");

        int connectOk = 0;
        if (gotIp) {
            WslcProcessSettings clientProc __attribute__((aligned(8)));
            ZeroMemory(&clientProc, sizeof(clientProc));
            hr = WslcInitProcessSettings(&clientProc);
            if (SUCCEEDED(hr)) {
                char cmd[160];
                snprintf(cmd, sizeof(cmd), "nc -w 3 %s %u", peerIp, kPeerPort);
                const char *clientArgv[] = {"/bin/sh", "-c", cmd};
                (void)WslcSetProcessSettingsCmdLine(&clientProc, clientArgv, 3);

                WslcProcess clientExec = NULL;
                err = NULL;
                hr = WslcCreateContainerProcess(container, &clientProc, &clientExec, &err);
                freeSdkString(err);
                err = NULL;
                if (SUCCEEDED(hr)) {
                    HANDLE hOut2 = NULL, hExit2 = NULL;
                    (void)WslcGetProcessIOHandle(clientExec, WSLC_PROCESS_IO_HANDLE_STDOUT, &hOut2);
                    (void)WslcGetProcessExitEvent(clientExec, &hExit2);
                    IoReadCtx outCtx2 = {hOut2, NULL, 0, 0};
                    HANDLE tOut2 = CreateThread(NULL, 0, ioReaderThread, &outCtx2, 0, NULL);
                    DWORD waitRes2 = hExit2 ? WaitForSingleObject(hExit2, 10000) : WAIT_TIMEOUT;
                    if (tOut2) {
                        WaitForSingleObject(tOut2, 3000);
                        CloseHandle(tOut2);
                    }
                    connectOk = (outCtx2.buf && strstr(outCtx2.buf, "navisoma-peer-ok") != NULL);
                    snprintf(detail, sizeof(detail), "peerIp=%s waitRes=%lu stdout=[%s]", peerIp,
                             (unsigned long)waitRes2, outCtx2.buf ? outCtx2.buf : "");
                    if (outCtx2.buf) free(outCtx2.buf);
                    (void)WslcReleaseProcess(clientExec);
                } else {
                    snprintf(detail, sizeof(detail), "exec into c1 to reach peer failed");
                }
            }
        } else {
            snprintf(detail, sizeof(detail), "no peer IP available");
        }
        logResult("intra_session_service_connectivity", connectOk, detail);

        if (peerStarted) {
            err = NULL;
            hr = WslcStopContainer(peer, WSLC_SIGNAL_SIGTERM, 5, &err);
            freeSdkString(err);
            err = NULL;
        }
        if (peerCreated) {
            err = NULL;
            hr = WslcDeleteContainer(peer, WSLC_DELETE_CONTAINER_FLAG_FORCE, &err);
            freeSdkString(err);
            err = NULL;
            (void)WslcReleaseContainer(peer);
        }
    } else {
        logSkip("peer_container_ip_inspect", "container not started");
        logSkip("intra_session_service_connectivity", "container not started");
    }

    if (initProcess) {
        (void)WslcReleaseProcess(initProcess);
    }

    /* ---------------- 7. teardown: container stop/delete/release ---------------- */
    if (containerStarted) {
        err = NULL;
        hr = WslcStopContainer(container, WSLC_SIGNAL_SIGTERM, 10, &err);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("container_stop", SUCCEEDED(hr), detail);
        freeSdkString(err);
        err = NULL;
    } else if (containerCreated) {
        logSkip("container_stop", "never started");
    }

    if (containerCreated) {
        err = NULL;
        hr = WslcDeleteContainer(container, WSLC_DELETE_CONTAINER_FLAG_FORCE, &err);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("container_delete", SUCCEEDED(hr), detail);
        freeSdkString(err);
        err = NULL;

        hr = WslcReleaseContainer(container);
        logResult("container_release", SUCCEEDED(hr), "");

        /*
         * Cleanup postcondition, not just the delete call's own return
         * code: look the container up by name again and require that
         * lookup to now fail. A container handle's own S_OK on delete is
         * not, by itself, proof the container is actually gone.
         */
        WslcContainer reopened = NULL;
        HRESULT reopenHr = WslcOpenContainer(session, kContainerName, &reopened, NULL);
        snprintf(detail, sizeof(detail), "reopen hr=0x%08lX (expected FAILED = truly deleted)", (unsigned long)reopenHr);
        logResult("container_delete_postcondition", FAILED(reopenHr), detail);
        if (SUCCEEDED(reopenHr)) {
            (void)WslcReleaseContainer(reopened);
        }
    }

    /* ---------------- 8. teardown: volume / images ---------------- */
    if (volumeOk) {
        err = NULL;
        hr = WslcDeleteSessionVhdVolume(session, kVolumeName, &err);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("volume_delete", SUCCEEDED(hr), detail);
        freeSdkString(err);
        err = NULL;

        /* Postcondition: deleting the same name again must now fail. */
        err = NULL;
        HRESULT secondDeleteHr = WslcDeleteSessionVhdVolume(session, kVolumeName, &err);
        snprintf(detail, sizeof(detail), "second delete hr=0x%08lX (expected FAILED = truly deleted)",
                 (unsigned long)secondDeleteHr);
        logResult("volume_delete_postcondition", FAILED(secondDeleteHr), detail);
        freeSdkString(err);
        err = NULL;
    }

    if (tagOk) {
        err = NULL;
        hr = WslcDeleteSessionImage(session, kTaggedRef, &err);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("image_tag_delete", SUCCEEDED(hr), detail);
        freeSdkString(err);
        err = NULL;
    }

    if (pullOk) {
        err = NULL;
        hr = WslcDeleteSessionImage(session, kPullImage, &err);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("image_pull_delete", SUCCEEDED(hr), detail);
        freeSdkString(err);
        err = NULL;

        /*
         * Postcondition: re-list the session's images and require the
         * deleted name to actually be gone from it, not just trust the
         * delete call's own return code.
         */
        WslcImageInfo *imagesAfter = NULL;
        uint32_t countAfter = 0;
        HRESULT listHr = WslcListSessionImages(session, &imagesAfter, &countAfter);
        int stillPresent = 0;
        if (SUCCEEDED(listHr) && imagesAfter) {
            for (uint32_t i = 0; i < countAfter; i++) {
                if (strstr(imagesAfter[i].name, "alpine") != NULL) {
                    stillPresent = 1;
                    break;
                }
            }
            CoTaskMemFree(imagesAfter);
        }
        snprintf(detail, sizeof(detail), "listHr=0x%08lX countAfter=%u alpineStillPresent=%d",
                 (unsigned long)listHr, countAfter, stillPresent);
        logResult("image_pull_delete_postcondition", SUCCEEDED(listHr) && !stillPresent, detail);
    }

    /* ---------------- 9. teardown: session terminate/release ---------------- */
    hr = WslcTerminateSession(session);
    logResult("session_terminate", SUCCEEDED(hr), "");

    hr = WslcReleaseSession(session);
    logResult("session_release", SUCCEEDED(hr), "");

    printf("PROBE_DONE failures=%d\n", gFailures);
    CoUninitialize();
    return gFailures == 0 ? 0 : 2;
}
