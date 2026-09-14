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
#include <shellapi.h>
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
#pragma comment(lib, "shell32.lib")

/* ---- fixed, deterministic probe identifiers (see docs/validation/wslc-capability-gate.md) ---- */
static const wchar_t *kSessionName = L"navisoma-wslc-probe";
static const wchar_t *kStoragePath = L"C:\\Users\\asopitech\\AppData\\Local\\Temp\\navisoma-wslc-probe\\storage";
static const char *kPullImage = "docker.io/library/alpine:3.19";
static const char *kTaggedRepo = "navisoma-probe-worker";
static const char *kTaggedTag = "ci";
static const char *kTaggedRef = "navisoma-probe-worker:ci";
/* Raw single-file rootfs import: supplementary plumbing evidence only.
 * NOT used as evidence for the Compose build-handoff claim -- see
 * kLoadedArchiveRef below for that. */
static const char *kRawImportRef = "navisoma-probe-rawimport:test";
static const char *kRawImportContainerName = "navisoma-wslc-probe-rawimport";
static const wchar_t *kRawImportTarPath =
    L"C:\\Users\\asopitech\\AppData\\Local\\Temp\\navisoma-wslc-probe\\import-fixture.tar";
/* Real multi-file docker-save/OCI archive, produced by an independent
 * script (build_test_image.py) from a public registry -- not WSLC, not
 * BuildKit, not a Dockerfile build. Its manifest.json carries this exact
 * repo:tag; the probe never passes this name to the load call itself. */
static const char *kLoadedArchiveRef = "navisoma-loaded-archive:ci";
static const char *kLoadedArchiveContainerName = "navisoma-wslc-probe-archive";
static const wchar_t *kLoadedArchiveTarPath =
    L"C:\\Users\\asopitech\\AppData\\Local\\Temp\\navisoma-wslc-probe\\archive-loader-evidence.tar";
static const char *kVolumeName = "navisoma-wslc-probe-vol";
static const char *kContainerName = "navisoma-wslc-probe-c1";
static const char *kPeerContainerName = "navisoma-wslc-probe-c2";
static const unsigned short kHostPort = 18080;
static const unsigned short kPeerPort = 9000;

/* Second, independent session + container used only to test whether a
 * different session/project can reach c1 -- kept separate from every name
 * above so the two sessions' lifecycles never overlap in name. */
static const wchar_t *kIsoSessionName = L"navisoma-wslc-probe-iso";
static const wchar_t *kIsoStoragePath =
    L"C:\\Users\\asopitech\\AppData\\Local\\Temp\\navisoma-wslc-probe\\iso-storage";
static const char *kIsoContainerName = "navisoma-wslc-probe-iso-c";
static const char *kIsoPeerContainerName = "navisoma-wslc-probe-iso-peer";

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
 * Re-lists a session's images and reports whether any name contains
 * `nameSubstring`. Used as a delete postcondition -- "complete cleanup"
 * means every created resource's absence is actually observed, not just
 * that its own delete call returned S_OK.
 */
static int imageNameStillPresent(WslcSession session, const char *nameSubstring, HRESULT *outListHr,
                                  uint32_t *outCount) {
    WslcImageInfo *images = NULL;
    uint32_t count = 0;
    HRESULT listHr = WslcListSessionImages(session, &images, &count);
    int present = 0;
    if (SUCCEEDED(listHr) && images) {
        for (uint32_t i = 0; i < count; i++) {
            if (strstr(images[i].name, nameSubstring) != NULL) {
                present = 1;
                break;
            }
        }
        CoTaskMemFree(images);
    }
    if (outListHr) *outListHr = listHr;
    if (outCount) *outCount = count;
    return present;
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
    int deleteVerifiedGone;
    char stdoutBuf[512];
    char stderrBuf[512];
} OneShotResult;

/*
 * Creates a container from `image` with `networkingMode`, runs `argv` as
 * its init process to completion, captures stdout/stderr/exit code, then
 * stops/deletes/releases the container and verifies -- by reopening it by
 * name, not by trusting the delete call's own return code -- that it is
 * actually gone. Used both to prove an image handed to WSLC by a path
 * other than pull (tag, import, load) is actually runnable, and as the
 * vehicle for the cross-session isolation probe below.
 */
