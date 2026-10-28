#include "PopupWindow.h"
#include "TextConverter.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <iostream>

namespace
{
    // 96 DPI 下的设计值，运行时按窗口实际 DPI 缩放
    constexpr int kMargin = 18;
    constexpr int kTopPadding = 16;
    constexpr int kLabelHeight = 20;
    constexpr int kLabelGap = 6;       // 小标题到正文
    constexpr int kBlockGap = 12;      // 正文到分隔线、分隔线到小标题
    constexpr int kBottomPadding = 16;
    constexpr int kMinOriginalHeight = 40;
    constexpr int kMaxOriginalHeight = 120;
    constexpr int kMinClientHeight = 180;
    constexpr int kMinTranslationHeight = 60;  // 拖动时译文至少保留三行

    // IDC_ARROW / IDC_SIZENS 的宽字符版本：MinGW 默认把 IDC_* 展开成 char 版本
    const LPCWSTR kArrowCursor = MAKEINTRESOURCEW(32512);
    const LPCWSTR kSizeCursor = MAKEINTRESOURCEW(32645);

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

    // 用与绘制相同的字体和宽度量出文本需要的高度
    int measureTextHeight(
        HWND hwnd,
        UINT dpi,
        const std::wstring &text,
        int width)
    {
        if (text.empty() || width <= 0)
        {
            return 0;
        }

        const HDC dc = GetDC(hwnd);

        if (dc == nullptr)
        {
            return 0;
        }

        HFONT font = createUiFont(dpi, 14);
        HGDIOBJ oldFont = SelectObject(dc, font);

        RECT rect{0, 0, width, 0};

        DrawTextW(
            dc,
            text.c_str(),
            -1,
            &rect,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);

        SelectObject(dc, oldFont);
        DeleteObject(font);
        ReleaseDC(hwnd, dc);

        return rect.bottom - rect.top;
    }

    struct Layout
    {
        int margin = 0;
        int originalLabelTop = 0;
        int originalLabelBottom = 0;
        int originalTop = 0;
        int originalHeight = 0;
        int dividerY = 0;
        int translatedLabelTop = 0;
        int translatedLabelBottom = 0;
        int translatedTop = 0;
    };

    Layout computeLayout(UINT dpi, int originalTextHeight)
    {
        Layout layout{};

        layout.margin = scaled(kMargin, dpi);
        layout.originalLabelTop = scaled(kTopPadding, dpi);
        layout.originalLabelBottom =
            layout.originalLabelTop + scaled(kLabelHeight, dpi);
        layout.originalTop =
            layout.originalLabelBottom + scaled(kLabelGap, dpi);
        layout.originalHeight = std::clamp(
            originalTextHeight,
            scaled(kMinOriginalHeight, dpi),
            scaled(kMaxOriginalHeight, dpi));
        layout.dividerY =
            layout.originalTop + layout.originalHeight + scaled(kBlockGap, dpi);
        layout.translatedLabelTop =
            layout.dividerY + scaled(kBlockGap, dpi);
        layout.translatedLabelBottom =
            layout.translatedLabelTop + scaled(kLabelHeight, dpi);
        layout.translatedTop =
            layout.translatedLabelBottom + scaled(kLabelGap, dpi);

        return layout;
    }

