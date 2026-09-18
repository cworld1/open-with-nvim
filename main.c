/* A configurable terminal launcher for Neovim. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <string.h>
#include <wctype.h>

enum { PATH_CAPACITY = 32768, COMMAND_CAPACITY = 65536 };

typedef enum { TITLE_FILENAME, TITLE_SIMPLE, TITLE_FULL } TitleStyle;
typedef enum { FILE_NORMAL, FILE_MSYS } FileStyle;

typedef struct {
    wchar_t command[COMMAND_CAPACITY];
    TitleStyle titleStyle;
    FileStyle fileStyle;
} AppConfig;

static HRESULT GetSiblingPath(const wchar_t *name, wchar_t *path, size_t pathCount)
{
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)pathCount);
    wchar_t *separator = wcsrchr(path, L'\\');
    if (length == 0 || length >= pathCount || separator == NULL) return E_FAIL;
    separator[1] = L'\0';
    return StringCchCatW(path, pathCount, name);
}

static void CreateDefaultConfig(const wchar_t *path)
{
    static const char content[] =
        "; open-with-nvim runtime configuration\n"
        "[open-with-nvim]\n"
        "; %f=file, %d=terminal directory, %t=title\n"
        "command=wt nt -d %d -p \"Fish Shell\" --title %t nvim %f\n"
        "; normal (C:\\path) or msys (/c/path) for %f and %t\n"
        "file-style=normal\n"
        "; filename, simple, or full\n"
        "title-style=filename\n";
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, content, (DWORD)(sizeof(content) - 1), &written, NULL);
        CloseHandle(file);
    }
}

/* Profile APIs parse embedded quotes, so command is read as literal INI text. */
static void ReadCommandSetting(const wchar_t *path, wchar_t *command, size_t commandCount)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    char *text = NULL;
    DWORD read;

    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) ||
        size.QuadPart < 0 || size.QuadPart > COMMAND_CAPACITY) goto done;
    text = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart + 1);
    if (text == NULL || !ReadFile(file, text, (DWORD)size.QuadPart, &read, NULL)) goto done;
    text[read] = '\0';

    BOOL inSection = FALSE;
    for (char *line = text; line != NULL;) {
        char *next = strpbrk(line, "\r\n");
        if (next != NULL) {
            *next++ = '\0';
            while (*next == '\r' || *next == '\n') ++next;
        }
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '[') {
            inSection = strcmp(line, "[open-with-nvim]") == 0;
        } else if (inSection && strncmp(line, "command", 7) == 0) {
            char *value = line + 7;
            while (*value == ' ' || *value == '\t') ++value;
            if (*value == L'=') {
                ++value;
                while (*value == ' ' || *value == '\t') ++value;
                char *end = value + strlen(value);
                while (end > value && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
                if (end - value >= 2 && value[0] == '"' && end[-1] == '"') {
                    ++value;
                    end[-1] = '\0';
                }
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, command,
                                        (int)commandCount) == 0)
                    MultiByteToWideChar(CP_ACP, 0, value, -1, command, (int)commandCount);
                break;
            }
        }
        line = next;
    }

done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (text != NULL) HeapFree(GetProcessHeap(), 0, text);
}

static void LoadConfig(AppConfig *config)
{
    wchar_t iniPath[PATH_CAPACITY], titleStyle[16], fileStyle[16];
    StringCchCopyW(config->command, _countof(config->command),
                   L"wt nt -d %d -p \"Fish Shell\" --title %t nvim %f");
    config->titleStyle = TITLE_FILENAME;
    config->fileStyle = FILE_NORMAL;
    if (FAILED(GetSiblingPath(L"open-with-nvim.ini", iniPath, _countof(iniPath)))) return;

    CreateDefaultConfig(iniPath);
    ReadCommandSetting(iniPath, config->command, _countof(config->command));
    GetPrivateProfileStringW(L"open-with-nvim", L"title-style", L"filename", titleStyle,
                             (DWORD)_countof(titleStyle), iniPath);
    GetPrivateProfileStringW(L"open-with-nvim", L"file-style", L"normal", fileStyle,
                             (DWORD)_countof(fileStyle), iniPath);
    if (_wcsicmp(titleStyle, L"simple") == 0) config->titleStyle = TITLE_SIMPLE;
    else if (_wcsicmp(titleStyle, L"full") == 0) config->titleStyle = TITLE_FULL;
    if (_wcsicmp(fileStyle, L"msys") == 0) config->fileStyle = FILE_MSYS;
}

/* Append one argument using the quoting rules used by CreateProcess. */
static HRESULT QuoteArgument(wchar_t *output, size_t outputCount, const wchar_t *input)
{
    if (FAILED(StringCchCatW(output, outputCount, L"\""))) return STRSAFE_E_INSUFFICIENT_BUFFER;
    for (const wchar_t *cursor = input;;) {
        size_t slashes = 0;
        while (*cursor == L'\\') { ++slashes; ++cursor; }
        size_t count = *cursor == L'"' ? slashes * 2 + 1 : *cursor ? slashes : slashes * 2;
        for (size_t i = 0; i < count; ++i)
            if (FAILED(StringCchCatW(output, outputCount, L"\\"))) return STRSAFE_E_INSUFFICIENT_BUFFER;
        if (*cursor == L'\0') break;
        if (FAILED(StringCchCatNW(output, outputCount, cursor, 1))) return STRSAFE_E_INSUFFICIENT_BUFFER;
        ++cursor;
    }
    return StringCchCatW(output, outputCount, L"\"");
}

