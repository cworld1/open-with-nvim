/*
 * Launch Neovim in Windows Terminal with a dragged file or directory path.
 */

#ifndef UNICODE
#   define UNICODE
#endif
#ifndef _UNICODE
#   define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <wctype.h>

typedef struct AppConfig {
    wchar_t terminalProfile[128];
    wchar_t shell[64];
    wchar_t editor[128];
} AppConfig;

enum { PATH_CAPACITY = 32768 };

/* Fish single-quoted strings recognize both \\' and \\\\ escapes. */
static HRESULT EscapeForFishSingleQuotes(const wchar_t *input,
                                         wchar_t *output,
                                         size_t outputCount)
{
    size_t outPos = 0;

    if (outputCount == 0) {
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }

    while (*input) {
        if (*input == L'\'' || *input == L'\\') {
            if (outPos + 2 >= outputCount) {
                return STRSAFE_E_INSUFFICIENT_BUFFER;
            }
            output[outPos++] = L'\\';
        }
        if (outPos + 1 >= outputCount) {
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        }
        output[outPos++] = *input++;
    }

    output[outPos] = L'\0';
    return S_OK;
}

/* Build Windows Terminal argument strings (preferred + fallback). */
static HRESULT BuildWtArgs(const AppConfig *cfg,
                          const wchar_t *msysPath,
                          const wchar_t *msysDirectory,
                          wchar_t *args,
                          size_t argsCount,
                          wchar_t *altArgs,
                          size_t altCount)
{
    wchar_t escapedPath[PATH_CAPACITY];
    wchar_t escapedDirectory[PATH_CAPACITY];

    if (msysPath != NULL && *msysPath != L'\0') {
        if (msysDirectory == NULL || *msysDirectory == L'\0') {
            return E_INVALIDARG;
        }
        if (FAILED(EscapeForFishSingleQuotes(msysPath, escapedPath, _countof(escapedPath)))) {
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        }
        if (FAILED(EscapeForFishSingleQuotes(msysDirectory, escapedDirectory, _countof(escapedDirectory)))) {
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        }

        if (FAILED(StringCchPrintfW(
                altArgs,
                altCount,
                L"-p \"%s\" %s -c \"cd -- '%s' && exec %s -- '%s'\"",
                cfg->terminalProfile,
                cfg->shell,
                escapedDirectory,
                cfg->editor,
                escapedPath))) {
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        }
    } else {
        if (FAILED(StringCchPrintfW(
                altArgs,
                altCount,
                L"-p \"%s\" %s -c \"%s\"",
                cfg->terminalProfile,
                cfg->shell,
                cfg->editor))) {
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        }
    }

    return StringCchPrintfW(args, argsCount, L"-w 0 %s", altArgs);
}

/* Build an absolute path to a file placed next to the current executable. */
static HRESULT GetSiblingPath(const wchar_t *fileName,
                              wchar_t *outPath,
                              size_t outCount)
{
    DWORD len = GetModuleFileNameW(NULL, outPath, (DWORD)outCount);
    if (len == 0 || len >= outCount) {
        return E_FAIL;
    }

    wchar_t *lastSlash = wcsrchr(outPath, L'\\');
    if (lastSlash == NULL) {
        return E_FAIL;
    }
    *(lastSlash + 1) = L'\0';

    return StringCchCatW(outPath, outCount, fileName);
}

