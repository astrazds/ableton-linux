/* Visual regression probe derived from ableton/fakeplugin.c.
 *
 * It reproduces Live's mixed-DPI VST3 editor window anatomy and JUCE-style
 * per-pixel-alpha shadow surfaces. A tester can see doubled or oversized
 * title bars and opaque black shadow rectangles without installing a plug-in.
 *
 * Output: pluginwindowprobe.txt. The windows close after ten seconds.
 */
#include <windows.h>

static HWND main_window;
static HWND default_editor;
static HWND live_editor;
static HANDLE output;
static char buffer[768];

static void emit(const char *text)
{
    DWORD written;
    WriteFile(output, text, lstrlenA(text), &written, NULL);
}

#define PRINT(...) do { wsprintfA(buffer, __VA_ARGS__); emit(buffer); } while (0)

static void paint_label(HWND window, const char *label, COLORREF colour)
{
    PAINTSTRUCT paint;
    RECT rect;
    HBRUSH brush;
    HDC dc = BeginPaint(window, &paint);
    GetClientRect(window, &rect);
    brush = CreateSolidBrush(colour);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245, 245, 245));
    DrawTextA(dc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    EndPaint(window, &paint);
}

static LRESULT CALLBACK normal_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_PAINT)
    {
        char class_name[64] = "";
        GetClassNameA(window, class_name, sizeof(class_name));
        if (!lstrcmpA(class_name, "ProbeMain"))
            paint_label(window, "Fake Live host\nObserve the two plug-in editor title bars and the shadow samples.", RGB(55, 62, 72));
        else
            paint_label(window, "Default non-client editor\nThere should not be a doubled or enormous title bar.", RGB(54, 103, 134));
        return 0;
    }
    if (message == WM_TIMER)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_DESTROY && window == main_window)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static LRESULT CALLBACK live_nonclient_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCALCSIZE)
    {
        RECT *rect = wparam ? &((NCCALCSIZE_PARAMS *)lparam)->rgrc[0] : (RECT *)lparam;
        rect->left += 3;
        rect->right -= 3;
        rect->bottom -= 3;
        rect->top += 58;
        return 0;
    }
    if (message == WM_PAINT)
    {
        paint_label(window, "Live-like custom non-client editor\nOne native title bar; no second Win32 caption.", RGB(104, 73, 126));
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static HWND create_shadow(HINSTANCE instance, int x, int y, int reverse)
{
    const int width = 260;
    const int height = 100;
    BITMAPINFO bitmap_info;
    void *bits = NULL;
    HDC screen;
    HDC memory;
    HBITMAP bitmap;
    HGDIOBJ previous;
    HWND window;
    POINT source = {0, 0};
    POINT destination = {x, y};
    SIZE size = {width, height};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    int px, py;

    window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"ProbeShadow", L"", WS_POPUP | WS_VISIBLE,
        x, y, width, height, main_window, NULL, instance, NULL);

    memset(&bitmap_info, 0, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(memory, &bitmap_info, DIB_RGB_COLORS, &bits, NULL, 0);
    previous = SelectObject(memory, bitmap);

    for (py = 0; py < height; py++)
    {
        for (px = 0; px < width; px++)
        {
            BYTE alpha = (BYTE)(30 + (190 * (reverse ? width - 1 - px : px)) / width);
            ((DWORD *)bits)[py * width + px] = ((DWORD)alpha << 24);
        }
    }

    UpdateLayeredWindow(window, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
    return window;
}

static void report_window(const char *name, HWND window)
{
    RECT outer = {0};
    RECT client = {0};
    POINT origin = {0, 0};
    GetWindowRect(window, &outer);
    GetClientRect(window, &client);
    ClientToScreen(window, &origin);
    PRINT("%s hwnd=%p dpi=%u outer=%dx%d client=%dx%d top_nc=%d\r\n",
          name, window, GetDpiForWindow(window),
          (int)(outer.right - outer.left), (int)(outer.bottom - outer.top),
          (int)(client.right - client.left), (int)(client.bottom - client.top),
          (int)(origin.y - outer.top));
}

void mainCRTStartup(void)
{
    WNDCLASSW window_class;
    HINSTANCE instance = GetModuleHandleW(NULL);
    DPI_AWARENESS_CONTEXT previous_context;
    MSG message;

    output = CreateFileA("pluginwindowprobe.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(2);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    memset(&window_class, 0, sizeof(window_class));
    window_class.lpfnWndProc = normal_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    window_class.lpszClassName = L"ProbeMain";
    RegisterClassW(&window_class);
    window_class.lpszClassName = L"ProbeEditorDefault";
    RegisterClassW(&window_class);
    window_class.lpfnWndProc = live_nonclient_proc;
    window_class.lpszClassName = L"ProbeEditorLive";
    RegisterClassW(&window_class);
    window_class.lpfnWndProc = normal_proc;
    window_class.lpszClassName = L"ProbeShadow";
    RegisterClassW(&window_class);

    main_window = CreateWindowExW(
        0, L"ProbeMain", L"Live-compatible window probe",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        80, 80, 960, 620, NULL, NULL, instance, NULL);

    previous_context = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    default_editor = CreateWindowExW(
        WS_EX_TOOLWINDOW, L"ProbeEditorDefault", L"VST3 editor: default NC",
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VISIBLE,
        130, 180, 390, 260, main_window, NULL, instance, NULL);
    live_editor = CreateWindowExW(
        WS_EX_TOOLWINDOW, L"ProbeEditorLive", L"VST3 editor: Live-like NC",
        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VISIBLE,
        570, 180, 390, 260, main_window, NULL, instance, NULL);
    create_shadow(instance, 160, 500, 0);
    SetThreadDpiAwarenessContext(previous_context);
    create_shadow(instance, 610, 500, 1);

    report_window("main", main_window);
    report_window("default_editor", default_editor);
    report_window("live_editor", live_editor);
    PRINT("Two alpha-gradient shadow samples should be translucent, not opaque black.\r\n");

    SetTimer(main_window, 1, 10000, NULL);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(output);
    ExitProcess(0);
}