static void runOneShotAndCleanup(WslcSession session, const char *containerName, const char *image,
                                  WslcContainerNetworkingMode networkingMode, const char *const *argvList,
                                  size_t argc, OneShotResult *out) {
    ZeroMemory(out, sizeof(*out));

    WslcContainerSettings cs __attribute__((aligned(8)));
    ZeroMemory(&cs, sizeof(cs));
    HRESULT hr = WslcInitContainerSettings(image, &cs);
    if (FAILED(hr)) {
        return;
    }
    (void)WslcSetContainerSettingsName(&cs, containerName);
    (void)WslcSetContainerSettingsNetworkingMode(&cs, networkingMode);

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

    WslcContainer reopened = NULL;
    HRESULT reopenHr = WslcOpenContainer(session, containerName, &reopened, NULL);
    out->deleteVerifiedGone = FAILED(reopenHr);
    if (SUCCEEDED(reopenHr)) {
        (void)WslcReleaseContainer(reopened);
    }
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

/*
 * `storagePath` in WslcInitSessionSettings is caller-supplied, so it is
 * caller-owned storage by construction -- the SDK does not delete it on
 * WslcTerminateSession/WslcReleaseSession (observed directly: the
 * directory was still present after both runs of an earlier version of
 * this probe). This recursively deletes it and then asserts non-existence
 * via GetFileAttributesW, rather than trusting the delete call.
 */
static int deleteStoragePathAndVerifyGone(const wchar_t *path, char *detailOut, size_t detailOutSize) {
    wchar_t buf[512];
    size_t len = wcslen(path);
    if (len + 2 >= sizeof(buf) / sizeof(buf[0])) {
        snprintf(detailOut, detailOutSize, "path too long");
        return 0;
    }
    memcpy(buf, path, (len + 1) * sizeof(wchar_t));
    buf[len + 1] = L'\0'; /* SHFileOperationW's pFrom must be double-null-terminated */

    SHFILEOPSTRUCTW op;
    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = buf;
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION | FOF_NOERRORUI;
    int opRc = SHFileOperationW(&op);

    DWORD attrs = GetFileAttributesW(path);
    int gone = (attrs == INVALID_FILE_ATTRIBUTES);
    snprintf(detailOut, detailOutSize, "shFileOpRc=%d attrsAfter=0x%08lX (INVALID_FILE_ATTRIBUTES=gone)", opRc,
             (unsigned long)attrs);
    return gone;
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

    /*
     * ---------------- 4b. SUPPLEMENTARY, not build-handoff evidence: raw
     * single-file rootfs import ----------------
     * WslcImportSessionImageFromFile's "docker import"-shaped semantics
     * accept any tar as a flat single-layer rootfs with no embedded name
     * or manifest. That is a real, distinct WSLC capability worth
     * recording, but it is not what a Compose `build:` handoff produces
     * (a multi-layer, manifest-carrying, self-naming OCI/docker-save
     * archive) -- see 4c below for that evidence instead.
     */
    {
        WslcImportImageOptions importOpts;
        ZeroMemory(&importOpts, sizeof(importOpts));
        err = NULL;
        hr = WslcImportSessionImageFromFile(session, kRawImportRef, kRawImportTarPath, &importOpts, &err);
        int importOk = SUCCEEDED(hr);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("supplementary_raw_rootfs_import", importOk, detail);
        freeSdkString(err);
        err = NULL;

        if (importOk) {
            static const char *importArgv[] = {
                "/bin/busybox", "sh", "-c",
                "echo navisoma-import-stdout-marker; echo navisoma-import-stderr-marker 1>&2; exit 0"};
            OneShotResult res;
            runOneShotAndCleanup(session, kRawImportContainerName, kRawImportRef, WSLC_CONTAINER_NETWORKING_MODE_NONE,
                                  importArgv, sizeof(importArgv) / sizeof(importArgv[0]), &res);
            int runOk = res.created && res.started && res.waitedOk && res.exitOk && res.exitCode == 0 &&
                        strstr(res.stdoutBuf, "navisoma-import-stdout-marker") != NULL &&
                        strstr(res.stderrBuf, "navisoma-import-stderr-marker") != NULL;
            snprintf(detail, sizeof(detail),
                     "created=%d started=%d waitedOk=%d exitOk=%d exitCode=%d stdout=[%s] stderr=[%s]", res.created,
                     res.started, res.waitedOk, res.exitOk, res.exitCode, res.stdoutBuf, res.stderrBuf);
            logResult("supplementary_raw_rootfs_run_verify", runOk, detail);
            logResult("supplementary_raw_rootfs_container_delete_postcondition", res.deleteVerifiedGone, "");

            err = NULL;
            hr = WslcDeleteSessionImage(session, kRawImportRef, &err);
            describeHr(hr, err, detail, sizeof(detail));
            logResult("supplementary_raw_rootfs_image_delete", SUCCEEDED(hr), detail);
            freeSdkString(err);
            err = NULL;

            HRESULT rawListHr;
            uint32_t rawCountAfter;
            int rawStillPresent = imageNameStillPresent(session, "navisoma-probe-rawimport", &rawListHr, &rawCountAfter);
            snprintf(detail, sizeof(detail), "listHr=0x%08lX stillPresent=%d", (unsigned long)rawListHr, rawStillPresent);
            logResult("supplementary_raw_rootfs_image_delete_postcondition", SUCCEEDED(rawListHr) && !rawStillPresent,
                      detail);
        } else {
            logSkip("supplementary_raw_rootfs_run_verify", "import failed");
            logSkip("supplementary_raw_rootfs_container_delete_postcondition", "import failed");
            logSkip("supplementary_raw_rootfs_image_delete", "import failed");
            logSkip("supplementary_raw_rootfs_image_delete_postcondition", "import failed");
        }
    }

    /*
     * ---------------- 4c. build.imagestore.import: load a real,
     * independently produced, digest-verified multi-layer OCI/docker-save
     * archive ----------------
     * archive-loader-evidence.tar was produced by build_test_image.py, a
     * standalone script that fetches an already-published image from a
     * public registry over plain HTTPS and verifies every blob it uses
     * against its declared content digest before re-packaging it. This is
     * NOT a Dockerfile/BuildKit build -- no Dockerfile was compiled, only
     * an existing image's bytes were fetched, verified, and re-tagged --
     * and is not described as one anywhere in this probe or its docs. It
     * is real evidence for the *consumption* half of the Compose
     * build-handoff contract: WSLC's C API only ever sees bytes in this
     * shape regardless of what produced them, and this proves it can load
     * and run a genuine multi-layer archive from its own embedded
     * identity. Its manifest.json carries the repo:tag `kLoadedArchiveRef`
     * itself; WslcLoadSessionImageFromFile takes no separate name
     * parameter, so the only way for that name to reach
     * WslcInitContainerSettings below is for the archive's own embedded
     * identity to have round-tripped through the load call correctly.
     */
    {
        WslcLoadImageOptions loadOpts;
        ZeroMemory(&loadOpts, sizeof(loadOpts));
        err = NULL;
        hr = WslcLoadSessionImageFromFile(session, kLoadedArchiveTarPath, &loadOpts, &err);
        int loadOk = SUCCEEDED(hr);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("archive_load_handoff", loadOk, detail);
        freeSdkString(err);
        err = NULL;

        int nameObserved = 0;
        if (loadOk) {
            WslcImageInfo *images = NULL;
            uint32_t count = 0;
            HRESULT listHr = WslcListSessionImages(session, &images, &count);
            if (SUCCEEDED(listHr) && images) {
                for (uint32_t i = 0; i < count; i++) {
                    if (strstr(images[i].name, "navisoma-loaded-archive") != NULL) {
                        nameObserved = 1;
                        break;
                    }
                }
                CoTaskMemFree(images);
            }
            snprintf(detail, sizeof(detail), "listHr=0x%08lX count=%u nameObserved=%d", (unsigned long)listHr, count,
                     nameObserved);
            logResult("archive_load_exact_reference_observed", nameObserved, detail);
        } else {
            logSkip("archive_load_exact_reference_observed", "load failed");
        }

        if (loadOk) {
            static const char *archiveArgv[] = {
                "/bin/sh", "-c",
                "echo navisoma-archive-stdout-marker; echo navisoma-archive-stderr-marker 1>&2; exit 0"};
            OneShotResult res;
            runOneShotAndCleanup(session, kLoadedArchiveContainerName, kLoadedArchiveRef,
                                  WSLC_CONTAINER_NETWORKING_MODE_NONE, archiveArgv,
                                  sizeof(archiveArgv) / sizeof(archiveArgv[0]), &res);
            int runOk = res.created && res.started && res.waitedOk && res.exitOk && res.exitCode == 0 &&
                        strstr(res.stdoutBuf, "navisoma-archive-stdout-marker") != NULL &&
                        strstr(res.stderrBuf, "navisoma-archive-stderr-marker") != NULL;
            snprintf(detail, sizeof(detail),
                     "created=%d started=%d waitedOk=%d exitOk=%d exitCode=%d stdout=[%s] stderr=[%s]", res.created,
                     res.started, res.waitedOk, res.exitOk, res.exitCode, res.stdoutBuf, res.stderrBuf);
            logResult("archive_load_run_verify", runOk, detail);
            logResult("archive_load_container_delete_postcondition", res.deleteVerifiedGone, "");

            err = NULL;
            hr = WslcDeleteSessionImage(session, kLoadedArchiveRef, &err);
            int deleteOk = SUCCEEDED(hr);
            describeHr(hr, err, detail, sizeof(detail));
            logResult("archive_load_image_delete", deleteOk, detail);
            freeSdkString(err);
            err = NULL;

            WslcImageInfo *imagesAfter = NULL;
            uint32_t countAfter = 0;
            HRESULT listHr2 = WslcListSessionImages(session, &imagesAfter, &countAfter);
            int stillPresent = 0;
            if (SUCCEEDED(listHr2) && imagesAfter) {
                for (uint32_t i = 0; i < countAfter; i++) {
                    if (strstr(imagesAfter[i].name, "navisoma-loaded-archive") != NULL) {
                        stillPresent = 1;
                        break;
                    }
                }
                CoTaskMemFree(imagesAfter);
            }
            snprintf(detail, sizeof(detail), "listHr=0x%08lX stillPresent=%d", (unsigned long)listHr2, stillPresent);
            logResult("archive_load_image_delete_postcondition", SUCCEEDED(listHr2) && !stillPresent, detail);
        } else {
            logSkip("archive_load_run_verify", "load failed");
            logSkip("archive_load_container_delete_postcondition", "load failed");
            logSkip("archive_load_image_delete", "load failed");
            logSkip("archive_load_image_delete_postcondition", "load failed");
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

    /* c1's own bridge IP, needed by the cross-session isolation probe (6f) below. */
    char c1Ip[32] = {0};
    int c1GotIp = 0;
    if (containerStarted) {
        Sleep(500);
        PSTR inspect1 = NULL;
        hr = WslcInspectContainer(container, &inspect1);
        if (SUCCEEDED(hr) && inspect1) {
            c1GotIp = extractJsonStringField(inspect1, "IPAddress", c1Ip, sizeof(c1Ip));
            CoTaskMemFree(inspect1);
        }
        logResult("c1_container_ip_inspect", c1GotIp, c1GotIp ? c1Ip : "no IPAddress field found");
    } else {
        logSkip("c1_container_ip_inspect", "container not started");
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
            /*
             * All three -- stdout marker, stderr marker, exit code -- are
             * required together for this one check, per the policy's rule
             * that a process-I/O claim must assert both streams and the
             * exit status jointly, not stdout-only with stderr checked
             * elsewhere (or not at all).
             */
            int volumeReadOk = strstr(r1.stdoutBuf, "navisoma-volume-marker") != NULL;
            int stderrOk = strstr(r1.stderrBuf, "navisoma-exec-stderr-marker") != NULL;
            snprintf(detail, sizeof(detail), "waitedOk=%d exitOk=%d exitCode=%d stdout=[%s] stderr=[%s]", r1.waitedOk,
                     r1.exitOk, r1.exitCode, r1.stdoutBuf, r1.stderrBuf);
            logResult("process_stdio_exit_status",
                      (r1.waitedOk && r1.exitOk && r1.exitCode == 0 && volumeReadOk && stderrOk), detail);

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
            logSkip("process_exec_second_invocation", "first exec failed");
        }
    } else {
        logSkip("process_exec", "container not started");
        logSkip("process_stdio_exit_status", "container not started");
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

            WslcContainer peerReopened = NULL;
            HRESULT peerReopenHr = WslcOpenContainer(session, kPeerContainerName, &peerReopened, NULL);
            snprintf(detail, sizeof(detail), "reopen hr=0x%08lX (expected FAILED = truly deleted)",
                     (unsigned long)peerReopenHr);
            logResult("peer_container_delete_postcondition", FAILED(peerReopenHr), detail);
            if (SUCCEEDED(peerReopenHr)) {
                (void)WslcReleaseContainer(peerReopened);
            }
        }
    } else {
        logSkip("peer_container_ip_inspect", "container not started");
        logSkip("intra_session_service_connectivity", "container not started");
        logSkip("peer_container_delete_postcondition", "container not started");
    }

    /*
     * ---------------- 6f. network: cross-session isolation, with a
     * positive control and both directions ----------------
     * The claim under test in 6d was intra-project connectivity. This is
     * the separate claim: a container in a DIFFERENT, independently
     * created session must NOT be able to reach c1, AND c1 must not be
     * able to reach it back. Per docs/validation/work-instruction-policy.md's
     * rule on negative claims, absence of a marker is not by itself
     * evidence of isolation -- it could equally be a broken client, a
     * dead target, or an ambiguous overlapping address. This block
     * therefore also runs a positive control (the same client mechanism
     * reaching a definitely-live target in the SAME session, which must
     * succeed) and an explicit identity check (c1's and the iso peer's IP
     * strings must differ), and every negative predicate below requires
     * the exec to have actually completed (waitedOk && exitOk) in addition
     * to the target marker being absent -- not marker-absence alone.
     */
    if (containerStarted && c1GotIp) {
        WslcSessionSettings isoSettings __attribute__((aligned(8)));
        ZeroMemory(&isoSettings, sizeof(isoSettings));
        HRESULT isoInitHr = WslcInitSessionSettings(kIsoSessionName, kIsoStoragePath, &isoSettings);
        WslcSession isoSession = NULL;
        int isoSessionOk = 0;
        if (SUCCEEDED(isoInitHr)) {
            (void)WslcSetSessionSettingsTimeout(&isoSettings, 60000);
            err = NULL;
            HRESULT isoSessionHr = WslcCreateSession(&isoSettings, &isoSession, &err);
            isoSessionOk = SUCCEEDED(isoSessionHr);
            describeHr(isoSessionHr, err, detail, sizeof(detail));
            logResult("iso_session_create", isoSessionOk, detail);
            freeSdkString(err);
            err = NULL;
        } else {
            describeHr(isoInitHr, NULL, detail, sizeof(detail));
            logResult("iso_session_create", 0, detail);
        }

        int isoImportOk = 0;
        if (isoSessionOk) {
            WslcImportImageOptions isoImportOpts;
            ZeroMemory(&isoImportOpts, sizeof(isoImportOpts));
            err = NULL;
            HRESULT isoImportHr =
                WslcImportSessionImageFromFile(isoSession, kRawImportRef, kRawImportTarPath, &isoImportOpts, &err);
            isoImportOk = SUCCEEDED(isoImportHr);
            describeHr(isoImportHr, err, detail, sizeof(detail));
            logResult("iso_session_image_import", isoImportOk, detail);
            freeSdkString(err);
            err = NULL;
        } else {
            logSkip("iso_session_image_import", "iso session create failed");
        }

        /*
         * Each session's bridge allocates addresses independently starting
         * from the same base (observed directly: a first container in a
         * freshly created session gets the same address, e.g. 172.17.0.2,
         * regardless of what the main session is already using). Left
         * alone, this collides with c1's address and makes the isolation
         * checks below ambiguous -- a "connection failed" to c1's address
         * could equally mean "nothing local is listening at my own
         * same-numbered address" rather than "blocked from reaching the
         * other session". A one-shot create+delete of a throwaway
         * container does NOT work around this: the freed address was
         * observed being handed straight back out to the very next
         * container (deleting it returns the address to the pool
         * immediately). Instead this container is kept RUNNING -- it
         * physically occupies its address -- for as long as the isolation
         * checks below need isoPeer to have a different one; the
         * `cross_session_ip_addresses_distinct` check still verifies this
         * actually worked rather than assuming it did.
         */
        WslcContainer isoBump = NULL;
        int isoBumpCreated = 0, isoBumpStarted = 0;
        if (isoImportOk) {
            WslcContainerSettings isoBumpCs __attribute__((aligned(8)));
            ZeroMemory(&isoBumpCs, sizeof(isoBumpCs));
            HRESULT ibcHr = WslcInitContainerSettings(kRawImportRef, &isoBumpCs);
            if (SUCCEEDED(ibcHr)) {
                (void)WslcSetContainerSettingsName(&isoBumpCs, "navisoma-wslc-probe-iso-addrbump");
                (void)WslcSetContainerSettingsNetworkingMode(&isoBumpCs, WSLC_CONTAINER_NETWORKING_MODE_BRIDGED);

                WslcProcessSettings isoBumpInit __attribute__((aligned(8)));
                ZeroMemory(&isoBumpInit, sizeof(isoBumpInit));
                HRESULT ibiHr = WslcInitProcessSettings(&isoBumpInit);
                if (SUCCEEDED(ibiHr)) {
                    static const char *isoBumpArgv[] = {"/bin/busybox", "sh", "-c",
                                                         "i=0; while [ $i -lt 60 ]; do /bin/busybox sleep 1; i=$((i+1)); done"};
                    (void)WslcSetProcessSettingsCmdLine(&isoBumpInit, isoBumpArgv,
                                                         sizeof(isoBumpArgv) / sizeof(isoBumpArgv[0]));
                    (void)WslcSetContainerSettingsInitProcess(&isoBumpCs, &isoBumpInit);
                }

                err = NULL;
                HRESULT ibCreateHr = WslcCreateContainer(isoSession, &isoBumpCs, &isoBump, &err);
                isoBumpCreated = SUCCEEDED(ibCreateHr);
                freeSdkString(err);
                err = NULL;
            }
            if (isoBumpCreated) {
                err = NULL;
                HRESULT ibStartHr = WslcStartContainer(isoBump, WSLC_CONTAINER_START_FLAG_ATTACH, &err);
                isoBumpStarted = SUCCEEDED(ibStartHr);
                freeSdkString(err);
                err = NULL;
                if (isoBumpStarted) {
                    Sleep(500);
                }
            }
            char isoBumpIp[32] = {0};
            int isoBumpGotIp = 0;
            if (isoBumpStarted) {
                PSTR isoBumpInspect = NULL;
                HRESULT ibInspectHr = WslcInspectContainer(isoBump, &isoBumpInspect);
                if (SUCCEEDED(ibInspectHr) && isoBumpInspect) {
                    isoBumpGotIp = extractJsonStringField(isoBumpInspect, "IPAddress", isoBumpIp, sizeof(isoBumpIp));
                    CoTaskMemFree(isoBumpInspect);
                }
            }
            snprintf(detail, sizeof(detail), "bumpIp=%s", isoBumpGotIp ? isoBumpIp : "?");
            logResult("iso_session_address_allocator_bump", isoBumpCreated && isoBumpStarted && isoBumpGotIp, detail);
        } else {
            logSkip("iso_session_address_allocator_bump", "iso session/image setup failed");
        }

        /* Long-running listener INSIDE the iso session -- both the positive
         * control (from within the same session) and the main->iso
         * direction connect to this, not to the iso session's one-shot
         * testers, which don't stay up long enough to be a target. */
        WslcContainer isoPeer = NULL;
        int isoPeerCreated = 0, isoPeerStarted = 0;
        char isoPeerIp[32] = {0};
        int isoPeerGotIp = 0;
        if (isoImportOk) {
            WslcContainerSettings isoPeerCs __attribute__((aligned(8)));
            ZeroMemory(&isoPeerCs, sizeof(isoPeerCs));
            HRESULT ipcHr = WslcInitContainerSettings(kRawImportRef, &isoPeerCs);
            if (SUCCEEDED(ipcHr)) {
                (void)WslcSetContainerSettingsName(&isoPeerCs, kIsoPeerContainerName);
                (void)WslcSetContainerSettingsNetworkingMode(&isoPeerCs, WSLC_CONTAINER_NETWORKING_MODE_BRIDGED);

                WslcProcessSettings isoPeerInit __attribute__((aligned(8)));
                ZeroMemory(&isoPeerInit, sizeof(isoPeerInit));
                HRESULT ipiHr = WslcInitProcessSettings(&isoPeerInit);
                if (SUCCEEDED(ipiHr)) {
                    char isoPeerCmd[160];
                    snprintf(isoPeerCmd, sizeof(isoPeerCmd),
                             "i=0; while [ $i -lt 40 ]; do echo navisoma-iso-peer-ok | /bin/busybox nc -l -p %u; "
                             "i=$((i+1)); done",
                             kPeerPort);
                    const char *isoPeerArgv[] = {"/bin/busybox", "sh", "-c", isoPeerCmd};
                    (void)WslcSetProcessSettingsCmdLine(&isoPeerInit, isoPeerArgv,
                                                         sizeof(isoPeerArgv) / sizeof(isoPeerArgv[0]));
                    (void)WslcSetContainerSettingsInitProcess(&isoPeerCs, &isoPeerInit);
                }

                err = NULL;
                HRESULT ipCreateHr = WslcCreateContainer(isoSession, &isoPeerCs, &isoPeer, &err);
                isoPeerCreated = SUCCEEDED(ipCreateHr);
                freeSdkString(err);
                err = NULL;
            }
            logResult("iso_peer_container_create", isoPeerCreated, "");

            if (isoPeerCreated) {
                err = NULL;
                HRESULT ipStartHr = WslcStartContainer(isoPeer, WSLC_CONTAINER_START_FLAG_ATTACH, &err);
                isoPeerStarted = SUCCEEDED(ipStartHr);
                freeSdkString(err);
                err = NULL;
            }
            logResult("iso_peer_container_start", isoPeerStarted, "");

            if (isoPeerStarted) {
                Sleep(1500);
                PSTR isoPeerInspect = NULL;
                HRESULT ipInspectHr = WslcInspectContainer(isoPeer, &isoPeerInspect);
                if (SUCCEEDED(ipInspectHr) && isoPeerInspect) {
                    isoPeerGotIp = extractJsonStringField(isoPeerInspect, "IPAddress", isoPeerIp, sizeof(isoPeerIp));
                    CoTaskMemFree(isoPeerInspect);
                }
            }
            logResult("iso_peer_container_ip_inspect", isoPeerGotIp, isoPeerGotIp ? isoPeerIp : "no IPAddress field found");
        } else {
            logSkip("iso_peer_container_create", "iso session/image setup failed");
            logSkip("iso_peer_container_start", "iso session/image setup failed");
            logSkip("iso_peer_container_ip_inspect", "iso session/image setup failed");
        }

        /* Identity check: rule out the trivial case where both sessions'
         * bridges happened to hand out the exact same address string,
         * which would make either direction's result ambiguous. */
        if (c1GotIp && isoPeerGotIp) {
            int distinct = strcmp(c1Ip, isoPeerIp) != 0;
            snprintf(detail, sizeof(detail), "c1Ip=%s isoPeerIp=%s", c1Ip, isoPeerIp);
            logResult("cross_session_ip_addresses_distinct", distinct, detail);
        } else {
            logSkip("cross_session_ip_addresses_distinct", "missing one or both IPs");
        }

        /* Positive control: the exact same client mechanism (imported
         * busybox image, one-shot container, `nc -w 3 <ip> <port>`) used
         * for the negative checks below, but pointed at a target in the
         * SAME session that is definitely alive. This must succeed, or
         * the negative results below would be uninterpretable -- a silent
         * client/tooling failure would look identical to isolation. */
        int positiveControlOk = 0;
        if (isoImportOk && isoPeerGotIp) {
            char posCmd[160];
            snprintf(posCmd, sizeof(posCmd), "/bin/busybox nc -w 3 %s %u", isoPeerIp, kPeerPort);
            const char *posArgv[] = {"/bin/busybox", "sh", "-c", posCmd};
            OneShotResult posRes;
            runOneShotAndCleanup(isoSession, "navisoma-wslc-probe-iso-posctrl", kRawImportRef,
                                  WSLC_CONTAINER_NETWORKING_MODE_BRIDGED, posArgv,
                                  sizeof(posArgv) / sizeof(posArgv[0]), &posRes);
            int gotMarker = strstr(posRes.stdoutBuf, "navisoma-iso-peer-ok") != NULL;
            positiveControlOk = posRes.created && posRes.started && posRes.waitedOk && posRes.exitOk && gotMarker;
            snprintf(detail, sizeof(detail),
                     "target=%s:%u created=%d started=%d waitedOk=%d exitOk=%d exitCode=%d stdout=[%s]", isoPeerIp,
                     kPeerPort, posRes.created, posRes.started, posRes.waitedOk, posRes.exitOk, posRes.exitCode,
                     posRes.stdoutBuf);
            logResult("cross_session_positive_control", positiveControlOk, detail);
        } else {
            logSkip("cross_session_positive_control", "iso session/peer setup failed");
        }

        /* Direction 1: iso session -> main session (c1). Same client
         * mechanism just proven capable of succeeding above; this time
         * pointed at c1's real bridge IP in the other session. */
        if (positiveControlOk) {
            char isoCmd[160];
            snprintf(isoCmd, sizeof(isoCmd), "/bin/busybox nc -w 3 %s 80", c1Ip);
            const char *isoArgv[] = {"/bin/busybox", "sh", "-c", isoCmd};
            OneShotResult isoRes;
            runOneShotAndCleanup(isoSession, kIsoContainerName, kRawImportRef, WSLC_CONTAINER_NETWORKING_MODE_BRIDGED,
                                  isoArgv, sizeof(isoArgv) / sizeof(isoArgv[0]), &isoRes);
            int reachedC1 = strstr(isoRes.stdoutBuf, "navisoma-port-ok") != NULL;
            /* Negative result requires the exec to have actually completed
             * (not timed out waiting on our own exit-event wait, and the
             * exit code to have been retrievable) in addition to the
             * marker being absent -- marker-absence alone is not enough. */
            int completed = isoRes.created && isoRes.started && isoRes.waitedOk && isoRes.exitOk;
            snprintf(detail, sizeof(detail),
                     "target=%s:80 completed=%d exitCode=%d stdout=[%s] (expected: completed=1, no navisoma-port-ok)",
                     c1Ip, completed, isoRes.exitCode, isoRes.stdoutBuf);
            if (completed) {
                logResult("cross_session_isolation_iso_to_main", !reachedC1, detail);
            } else {
                logSkip("cross_session_isolation_iso_to_main", detail);
            }
            logResult("iso_to_main_container_delete_postcondition", isoRes.deleteVerifiedGone, "");
        } else {
            logSkip("cross_session_isolation_iso_to_main", "positive control failed -- toolchain unproven");
            logSkip("iso_to_main_container_delete_postcondition", "positive control failed");
        }

        /* Direction 2: main session (c1) -> iso session (isoPeer). c1 is
         * already running long-lived, so this is a plain exec on it. */
        if (isoPeerGotIp) {
            char mainToIsoCmd[160];
            snprintf(mainToIsoCmd, sizeof(mainToIsoCmd), "nc -w 3 %s %u", isoPeerIp, kPeerPort);
            const char *mainToIsoArgv[] = {"/bin/sh", "-c", mainToIsoCmd};
            ExecResult mainToIsoRes;
            execInRunningContainer(container, mainToIsoArgv, sizeof(mainToIsoArgv) / sizeof(mainToIsoArgv[0]),
                                    &mainToIsoRes);
            int reachedIsoPeer = strstr(mainToIsoRes.stdoutBuf, "navisoma-iso-peer-ok") != NULL;
            int mainToIsoCompleted = mainToIsoRes.execOk && mainToIsoRes.waitedOk && mainToIsoRes.exitOk;
            snprintf(detail, sizeof(detail),
                     "target=%s:%u completed=%d exitCode=%d stdout=[%s] (expected: completed=1, no navisoma-iso-peer-ok)",
                     isoPeerIp, kPeerPort, mainToIsoCompleted, mainToIsoRes.exitCode, mainToIsoRes.stdoutBuf);
            if (mainToIsoCompleted) {
                logResult("cross_session_isolation_main_to_iso", !reachedIsoPeer, detail);
            } else {
                logSkip("cross_session_isolation_main_to_iso", detail);
            }
        } else {
            logSkip("cross_session_isolation_main_to_iso", "no iso peer IP available");
        }

        if (isoPeerStarted) {
            err = NULL;
            HRESULT ipStopHr = WslcStopContainer(isoPeer, WSLC_SIGNAL_SIGTERM, 5, &err);
            (void)ipStopHr;
            freeSdkString(err);
            err = NULL;
        }
        if (isoPeerCreated) {
            err = NULL;
            HRESULT ipDelHr = WslcDeleteContainer(isoPeer, WSLC_DELETE_CONTAINER_FLAG_FORCE, &err);
            (void)ipDelHr;
            freeSdkString(err);
            err = NULL;
            (void)WslcReleaseContainer(isoPeer);

            WslcContainer isoPeerReopened = NULL;
            HRESULT ipReopenHr = WslcOpenContainer(isoSession, kIsoPeerContainerName, &isoPeerReopened, NULL);
            logResult("iso_peer_container_delete_postcondition", FAILED(ipReopenHr), "");
            if (SUCCEEDED(ipReopenHr)) {
                (void)WslcReleaseContainer(isoPeerReopened);
            }
        }

        if (isoBumpStarted) {
            err = NULL;
            HRESULT ibStopHr = WslcStopContainer(isoBump, WSLC_SIGNAL_SIGTERM, 5, &err);
            (void)ibStopHr;
            freeSdkString(err);
            err = NULL;
        }
        if (isoBumpCreated) {
            err = NULL;
            HRESULT ibDelHr = WslcDeleteContainer(isoBump, WSLC_DELETE_CONTAINER_FLAG_FORCE, &err);
            (void)ibDelHr;
            freeSdkString(err);
            err = NULL;
            (void)WslcReleaseContainer(isoBump);

            WslcContainer isoBumpReopened = NULL;
            HRESULT ibReopenHr = WslcOpenContainer(isoSession, "navisoma-wslc-probe-iso-addrbump", &isoBumpReopened, NULL);
            logResult("iso_session_address_allocator_bump_delete_postcondition", FAILED(ibReopenHr), "");
            if (SUCCEEDED(ibReopenHr)) {
                (void)WslcReleaseContainer(isoBumpReopened);
            }
        } else {
            logSkip("iso_session_address_allocator_bump_delete_postcondition", "bump container create failed");
        }

        if (isoImportOk) {
            err = NULL;
            HRESULT isoImgDelHr = WslcDeleteSessionImage(isoSession, kRawImportRef, &err);
            freeSdkString(err);
            err = NULL;
            logResult("iso_session_image_delete", SUCCEEDED(isoImgDelHr), "");

            HRESULT isoImgListHr;
            uint32_t isoImgCountAfter;
            int isoImgStillPresent =
                imageNameStillPresent(isoSession, "navisoma-probe-rawimport", &isoImgListHr, &isoImgCountAfter);
            snprintf(detail, sizeof(detail), "listHr=0x%08lX stillPresent=%d", (unsigned long)isoImgListHr,
                     isoImgStillPresent);
            logResult("iso_session_image_delete_postcondition", SUCCEEDED(isoImgListHr) && !isoImgStillPresent, detail);
        } else {
            logSkip("iso_session_image_delete", "iso session/image setup failed");
            logSkip("iso_session_image_delete_postcondition", "iso session/image setup failed");
        }

        if (isoSessionOk) {
            HRESULT isoTermHr = WslcTerminateSession(isoSession);
            logResult("iso_session_terminate", SUCCEEDED(isoTermHr), "");
            HRESULT isoRelHr = WslcReleaseSession(isoSession);
            logResult("iso_session_release", SUCCEEDED(isoRelHr), "");

            char storageDetail[256];
            int isoStorageGone = deleteStoragePathAndVerifyGone(kIsoStoragePath, storageDetail, sizeof(storageDetail));
            logResult("iso_session_storage_deleted_and_verified_gone", isoStorageGone, storageDetail);
        } else {
            logSkip("iso_session_terminate", "iso session create failed");
            logSkip("iso_session_release", "iso session create failed");
            logSkip("iso_session_storage_deleted_and_verified_gone", "iso session create failed");
        }
    } else {
        logSkip("iso_session_create", "c1 not started or no IP");
        logSkip("iso_session_image_import", "c1 not started or no IP");
        logSkip("iso_session_address_allocator_bump", "c1 not started or no IP");
        logSkip("iso_session_address_allocator_bump_delete_postcondition", "c1 not started or no IP");
        logSkip("iso_peer_container_create", "c1 not started or no IP");
        logSkip("iso_peer_container_start", "c1 not started or no IP");
        logSkip("iso_peer_container_ip_inspect", "c1 not started or no IP");
        logSkip("cross_session_ip_addresses_distinct", "c1 not started or no IP");
        logSkip("cross_session_positive_control", "c1 not started or no IP");
        logSkip("cross_session_isolation_iso_to_main", "c1 not started or no IP");
        logSkip("iso_to_main_container_delete_postcondition", "c1 not started or no IP");
        logSkip("cross_session_isolation_main_to_iso", "c1 not started or no IP");
        logSkip("iso_peer_container_delete_postcondition", "c1 not started or no IP");
        logSkip("iso_session_image_delete", "c1 not started or no IP");
        logSkip("iso_session_image_delete_postcondition", "c1 not started or no IP");
        logSkip("iso_session_terminate", "c1 not started or no IP");
        logSkip("iso_session_release", "c1 not started or no IP");
        logSkip("iso_session_storage_deleted_and_verified_gone", "c1 not started or no IP");
    }

    /*
     * ---------------- 7 (early half). container termination verified,
     * before releasing the init process handle ----------------
     * c1's init process is a genuinely long-running loop (up to 40
     * iterations of a blocking listen), not a short-lived one-shot
     * command. Stopping it here and then checking its own reported state
     * and exit event -- rather than only trusting WslcStopContainer's
     * return code -- is the cancellation/termination evidence for a
     * long-running process that a one-shot exec can't provide.
     */
    if (containerStarted) {
        err = NULL;
        hr = WslcStopContainer(container, WSLC_SIGNAL_SIGTERM, 10, &err);
        describeHr(hr, err, detail, sizeof(detail));
        logResult("container_stop", SUCCEEDED(hr), detail);
        freeSdkString(err);
        err = NULL;

        if (initProcess) {
            HANDLE initExitEvt = NULL;
            (void)WslcGetProcessExitEvent(initProcess, &initExitEvt);
            DWORD initWaitRes = initExitEvt ? WaitForSingleObject(initExitEvt, 8000) : WAIT_TIMEOUT;
            WslcProcessState initState = WSLC_PROCESS_STATE_UNKNOWN;
            (void)WslcGetProcessState(initProcess, &initState);
            INT32 initExitCode = -999;
            HRESULT initExitHr = WslcGetProcessExitCode(initProcess, &initExitCode);
            snprintf(detail, sizeof(detail), "exitEventWait=%lu state=%d exitCodeHr=0x%08lX exitCode=%d",
                     (unsigned long)initWaitRes, (int)initState, (unsigned long)initExitHr, (int)initExitCode);
            logResult("container_termination_signal_verified",
                      initWaitRes == WAIT_OBJECT_0 &&
                          (initState == WSLC_PROCESS_STATE_EXITED || initState == WSLC_PROCESS_STATE_SIGNALLED),
                      detail);
        } else {
            logSkip("container_termination_signal_verified", "no init process handle");
        }
    } else if (containerCreated) {
        logSkip("container_stop", "never started");
        logSkip("container_termination_signal_verified", "never started");
    }

    if (initProcess) {
        (void)WslcReleaseProcess(initProcess);
    }

    /* ---------------- 7. teardown: container delete/release (stop already done above, with termination verification) ---------------- */
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

        WslcImageInfo *tagImagesAfter = NULL;
        uint32_t tagCountAfter = 0;
        HRESULT tagListHr = WslcListSessionImages(session, &tagImagesAfter, &tagCountAfter);
        int tagStillPresent = 0;
        if (SUCCEEDED(tagListHr) && tagImagesAfter) {
            for (uint32_t i = 0; i < tagCountAfter; i++) {
                if (strstr(tagImagesAfter[i].name, kTaggedRepo) != NULL) {
                    tagStillPresent = 1;
                    break;
                }
            }
            CoTaskMemFree(tagImagesAfter);
        }
        snprintf(detail, sizeof(detail), "listHr=0x%08lX tagStillPresent=%d", (unsigned long)tagListHr,
                 tagStillPresent);
        logResult("image_tag_delete_postcondition", SUCCEEDED(tagListHr) && !tagStillPresent, detail);
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

    {
        char storageDetail[256];
        int storageGone = deleteStoragePathAndVerifyGone(kStoragePath, storageDetail, sizeof(storageDetail));
        logResult("session_storage_deleted_and_verified_gone", storageGone, storageDetail);
    }

    printf("PROBE_DONE failures=%d\n", gFailures);
    CoUninitialize();
    return gFailures == 0 ? 0 : 2;
}