    // 可拖动的分隔条：一条细线加中间三个点
    void drawDivider(
        HDC hdc,
        UINT dpi,
        int margin,
        int right,
        int centerY,
        bool highlight)
    {
        HPEN pen = CreatePen(
            PS_SOLID,
            std::max(1, scaled(1, dpi)),
            highlight ? RGB(150, 150, 150) : RGB(214, 214, 214));

        HGDIOBJ oldPen = SelectObject(hdc, pen);

        MoveToEx(hdc, margin, centerY, nullptr);
        LineTo(hdc, right + 1, centerY);

        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        const int dot = std::max(3, scaled(4, dpi));
        const int gap = std::max(3, scaled(5, dpi));
        const int total = dot * 3 + gap * 2;
        const int startX = (margin + right - total) / 2;

        HBRUSH brush = CreateSolidBrush(
            highlight ? RGB(80, 80, 80) : RGB(130, 130, 130));

        HGDIOBJ oldBrush = SelectObject(hdc, brush);

        HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
        HGDIOBJ oldDotPen = SelectObject(hdc, nullPen);

        for (int i = 0; i < 3; ++i)
        {
            const int x = startX + i * (dot + gap);

            Ellipse(
                hdc,
                x,
                centerY - dot / 2,
                x + dot,
                centerY - dot / 2 + dot);
        }

        SelectObject(hdc, oldDotPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(brush);
    }

    int maxClientHeight(HWND hwnd, UINT dpi)
    {
        MONITORINFO info{};
        info.cbSize = sizeof(info);

        const HMONITOR monitor =
            MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

        if (GetMonitorInfoW(monitor, &info))
        {
            const int workHeight =
                info.rcWork.bottom - info.rcWork.top;

            return static_cast<int>(workHeight * 0.8);
        }

        return scaled(600, dpi);
    }

    // 窗口高度跟着文本走，最长不超过工作区的 80%
    int desiredClientHeight(
        HWND hwnd,
        UINT dpi,
        int clientWidth,
        const std::wstring &original,
        const std::wstring &translated)
    {
        const int contentWidth = clientWidth - 2 * scaled(kMargin, dpi);

        const int originalHeight = measureTextHeight(
            hwnd, dpi, original, contentWidth);

        const Layout layout = computeLayout(dpi, originalHeight);

        const int translatedHeight = measureTextHeight(
            hwnd, dpi, translated, contentWidth);

        int height = layout.translatedTop +
                     translatedHeight +
                     scaled(kBottomPadding, dpi);

        height = std::max(height, scaled(kMinClientHeight, dpi));
        height = std::min(height, maxClientHeight(hwnd, dpi));

        return height;
    }
}

// 注册窗口类
bool PopupWindow::initialize(HINSTANCE instance)
{
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
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

    // 每次出现新内容都回到自动分配
    splitOffset_ = 0;

    if (hwnd_ != nullptr && IsWindow(hwnd_))
    {
        // 文本变了，窗口高度也要跟着变
        resizeToContent(hwnd_);

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

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WINDOW_CLASS_NAME,
        L"TranslateCpp",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        cursor.x + 16,
        cursor.y + 16,
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

    resizeToContent(hwnd_);

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
}

// 按内容计算窗口大小，并把窗口收回当前显示器的工作区
void PopupWindow::resizeToContent(HWND hwnd)
{
    if (hwnd == nullptr || !IsWindow(hwnd))
    {
        return;
    }

    RECT windowRect{};
    RECT clientRect{};

    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);

    const int frameWidth =
        (windowRect.right - windowRect.left) - clientRect.right;
    const int frameHeight =
        (windowRect.bottom - windowRect.top) - clientRect.bottom;

    const UINT dpi = windowDpi(hwnd);
    const int clientWidth = scaled(BASE_WIDTH, dpi);

    const int clientHeight = desiredClientHeight(
        hwnd, dpi, clientWidth, originalText_, translatedText_);

    int width = clientWidth + frameWidth;
    int height = clientHeight + frameHeight;
    int x = windowRect.left;
    int y = windowRect.top;

    MONITORINFO info{};
    info.cbSize = sizeof(info);

    if (GetMonitorInfoW(
            MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
            &info))
    {
        const RECT &work = info.rcWork;

        if (x + width > work.right)
        {
            x = work.right - width;
        }

        if (y + height > work.bottom)
        {
            y = work.bottom - height;
        }

        if (x < work.left)
        {
            x = work.left;
        }

        if (y < work.top)
        {
            y = work.top;
        }
    }

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE);
}

// 分隔条中心线在客户区中的 y 坐标
int PopupWindow::dividerCenterY() const
{
    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return 0;
    }

    const UINT dpi = windowDpi(hwnd_);

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int contentWidth = client.right - 2 * scaled(kMargin, dpi);
    const int measured = measureTextHeight(
        hwnd_, dpi, originalText_, contentWidth);

    const Layout layout = computeLayout(dpi, measured);

    return layout.dividerY + splitOffset_;
}

bool PopupWindow::isOverDivider(POINT clientPoint) const
{
    if (hwnd_ == nullptr)
    {
        return false;
    }

    const UINT dpi = windowDpi(hwnd_);
    const int half = scaled(7, dpi);
    const int center = dividerCenterY();

    return clientPoint.y >= center - half &&
           clientPoint.y <= center + half;
}

