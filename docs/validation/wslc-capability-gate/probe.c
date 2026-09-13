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
static const unsigned short kHostPort = 18080;

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
            hr = WslcDeleteSessionImage(session, kImportRef, &err);
            freeSdkString(err);
            err = NULL;
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

    /* ---------------- 6c. process stdout/stderr + exit status (exec into running container) ---------------- */
    if (containerStarted) {
        WslcProcessSettings execProc __attribute__((aligned(8)));
        ZeroMemory(&execProc, sizeof(execProc));
        hr = WslcInitProcessSettings(&execProc);
        if (SUCCEEDED(hr)) {
            static const char *catArgv[] = {"/bin/cat", "/mnt/probe-vol/marker.txt"};
            (void)WslcSetProcessSettingsCmdLine(&execProc, catArgv, 2);

            WslcProcess execProcess = NULL;
            err = NULL;
            hr = WslcCreateContainerProcess(container, &execProc, &execProcess, &err);
            int execOk = SUCCEEDED(hr);
            describeHr(hr, err, detail, sizeof(detail));
            logResult("process_exec", execOk, detail);
            freeSdkString(err);
            err = NULL;

            if (execOk) {
                HANDLE hOut = NULL, hErr = NULL, hExit = NULL;
                (void)WslcGetProcessIOHandle(execProcess, WSLC_PROCESS_IO_HANDLE_STDOUT, &hOut);
                (void)WslcGetProcessIOHandle(execProcess, WSLC_PROCESS_IO_HANDLE_STDERR, &hErr);
                (void)WslcGetProcessExitEvent(execProcess, &hExit);

                IoReadCtx outCtx = {hOut, NULL, 0, 0};
                IoReadCtx errCtx = {hErr, NULL, 0, 0};
                HANDLE tOut = CreateThread(NULL, 0, ioReaderThread, &outCtx, 0, NULL);
                HANDLE tErr = CreateThread(NULL, 0, ioReaderThread, &errCtx, 0, NULL);

                DWORD waitRes = hExit ? WaitForSingleObject(hExit, 15000) : WAIT_TIMEOUT;

                if (tOut) { WaitForSingleObject(tOut, 3000); CloseHandle(tOut); }
                if (tErr) { WaitForSingleObject(tErr, 3000); CloseHandle(tErr); }

                INT32 exitCode = -1;
                hr = WslcGetProcessExitCode(execProcess, &exitCode);
                int volumeReadOk = (outCtx.buf && strstr(outCtx.buf, "navisoma-volume-marker") != NULL);
                snprintf(detail, sizeof(detail),
                            "waitRes=%lu exitCodeHr=0x%08lX exitCode=%d stdout=[%s] stderr=[%s]",
                            (unsigned long)waitRes, (unsigned long)hr, (int)exitCode,
                            outCtx.buf ? outCtx.buf : "", errCtx.buf ? errCtx.buf : "");
                logResult("process_stdio_exit_status", (waitRes == WAIT_OBJECT_0 && SUCCEEDED(hr) && exitCode == 0), detail);
                logResult("named_volume_mount", volumeReadOk, volumeReadOk ? "marker read back through exec" : "marker not observed");

                if (outCtx.buf) free(outCtx.buf);
                if (errCtx.buf) free(errCtx.buf);
                (void)WslcReleaseProcess(execProcess);
            } else {
                logSkip("process_stdio_exit_status", "exec failed");
                logSkip("named_volume_mount", "exec failed");
            }
        }
    } else {
        logSkip("process_exec", "container not started");
        logSkip("process_stdio_exit_status", "container not started");
        logSkip("named_volume_mount", "container not started");
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
    }

    /* ---------------- 8. teardown: volume / images ---------------- */
    if (volumeOk) {
        err = NULL;
        hr = WslcDeleteSessionVhdVolume(session, kVolumeName, &err);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("volume_delete", SUCCEEDED(hr), detail);
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
