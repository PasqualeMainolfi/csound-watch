#include "watch_process.h"

#include <stddef.h>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string.h>
#include <wchar.h>

#define WATCH_VIEWER_FILENAME L"watch_viewer.exe"
#define WATCH_PROCESS_PATH_CAPACITY 32768U

static const char watch_process_module_anchor = 0;

static int32_t existing_file(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
}

static int32_t sibling_viewer_path(wchar_t path[WATCH_PROCESS_PATH_CAPACITY]) {
    HMODULE module = NULL;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR) &watch_process_module_anchor,
            &module)
    ) {
        return 0;
    }

    DWORD length = GetModuleFileNameW(module, path, WATCH_PROCESS_PATH_CAPACITY);
    if (length == 0U || length >= WATCH_PROCESS_PATH_CAPACITY) {
        return 0;
    }

    wchar_t *separator = path + length;
    while (separator > path && separator[-1] != L'\\' && separator[-1] != L'/') {
        separator--;
    }
    if (separator == path) {
        return 0;
    }

    size_t directory_length = (size_t) (separator - path);
    size_t filename_length = wcslen(WATCH_VIEWER_FILENAME);
    if (directory_length + filename_length >= WATCH_PROCESS_PATH_CAPACITY) {
        return 0;
    }

    memcpy(path + directory_length, WATCH_VIEWER_FILENAME, (filename_length + 1U) * sizeof(wchar_t));
    return existing_file(path);
}

static int32_t risset_viewer_path(wchar_t path[WATCH_PROCESS_PATH_CAPACITY]) {
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, WATCH_PROCESS_PATH_CAPACITY);
    if (length == 0U || length >= WATCH_PROCESS_PATH_CAPACITY) {
        return 0;
    }

    static const wchar_t suffix[] = L"\\risset\\assets\\watch\\" WATCH_VIEWER_FILENAME;
    size_t suffix_length = wcslen(suffix);
    if ((size_t) length + suffix_length >= WATCH_PROCESS_PATH_CAPACITY) {
        return 0;
    }

    memcpy(path + length, suffix, (suffix_length + 1U) * sizeof(wchar_t));
    return existing_file(path);
}

static int32_t resolve_viewer_path(wchar_t path[WATCH_PROCESS_PATH_CAPACITY]) {
    DWORD environment_length = GetEnvironmentVariableW(L"CSOUND_WATCH_VIEWER", path, WATCH_PROCESS_PATH_CAPACITY);
    if (environment_length > 0U && environment_length < WATCH_PROCESS_PATH_CAPACITY && existing_file(path)) {
        return 1;
    }

    if (sibling_viewer_path(path)) {
        return 1;
    }
    if (risset_viewer_path(path)) {
        return 1;
    }

    DWORD length = SearchPathW(NULL, WATCH_VIEWER_FILENAME, NULL, WATCH_PROCESS_PATH_CAPACITY, path, NULL);
    return length > 0U && length < WATCH_PROCESS_PATH_CAPACITY && existing_file(path);
}

int32_t watch_process_launch_viewer(void) {
    wchar_t executable[WATCH_PROCESS_PATH_CAPACITY];
    if (!resolve_viewer_path(executable)) {
        return (int32_t) ERROR_FILE_NOT_FOUND;
    }

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);

    if (!CreateProcessW(
            executable,
            NULL,
            NULL,
            NULL,
            FALSE,
            CREATE_NEW_PROCESS_GROUP,
            NULL,
            NULL,
            &startup,
            &process)) {
        return (int32_t) GetLastError();
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

#else

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define WATCH_VIEWER_FILENAME "watch_viewer"

static const char watch_process_module_anchor = 0;

static int32_t executable_file(const char *path) {
    return path != NULL
        && path[0] != '\0'
        && access(path, X_OK) == 0;
}

/*
 * risset unpacks its assets with Python's zipfile, which discards the mode bits
 * recorded in the archive, so the viewer can be installed without its execute
 * permission. Restore it instead of reporting a viewer that is not there.
 */
static int32_t executable_asset_file(const char *path) {
    if (executable_file(path)) {
        return 1;
    }

    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        return 0;
    }

    mode_t mode = info.st_mode & 07777;
    if (mode & S_IRUSR) {
        mode |= S_IXUSR;
    }
    if (mode & S_IRGRP) {
        mode |= S_IXGRP;
    }
    if (mode & S_IROTH) {
        mode |= S_IXOTH;
    }

    if (chmod(path, mode) != 0) {
        return 0;
    }

    return executable_file(path);
}

