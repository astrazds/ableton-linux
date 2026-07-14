/* Standalone regression probe derived from ableton/wmresize.c.
 *
 * Ableton Live adds four pixels to AdjustWindowRectExForDpi's menu-bearing
 * non-client height. The patched Wine menu layout must therefore produce a
 * real client inset equal to AdjustWindowRectExForDpi + 4. If it does not,
 * every ConfigureNotify can grow the main window by four pixels.
 *
 * Output: resizeprobe.txt. Exit 0 means every deliberate resize settled with
 * zero drift; exit 1 means at least one measured non-zero drift.
 */
#include <windows.h>

static HANDLE output;
static char buffer[512];
static int measuring;
static int reentry;
static int failures;

static void emit(const char *text)
{
    DWORD written;
    WriteFile(output, text, lstrlenA(text), &written, NULL);
}

#define PRINT(...) do { wsprintfA(buffer, __VA_ARGS__); emit(buffer); } while (0)

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_WINDOWPOSCHANGED && measuring && !reentry)
    {
        RECT client, outer, adjusted;
        int expected_width, expected_height, drift_width, drift_height;

        GetClientRect(window, &client);
        GetWindowRect(window, &outer);
        SetRect(&adjusted, 0, 0, client.right, client.bottom);
        AdjustWindowRectExForDpi(
            &adjusted,
            (DWORD)GetWindowLongPtrW(window, GWL_STYLE),
            GetMenu(window) != NULL,
            (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE),
            96);

        /* Live's observed model is AdjustWindowRectExForDpi + four vertical pixels. */
        adjusted.bottom += 4;
        expected_width = adjusted.right - adjusted.left;
        expected_height = adjusted.bottom - adjusted.top;
        drift_width = expected_width - (outer.right - outer.left);
        drift_height = expected_height - (outer.bottom - outer.top);

        PRINT("measure outer=%dx%d client=%dx%d expected=%dx%d drift=%d,%d\r\n",
              (int)(outer.right - outer.left), (int)(outer.bottom - outer.top),
              (int)client.right, (int)client.bottom,
              expected_width, expected_height, drift_width, drift_height);

        if (drift_width || drift_height) failures++;

        reentry = 1;
        SetWindowPos(window, NULL, 0, 0, expected_width, expected_height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        reentry = 0;
    }
    else if (message == WM_TIMER)
    {
        KillTimer(window, (UINT_PTR)wparam);
        if (wparam == 1)
        {
            measuring = 1;
            PRINT("drive resize 1000x700\r\n");
            SetWindowPos(window, NULL, 0, 0, 1000, 700,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            SetTimer(window, 2, 900, NULL);
        }
        else if (wparam == 2)
        {
            PRINT("drive resize 1100x740\r\n");
            SetWindowPos(window, NULL, 0, 0, 1100, 740,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            SetTimer(window, 3, 900, NULL);
        }
        else if (wparam == 3)
        {
            PRINT("RESULT %s failures=%d\r\n", failures ? "FAIL" : "PASS", failures);
            PostQuitMessage(failures ? 1 : 0);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    WNDCLASSW window_class;
    HINSTANCE instance = GetModuleHandleW(NULL);
    HMENU menu;
    HWND window;
    MSG message;
    int exit_code = 2;

    output = CreateFileA("resizeprobe.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(2);

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"AbletonResizeProbe";
    RegisterClassW(&window_class);

    menu = CreateMenu();
    AppendMenuW(menu, MF_STRING, 1, L"File");
    AppendMenuW(menu, MF_STRING, 2, L"Edit");
    AppendMenuW(menu, MF_STRING, 3, L"Create");
    AppendMenuW(menu, MF_STRING, 4, L"View");
    AppendMenuW(menu, MF_STRING, 5, L"Options");
    AppendMenuW(menu, MF_STRING, 6, L"Help");

    window = CreateWindowExW(0x100, window_class.lpszClassName, L"Ableton resize probe",
                             0x06cf0000 | WS_VISIBLE, 160, 120, 900, 600,
                             NULL, menu, instance, NULL);
    if (!window)
    {
        PRINT("CreateWindowExW failed error=%u\r\n", (unsigned)GetLastError());
        CloseHandle(output);
        ExitProcess(2);
    }

    SetTimer(window, 1, 900, NULL);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    exit_code = (int)message.wParam;
    DestroyWindow(window);
    CloseHandle(output);
    ExitProcess((UINT)exit_code);
}
