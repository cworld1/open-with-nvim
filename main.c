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
static BOOL LaunchWtWithPath(const wchar_t *msysPath)
{
    wchar_t escapedPath[32768];
    wchar_t args[65536];

    if (FAILED(EscapeForFishSingleQuotes(msysPath, escapedPath, _countof(escapedPath)))) {
        return FALSE;
    }

    if (FAILED(StringCchPrintfW(
            args,
            _countof(args),
            L"-w 0 -p \"Fish Shell\" fish -c \"nvim -- '%s'\"",
            escapedPath))) {
        return FALSE;
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
    (void)hInst;
    (void)hPrev;
    (void)pCmdLine;
    (void)nShowCmd;

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == NULL || argc < 2) {
        MessageBoxW(NULL,
                    L"Drag a file or folder onto this executable.",
                    L"Usage",
                    MB_OK);
        if (argv != NULL) {
            LocalFree(argv);
        }
        return 1;
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

    if (!LaunchWtWithPath(msysPath)) {
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