static int32_t join_path(
    char path[PATH_MAX],
    const char *directory,
    size_t directory_length
) {
    int written = snprintf(
        path,
        PATH_MAX,
        "%.*s%s%s",
        (int) directory_length,
        directory,
        directory_length > 0U && directory[directory_length - 1U] == '/' ? "" : "/",
        WATCH_VIEWER_FILENAME);

    return written > 0 && written < PATH_MAX;
}

static int32_t sibling_viewer_path(char path[PATH_MAX]) {
    Dl_info module;
    memset(&module, 0, sizeof(module));
    if (dladdr(&watch_process_module_anchor, &module) == 0 || module.dli_fname == NULL) {
        return 0;
    }

    const char *separator = strrchr(module.dli_fname, '/');
    if (separator == NULL) {
        return 0;
    }

    size_t directory_length = (size_t) (separator - module.dli_fname);
    return join_path(path, module.dli_fname, directory_length) && executable_file(path);
}

static int32_t risset_viewer_path(char path[PATH_MAX]) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return 0;
    }

#if defined(__APPLE__)
    static const char suffix[] = "/Library/Application Support/risset/assets/watch/"WATCH_VIEWER_FILENAME;
#else
    static const char suffix[] = "/.local/share/risset/assets/watch/"WATCH_VIEWER_FILENAME;
#endif

    int written = snprintf(path, PATH_MAX, "%s%s", home, suffix);
    return written > 0 && written < PATH_MAX && executable_asset_file(path);
}

static int32_t path_viewer_path(char path[PATH_MAX]) {
    const char *path_environment = getenv("PATH");
    if (path_environment == NULL) {
        return 0;
    }

    const char *component = path_environment;
    while (1) {
        const char *separator = strchr(component, ':');
        size_t length = separator == NULL ? strlen(component) : (size_t) (separator - component);
        const char *directory = length == 0U ? "." : component;
        size_t directory_length = length == 0U ? 1U : length;

        if (directory_length <= (size_t) INT_MAX && join_path(path, directory, directory_length) && executable_file(path)) {
            return 1;
        }

        if (separator == NULL) {
            return 0;
        }
        component = separator + 1;
    }
}

static int32_t resolve_viewer_path(char path[PATH_MAX]) {
    const char *override = getenv("CSOUND_WATCH_VIEWER");
    if (executable_file(override)) {
        int written = snprintf(path, PATH_MAX, "%s", override);
        if (written > 0 && written < PATH_MAX) {
            return 1;
        }
    }

    return sibling_viewer_path(path) || risset_viewer_path(path) || path_viewer_path(path);
}

int32_t watch_process_launch_viewer(void) {
    char executable[PATH_MAX];
    if (!resolve_viewer_path(executable)) {
        return ENOENT;
    }

    pid_t intermediate = fork();
    if (intermediate < 0) {
        return errno;
    }

    if (intermediate == 0) {
        if (setsid() < 0) {
            _exit(126);
        }

        pid_t viewer = fork();
        if (viewer < 0) {
            _exit(126);
        }
        if (viewer > 0) {
            _exit(0);
        }

        execl(executable, executable, (char *) NULL);
        _exit(127);
    }

    int status = 0;
    while (waitpid(intermediate, &status, 0) < 0) {
        if (errno != EINTR) {
            return errno;
        }
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : ECHILD;
}

#endif