// 拖动时限制上下限：原文至少两行，译文至少三行
void PopupWindow::applySplitOffset(int offset)
{
    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return;
    }

    const UINT dpi = windowDpi(hwnd_);

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int contentWidth = client.right - 2 * scaled(kMargin, dpi);
    const int measured = measureTextHeight(
        hwnd_, dpi, originalText_, contentWidth);

    const Layout layout = computeLayout(dpi, measured);

    const int natural = layout.originalHeight;
    const int minOffset = scaled(kMinOriginalHeight, dpi) - natural;

    const int available =
        (client.bottom - scaled(kBottomPadding, dpi)) -
        layout.translatedTop;

    const int maxOffset =
        available - scaled(kMinTranslationHeight, dpi) - natural;

    const int low = std::min(minOffset, maxOffset);
    const int high = std::max(minOffset, maxOffset);

    splitOffset_ = std::clamp(offset, low, high);

    InvalidateRect(hwnd_, nullptr, TRUE);
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

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);

            SetCursor(LoadCursorW(
                nullptr,
                self->isOverDivider(point) ? kSizeCursor : kArrowCursor));

            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
    {
        const POINT point{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)};

        if (self->dragging_)
        {
            self->applySplitOffset(
                self->dragStartOffset_ +
                (point.y - self->dragStart_.y));

            return 0;
        }

        const bool over = self->isOverDivider(point);

        if (over != self->dividerHover_)
        {
            self->dividerHover_ = over;
            InvalidateRect(hwnd, nullptr, TRUE);
        }

        if (!self->trackingMouse_)
        {
            TRACKMOUSEEVENT track{
                sizeof(TRACKMOUSEEVENT),
                TME_LEAVE,
                hwnd,
                0};

            if (TrackMouseEvent(&track))
            {
                self->trackingMouse_ = true;
            }
        }

        break;
    }

    case WM_MOUSELEAVE:
        self->trackingMouse_ = false;

        if (self->dividerHover_)
        {
            self->dividerHover_ = false;
            InvalidateRect(hwnd, nullptr, TRUE);
        }

        break;

    case WM_LBUTTONDOWN:
    {
        const POINT point{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)};

        if (self->isOverDivider(point))
        {
            SetCapture(hwnd);
            SetCursor(LoadCursorW(nullptr, kSizeCursor));

            self->dragging_ = true;
            self->dragStart_ = point;
            self->dragStartOffset_ = self->splitOffset_;
        }

        break;
    }

    case WM_LBUTTONUP:
        if (self->dragging_)
        {
            self->dragging_ = false;
            ReleaseCapture();
        }
        break;

    case WM_LBUTTONDBLCLK:
    {
        const POINT point{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)};

        // 双击恢复按内容自动分配
        if (self->isOverDivider(point))
        {
            self->applySplitOffset(0);
        }

        break;
    }

    case WM_CAPTURECHANGED:
        self->dragging_ = false;
        break;

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

        // 偏移量是按旧 DPI 算出来的，换屏后直接复位
        self->splitOffset_ = 0;

        self->resizeToContent(hwnd);
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

    const int contentWidth =
        clientRect.right - 2 * scaled(kMargin, dpi);

    const int measuredOriginal = measureTextHeight(
        hwnd, dpi, originalText_, contentWidth);

    Layout layout = computeLayout(dpi, measuredOriginal);

    // 拖动分隔条后，原文区变高、下面整块跟着下移
    layout.originalHeight += splitOffset_;
    layout.dividerY += splitOffset_;
    layout.translatedLabelTop += splitOffset_;
    layout.translatedLabelBottom += splitOffset_;
    layout.translatedTop += splitOffset_;

    const int margin = layout.margin;
    const int right = clientRect.right - margin;

    HFONT bodyFont = createUiFont(dpi, 14);
    HFONT labelFont = createUiFont(dpi, 12);

    HGDIOBJ oldFont = SelectObject(hdc, labelFont);

    // 原文标题
    SetTextColor(hdc, RGB(110, 110, 110));

    RECT originalLabel{
        margin,
        layout.originalLabelTop,
        right,
        layout.originalLabelBottom};

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
        layout.originalTop,
        right,
        layout.originalTop + layout.originalHeight};

    DrawTextW(
        hdc,
        originalText_.c_str(),
        -1,
        &originalRect,
        DT_LEFT | DT_WORDBREAK | DT_WORD_ELLIPSIS | DT_NOPREFIX);

    // 可拖动的分隔条
    drawDivider(
        hdc,
        dpi,
        margin,
        right,
        layout.dividerY,
        dividerHover_ || dragging_);

    // 译文标题
    SelectObject(hdc, labelFont);
    SetTextColor(hdc, RGB(110, 110, 110));

    RECT translatedLabel{
        margin,
        layout.translatedLabelTop,
        right,
        layout.translatedLabelBottom};

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
        layout.translatedTop,
        right,
        clientRect.bottom - scaled(kBottomPadding, dpi)};

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
