/* Interactive 64-bit GetOpenFileNameW portal smoke probe.
 *
 * The tester should identify the native desktop portal and press Cancel.
 * The output records success/cancel/error only; it never records a path.
 */
#include <windows.h>
#include <commdlg.h>

static HANDLE output;
static char buffer[256];

static void emit(const char *text)
{
    DWORD written;
    WriteFile(output, text, lstrlenA(text), &written, NULL);
}

#define PRINT(...) do { wsprintfA(buffer, __VA_ARGS__); emit(buffer); } while (0)

void mainCRTStartup(void)
{
    OPENFILENAMEW open_file;
    WCHAR selected[MAX_PATH];
    static const WCHAR filter[] =
        L"Ableton sets and text\0*.als;*.txt\0All files\0*.*\0\0";
    BOOL result;
    DWORD error;

    output = CreateFileA("portalprobe.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(2);

    memset(&open_file, 0, sizeof(open_file));
    memset(selected, 0, sizeof(selected));
    open_file.lStructSize = sizeof(open_file);
    open_file.lpstrFilter = filter;
    open_file.lpstrFile = selected;
    open_file.nMaxFile = MAX_PATH;
    open_file.lpstrTitle = L"Ableton Wine beta portal probe: press Cancel";
    open_file.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    emit("calling GetOpenFileNameW\r\n");
    result = GetOpenFileNameW(&open_file);
    error = CommDlgExtendedError();
    PRINT("returned=%u selected=%u extended_error=%u\r\n",
          (unsigned)result, (unsigned)(result && selected[0]), (unsigned)error);
    CloseHandle(output);
    ExitProcess(error ? 1 : 0);
}