static HRESULT GetTargetDirectory(const wchar_t *target, wchar_t *directory, size_t directoryCount)
{
    DWORD attributes = GetFileAttributesW(target);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return StringCchCopyW(directory, directoryCount, target);
    HRESULT result = StringCchCopyW(directory, directoryCount, target);
    wchar_t *separator = wcsrchr(directory, L'\\');
    if (FAILED(result) || separator == NULL) return E_FAIL;
    if (separator == directory + 2 && directory[1] == L':') ++separator;
    *separator = L'\0';
    return S_OK;
}

static HRESULT GetDefaultDirectory(wchar_t *directory, size_t directoryCount)
{
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", directory, (DWORD)directoryCount);
    if (length > 0 && length < directoryCount) return S_OK;
    length = GetCurrentDirectoryW((DWORD)directoryCount, directory);
    return (length > 0 && length < directoryCount) ? S_OK : E_FAIL;
}

static HRESULT FormatFilePath(FileStyle style, const wchar_t *input, wchar_t *output, size_t outputCount)
{
    if (style == FILE_NORMAL) return StringCchCopyW(output, outputCount, input);
    if (input[0] && input[1] == L':') {
        if (FAILED(StringCchPrintfW(output, outputCount, L"/%c%s", towlower(input[0]), input + 2)))
            return STRSAFE_E_INSUFFICIENT_BUFFER;
    } else if (FAILED(StringCchCopyW(output, outputCount, input))) {
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }
    for (wchar_t *p = output; *p; ++p) if (*p == L'\\') *p = L'/';
    return S_OK;
}

static HRESULT BuildTitle(const wchar_t *path, TitleStyle style, wchar_t *title, size_t titleCount)
{
    wchar_t normalized[PATH_CAPACITY];
    HRESULT result = StringCchCopyW(normalized, _countof(normalized), path);
    if (FAILED(result)) return result;
    for (wchar_t *p = normalized; *p; ++p) if (*p == L'\\') *p = L'/';
    wchar_t *filename = wcsrchr(normalized, L'/');
    filename = filename ? filename + 1 : normalized;
    if (style == TITLE_FILENAME) return StringCchCopyW(title, titleCount, filename);
    if (style == TITLE_FULL) return StringCchCopyW(title, titleCount, normalized);

    title[0] = L'\0';
    for (wchar_t *part = normalized; *part;) {
        wchar_t *separator = wcschr(part, L'/');
        if (separator == NULL) return StringCchCatW(title, titleCount, part);
        size_t length = (part == normalized && part[1] == L':') ? 2 : 1;
        if (separator != part && FAILED(StringCchCatNW(title, titleCount, part, length)))
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        if (FAILED(StringCchCatW(title, titleCount, L"/"))) return STRSAFE_E_INSUFFICIENT_BUFFER;
        part = separator + 1;
    }
    return S_OK;
}

static HRESULT AppendPlaceholder(wchar_t code, const wchar_t *file, const wchar_t *directory,
                                 const wchar_t *title, wchar_t *command, size_t commandCount)
{
    if (code == L'%') return StringCchCatW(command, commandCount, L"%");
    if (code == L'f') return file ? QuoteArgument(command, commandCount, file) : S_OK;
    if (code == L'd') return QuoteArgument(command, commandCount, directory);
    if (code == L't') return QuoteArgument(command, commandCount, title);
    return E_INVALIDARG;
}

static HRESULT ExpandCommand(const AppConfig *config, const wchar_t *target,
                             wchar_t *command, size_t commandCount)
{
    wchar_t directory[PATH_CAPACITY], file[PATH_CAPACITY], title[PATH_CAPACITY] = L"Neovim";
    if (target && FAILED(GetTargetDirectory(target, directory, _countof(directory)))) return E_FAIL;
    if (!target && FAILED(GetDefaultDirectory(directory, _countof(directory)))) return E_FAIL;
    if (target && FAILED(FormatFilePath(config->fileStyle, target, file, _countof(file)))) return E_FAIL;
    if (target && FAILED(BuildTitle(file, config->titleStyle, title, _countof(title)))) return E_FAIL;

    command[0] = L'\0';
    for (const wchar_t *p = config->command; *p; ++p) {
        if (*p == L'%' && wcschr(L"fdt%", p[1])) {
            HRESULT result = AppendPlaceholder(p[1], target ? file : NULL, directory, title,
                                               command, commandCount);
            if (FAILED(result)) return result;
            ++p;
        } else if (FAILED(StringCchCatNW(command, commandCount, p, 1))) {
            return STRSAFE_E_INSUFFICIENT_BUFFER;
        }
    }
    return S_OK;
}

static BOOL Launch(const AppConfig *config, const wchar_t *target)
{
    wchar_t command[COMMAND_CAPACITY];
    STARTUPINFOW startup = { .cb = sizeof(startup) };
    PROCESS_INFORMATION process = {0};
    if (FAILED(ExpandCommand(config, target, command, _countof(command)))) return FALSE;
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int show)
{
    (void)instance; (void)previous; (void)commandLine; (void)show;
    AppConfig config;
    wchar_t target[PATH_CAPACITY];
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *error = NULL;
    LoadConfig(&config);

    if (argv == NULL) {
        error = L"Failed to parse command line.";
    }
    else {
        const wchar_t *launchTarget = NULL;
        if (argc > 1) {
            DWORD length = GetFullPathNameW(argv[1], _countof(target), target, NULL);
            if (length == 0 || length >= _countof(target)) {
                error = L"Failed to resolve the selected path.";
            } else {
                launchTarget = target;
            }
        }
        if (error == NULL && !Launch(&config, launchTarget))
            error = L"Failed to start the configured terminal command.";
    }
    LocalFree(argv);
    if (error) {
        MessageBoxW(NULL, error, L"Open with Nvim", MB_ICONERROR);
        return 1;
    }
    return 0;
}
