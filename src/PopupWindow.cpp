#include "PopupWindow.h"
#include "TextConverter.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>

namespace
{
    // 96 DPI 下的设计值按窗口实际 DPI 等比缩放
    int scaled(int value, UINT dpi)
    {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    UINT windowDpi(HWND hwnd)
    {
        const HDC dc = GetDC(hwnd);

        if (dc == nullptr)
        {
            return 96;
        }

        const int dpi = GetDeviceCaps(dc, LOGPIXELSX);

        ReleaseDC(hwnd, dc);

        return dpi > 0 ? static_cast<UINT>(dpi) : 96;
    }

    HFONT createUiFont(UINT dpi, int pixelSize)
    {
        return CreateFontW(
            -scaled(pixelSize, dpi),
            0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");
    }
}

// 注册窗口类
bool PopupWindow::initialize(HINSTANCE instance)
{
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PopupWindow::windowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;

    if (RegisterClassExW(&wc) != 0)
    {
        return true;
    }

    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void PopupWindow::show(
    const std::string &original,
    const std::string &translated)
{
    showInternal(original, translated, false);
}

void PopupWindow::showError(
    const std::string &original,
    const std::string &message)
{
    showInternal(original, message, true);
}

// 显示或更新悬浮窗
void PopupWindow::showInternal(
    const std::string &original,
    const std::string &translated,
    bool isError)
{
    originalText_ = utf8ToWide(original);
    translatedText_ = utf8ToWide(translated);
    isError_ = isError;

    if (hwnd_ != nullptr && IsWindow(hwnd_))
    {
        InvalidateRect(hwnd_, nullptr, TRUE);

        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

        SetWindowPos(
            hwnd_,
            HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE |
                SWP_NOSIZE |
                SWP_NOACTIVATE);

        return;
    }

    POINT cursor{};
    GetCursorPos(&cursor);

    int x = cursor.x + 16;
    int y = cursor.y + 16;

    HMONITOR monitor = MonitorFromPoint(
        cursor,
        MONITOR_DEFAULTTONEAREST);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    RECT workArea{};
    bool hasWorkArea = false;

    if (GetMonitorInfoW(monitor, &monitorInfo))
    {
        workArea = monitorInfo.rcWork;
        hasWorkArea = true;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WINDOW_CLASS_NAME,
        L"TranslateCpp",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        BASE_WIDTH,
        BASE_HEIGHT,
        nullptr,
        nullptr,
        instance_,
        this);

    if (hwnd_ == nullptr)
    {
        std::cerr
            << "Failed to create popup window. Error: "
            << GetLastError()
            << '\n';

        return;
    }

    // 窗口落在哪块屏幕就按那块屏幕的 DPI 放大；同时把窗口收回工作区。
    const UINT dpi = windowDpi(hwnd_);
    const int width = scaled(BASE_WIDTH, dpi);
    const int height = scaled(BASE_HEIGHT, dpi);

    if (hasWorkArea)
    {
        if (x + width > workArea.right)
        {
            x = cursor.x - width - 16;
        }

        if (y + height > workArea.bottom)
        {
            y = cursor.y - height - 16;
        }

        if (x < workArea.left)
        {
            x = workArea.left;
        }

        if (y < workArea.top)
        {
            y = workArea.top;
        }
    }

    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE);

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
}

// 关闭悬浮窗
void PopupWindow::close()
{
    if (hwnd_ != nullptr && IsWindow(hwnd_))
    {
        DestroyWindow(hwnd_);
    }

    hwnd_ = nullptr;
}

// 静态窗口消息处理函数
LRESULT CALLBACK PopupWindow::windowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    PopupWindow *self = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto *createStruct =
            reinterpret_cast<const CREATESTRUCTW *>(lParam);

        self = static_cast<PopupWindow *>(
            createStruct->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<PopupWindow *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr)
    {
        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    switch (message)
    {
    case WM_PAINT:
        self->paint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        // 绘制时整块重画，省掉一次背景擦除可以少一点闪烁
        return 1;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DPICHANGED:
    {
        const auto *suggested =
            reinterpret_cast<const RECT *>(lParam);

        SetWindowPos(
            hwnd,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_NCDESTROY:
        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            0);

        if (self->hwnd_ == hwnd)
        {
            self->hwnd_ = nullptr;
        }
        break;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam);
}

// 绘制窗口内容
void PopupWindow::paint(HWND hwnd)
{
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    HBRUSH background = CreateSolidBrush(
        RGB(255, 255, 255));

    FillRect(hdc, &clientRect, background);
    DeleteObject(background);

    SetBkMode(hdc, TRANSPARENT);

    const UINT dpi = windowDpi(hwnd);
    const int margin = scaled(18, dpi);

    HFONT bodyFont = createUiFont(dpi, 14);
    HFONT labelFont = createUiFont(dpi, 12);

    HGDIOBJ oldFont = SelectObject(hdc, labelFont);

    // 原文标题
    SetTextColor(hdc, RGB(110, 110, 110));

    RECT originalLabel{
        margin,
        scaled(16, dpi),
        clientRect.right - margin,
        scaled(36, dpi)};

    DrawTextW(
        hdc,
        L"原文",
        -1,
        &originalLabel,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 原文内容
    SelectObject(hdc, bodyFont);
    SetTextColor(hdc, RGB(70, 70, 70));

    RECT originalRect{
        margin,
        scaled(42, dpi),
        clientRect.right - margin,
        scaled(104, dpi)};

    DrawTextW(
        hdc,
        originalText_.c_str(),
        -1,
        &originalRect,
        DT_LEFT | DT_WORDBREAK | DT_WORD_ELLIPSIS | DT_NOPREFIX);

    // 分隔线
    HPEN pen = CreatePen(
        PS_SOLID,
        scaled(1, dpi),
        RGB(230, 230, 230));

    HGDIOBJ oldPen = SelectObject(hdc, pen);

    const int dividerY = scaled(116, dpi);

    MoveToEx(hdc, margin, dividerY, nullptr);
    LineTo(hdc, clientRect.right - margin, dividerY);

    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    // 译文标题
    SelectObject(hdc, labelFont);
    SetTextColor(hdc, RGB(110, 110, 110));

    RECT translatedLabel{
        margin,
        scaled(128, dpi),
        clientRect.right - margin,
        scaled(148, dpi)};

    DrawTextW(
        hdc,
        isError_ ? L"翻译失败" : L"翻译",
        -1,
        &translatedLabel,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 译文内容（失败时显示错误原因）
    SelectObject(hdc, bodyFont);
    SetTextColor(
        hdc,
        isError_ ? RGB(190, 45, 45) : RGB(25, 25, 25));

    RECT translatedRect{
        margin,
        scaled(152, dpi),
        clientRect.right - margin,
        clientRect.bottom - scaled(16, dpi)};

    DrawTextW(
        hdc,
        translatedText_.c_str(),
        -1,
        &translatedRect,
        DT_LEFT | DT_WORDBREAK | DT_WORD_ELLIPSIS | DT_NOPREFIX);

    SelectObject(hdc, oldFont);

    DeleteObject(bodyFont);
    DeleteObject(labelFont);

    EndPaint(hwnd, &ps);
}