/* Create a default config file if it does not exist. */
static void EnsureDefaultConfigFile(const wchar_t *configPath)
{
    static const char defaultConfig[] =
        "; open-with-nvim runtime configuration\r\n"
        "[open-with-nvim]\r\n"
        "terminal_profile=Fish Shell\r\n"
        "shell=fish\r\n"
        "editor=nvim\r\n";

    HANDLE hFile = CreateFileW(configPath,
                               GENERIC_WRITE,
                               FILE_SHARE_READ,
                               NULL,
                               CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(hFile,
                        defaultConfig,
                        (DWORD)(sizeof(defaultConfig) - 1),
                        &written,
                        NULL);
    CloseHandle(hFile);
}

/* Load runtime config from ini file in the executable directory. */
static void LoadAppConfig(AppConfig *cfg)
{
    wchar_t configPath[PATH_CAPACITY];

    StringCchCopyW(cfg->terminalProfile, _countof(cfg->terminalProfile), L"Fish Shell");
    StringCchCopyW(cfg->shell, _countof(cfg->shell), L"fish");
    StringCchCopyW(cfg->editor, _countof(cfg->editor), L"nvim");

    if (FAILED(GetSiblingPath(L"open-with-nvim.ini", configPath, _countof(configPath)))) {
        return;
    }

    EnsureDefaultConfigFile(configPath);

    GetPrivateProfileStringW(L"open-with-nvim",
                             L"terminal_profile",
                             L"Fish Shell",
                             cfg->terminalProfile,
                             (DWORD)_countof(cfg->terminalProfile),
                             configPath);

    GetPrivateProfileStringW(L"open-with-nvim",
                             L"shell",
                             L"fish",
                             cfg->shell,
                             (DWORD)_countof(cfg->shell),
                             configPath);

    GetPrivateProfileStringW(L"open-with-nvim",
                             L"editor",
                             L"nvim",
                             cfg->editor,
                             (DWORD)_countof(cfg->editor),
                             configPath);
}

/* Convert "C:\\foo\\Bar" to "/c/foo/Bar" for MSYS-compatible shells. */
static HRESULT WinPathToMsys2(const wchar_t *winPath,
                              wchar_t *outBuf,
                              size_t outSize)
{
    size_t srcLen = wcslen(winPath);

    if (outSize < 3) {
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }

    if (srcLen < 2 || winPath[1] != L':') {
        HRESULT hr = StringCchCopyW(outBuf, outSize, winPath);
        if (SUCCEEDED(hr)) {
            for (wchar_t *p = outBuf; *p; ++p) {
                if (*p == L'\\') *p = L'/';
            }
        }
        return hr;
    }

    wchar_t drive = towlower(winPath[0]);
    outBuf[0] = L'/';
    outBuf[1] = drive;
    outBuf[2] = L'\0';

    const wchar_t *src = winPath + 2;
    size_t pos = 2;

    while (*src && pos + 1 < outSize) {
        wchar_t ch = *src++;
        outBuf[pos++] = (ch == L'\\') ? L'/' : ch;
    }

    if (*src != L'\0') {
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }

    outBuf[pos] = L'\0';
    return S_OK;
}

/* Launch Windows Terminal using ShellExecute without opening a console window. */
static BOOL LaunchWt(const AppConfig *cfg, const wchar_t *msysPath,
                     const wchar_t *msysDirectory)
{
    wchar_t args[65536];
    wchar_t altArgs[65536];

    if (FAILED(BuildWtArgs(cfg, msysPath, msysDirectory, args, _countof(args), altArgs, _countof(altArgs)))) {
        return FALSE;
    }

    /* Try preferred form first, then fallback to the alternate if it fails. */
    HINSTANCE rc = ShellExecuteW(NULL, L"open", L"wt.exe", args, NULL, SW_SHOWNORMAL);

    if (!((INT_PTR)rc > 32)) {
        rc = ShellExecuteW(NULL, L"open", L"wt.exe", altArgs, NULL, SW_SHOWNORMAL);
    }

    return ((INT_PTR)rc > 32);
}

/* Resolve the target before Terminal changes the inherited working directory. */
static HRESULT ResolveTargetPaths(const wchar_t *input,
                                   wchar_t *msysPath,
                                   wchar_t *msysDirectory)
{
    wchar_t path[PATH_CAPACITY];
    DWORD length = GetFullPathNameW(input, _countof(path), path, NULL);
    if (length == 0 || length >= _countof(path)) {
        return E_FAIL;
    }

    HRESULT hr = WinPathToMsys2(path, msysPath, PATH_CAPACITY);
    if (FAILED(hr)) {
        return hr;
    }

    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash == NULL) {
            return E_FAIL;
        }
        /* Keep the separator in a drive root, including for a new file. */
        if (slash == path + 2 && path[1] == L':') {
            ++slash;
        }
        *slash = L'\0';
    }
    return WinPathToMsys2(path, msysDirectory, PATH_CAPACITY);
}

/* Windows subsystem entry point. */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR pCmdLine,
                    int nShowCmd)
{
    (void)hInst;
    (void)hPrev;
    (void)pCmdLine;
    (void)nShowCmd;

    AppConfig cfg;
    LoadAppConfig(&cfg);

    const wchar_t *error = NULL;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t msysPath[PATH_CAPACITY];
    wchar_t msysDirectory[PATH_CAPACITY];

    if (argv == NULL) {
        error = L"Failed to parse command line.";
    } else if (argc > 1 && FAILED(ResolveTargetPaths(argv[1], msysPath, msysDirectory))) {
        error = L"Failed to resolve the selected path or working directory.";
    } else if (!LaunchWt(&cfg, argc > 1 ? msysPath : NULL,
                        argc > 1 ? msysDirectory : NULL)) {
        error = L"Failed to start Windows Terminal (wt.exe).";
    }

    LocalFree(argv);
    if (error != NULL) {
        MessageBoxW(NULL, error, L"Open with Nvim", MB_ICONERROR);
        return 1;
    }
    return 0;
}
