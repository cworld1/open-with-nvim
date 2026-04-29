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

static HRESULT WinPathToMsys2(const wchar_t *winPath,
                              wchar_t *outBuf,
                              size_t outSize);

/* Build an absolute path to a file placed next to the current executable. */
static HRESULT GetSiblingPath(const wchar_t *fileName,
                              wchar_t *outPath,
                              size_t outCount)
{
    wchar_t exePath[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exePath, _countof(exePath));
    if (len == 0 || len >= _countof(exePath)) {
        return E_FAIL;
    }

    wchar_t *lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash == NULL) {
        return E_FAIL;
    }
    *(lastSlash + 1) = L'\0';

    if (FAILED(StringCchCopyW(outPath, outCount, exePath))) {
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    return StringCchCatW(outPath, outCount, fileName);
}

/* Create a default config file if it does not exist. */
static BOOL EnsureDefaultConfigFile(const wchar_t *configPath)
{
    static const char defaultConfig[] =
        "; open-with-nvim runtime configuration\r\n"
        "[open-with-nvim]\r\n"
        "terminal_profile=Fish Shell\r\n"
        "shell=fish\r\n"
        "editor=nvim\r\n";

    DWORD attr = GetFileAttributesW(configPath);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return TRUE;
    }

    HANDLE hFile = CreateFileW(configPath,
                               GENERIC_WRITE,
                               FILE_SHARE_READ,
                               NULL,
                               CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hFile,
                        defaultConfig,
                        (DWORD)(sizeof(defaultConfig) - 1),
                        &written,
                        NULL);
    CloseHandle(hFile);
    return ok;
}

/* Load runtime config from ini file in the executable directory. */
static void LoadAppConfig(AppConfig *cfg)
{
    wchar_t configPath[MAX_PATH];

    StringCchCopyW(cfg->terminalProfile, _countof(cfg->terminalProfile), L"Fish Shell");
    StringCchCopyW(cfg->shell, _countof(cfg->shell), L"fish");
    StringCchCopyW(cfg->editor, _countof(cfg->editor), L"nvim");

    if (FAILED(GetSiblingPath(L"open-with-nvim.ini", configPath, _countof(configPath)))) {
        return;
    }

    EnsureDefaultConfigFile(configPath);

    GetPrivateProfileStringW(L"open-with-nvim",
                             L"terminal_profile",
                             cfg->terminalProfile,
                             cfg->terminalProfile,
                             (DWORD)_countof(cfg->terminalProfile),
                             configPath);

    GetPrivateProfileStringW(L"open-with-nvim",
                             L"shell",
                             cfg->shell,
                             cfg->shell,
                             (DWORD)_countof(cfg->shell),
                             configPath);

    GetPrivateProfileStringW(L"open-with-nvim",
                             L"editor",
                             cfg->editor,
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

    if (srcLen < 2 || winPath[1] != L':') {
        return StringCchCopyW(outBuf, outSize, winPath);
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

/* Escape single quotes for safe use inside fish single-quoted strings. */
static HRESULT EscapeForFishSingleQuotes(const wchar_t *input,
                                         wchar_t *output,
                                         size_t outputCount)
{
    const wchar_t *escapeChunk = L"'\\''";
    size_t outPos = 0;

    if (outputCount == 0) {
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }

    while (*input) {
        if (*input == L'\'') {
            size_t i = 0;
            while (escapeChunk[i]) {
                if (outPos + 1 >= outputCount) {
                    return STRSAFE_E_INSUFFICIENT_BUFFER;
                }
                output[outPos++] = escapeChunk[i++];
            }
        } else {
            if (outPos + 1 >= outputCount) {
                return STRSAFE_E_INSUFFICIENT_BUFFER;
            }
            output[outPos++] = *input;
        }
        ++input;
    }

    output[outPos] = L'\0';
    return S_OK;
}

/* Launch Windows Terminal using ShellExecute without opening a console window. */
static BOOL LaunchWt(const AppConfig *cfg, const wchar_t *msysPath)
{
    wchar_t escapedPath[32768];
    wchar_t args[65536];

    if (msysPath != NULL && *msysPath != L'\0') {
        if (FAILED(EscapeForFishSingleQuotes(msysPath, escapedPath, _countof(escapedPath)))) {
            return FALSE;
        }

        if (FAILED(StringCchPrintfW(
                args,
                _countof(args),
                L"-w 0 -p \"%s\" %s -c \"%s -- '%s'\"",
                cfg->terminalProfile,
                cfg->shell,
                cfg->editor,
                escapedPath))) {
            return FALSE;
        }
    } else {
        if (FAILED(StringCchPrintfW(
                args,
                _countof(args),
                L"-w 0 -p \"%s\" %s -c \"%s\"",
                cfg->terminalProfile,
                cfg->shell,
                cfg->editor))) {
            return FALSE;
        }
    }

    HINSTANCE rc = ShellExecuteW(
        NULL,               /* Parent window */
        L"open",            /* Shell action */
        L"wt.exe",          /* Executable resolved from PATH */
        args,               /* Command arguments */
        NULL,               /* Default working directory */
        SW_HIDE);           /* Keep the launcher hidden */

    return ((INT_PTR)rc > 32);
}

/* Windows subsystem entry point. */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR pCmdLine,
                    int nShowCmd)
{
    AppConfig cfg;

    (void)hInst;
    (void)hPrev;
    (void)pCmdLine;
    (void)nShowCmd;

    LoadAppConfig(&cfg);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL) {
        MessageBoxW(NULL,
                    L"Failed to parse command line.",
                    L"Error",
                    MB_ICONERROR);
        return 1;
    }

    if (argc < 2) {
        BOOL ok = LaunchWt(&cfg, NULL);
        LocalFree(argv);
        if (!ok) {
            MessageBoxW(NULL,
                        L"Failed to start Windows Terminal (wt.exe).",
                        L"Error",
                        MB_ICONERROR);
            return 1;
        }
        return 0;
    }

    const wchar_t *rawPath = argv[1];

    size_t rawLen = wcslen(rawPath);
    size_t msysCap = (rawLen * 4) + 16;

    wchar_t *msysPath = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                             msysCap * sizeof(wchar_t));
    if (msysPath == NULL) {
        LocalFree(argv);
        MessageBoxW(NULL,
                    L"Memory allocation failed.",
                    L"Error",
                    MB_ICONERROR);
        return 1;
    }

    if (FAILED(WinPathToMsys2(rawPath, msysPath, msysCap))) {
        HeapFree(GetProcessHeap(), 0, msysPath);
        LocalFree(argv);
        MessageBoxW(NULL,
                    L"Path conversion failed.",
                    L"Error",
                    MB_ICONERROR);
        return 1;
    }

    if (!LaunchWt(&cfg, msysPath)) {
        HeapFree(GetProcessHeap(), 0, msysPath);
        LocalFree(argv);
        MessageBoxW(NULL,
                    L"Failed to start Windows Terminal (wt.exe).",
                    L"Error",
                    MB_ICONERROR);
        return 1;
    }

    HeapFree(GetProcessHeap(), 0, msysPath);
    LocalFree(argv);
    return 0;
}
