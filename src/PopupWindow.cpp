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
    constexpr int kGutter = 14;        // 右侧留给滚动条的宽度
    constexpr int kBarWidth = 6;
    constexpr int kTopPadding = 16;
    constexpr int kLabelHeight = 20;
    constexpr int kLabelGap = 6;
    constexpr int kBlockGap = 12;
    constexpr int kBottomPadding = 16;
    constexpr int kMinPaneText = 60;   // 每栏至少三行
    constexpr int kMinClientHeight = 180;
    constexpr int kScrollLine = 24;

    // 上下固定留白 + 两个小标题 + 分隔条两侧间距
    constexpr int kFixedHeight =
        kTopPadding + kLabelHeight + kLabelGap +
        kBlockGap + kBlockGap + kLabelHeight + kLabelGap +
        kBottomPadding;

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
        int gutter = 0;
        int contentRight = 0;

        int originalLabelTop = 0;
        int originalLabelBottom = 0;
        int originalTextTop = 0;
        int originalTextHeight = 0;

        int dividerY = 0;

        int translatedLabelTop = 0;
        int translatedLabelBottom = 0;
        int translatedTextTop = 0;
        int translatedTextHeight = 0;
    };

    Layout computeLayout(
        UINT dpi,
        int clientWidth,
        int clientHeight,
        int originalTextHeight)
    {
        Layout layout{};

        layout.margin = scaled(kMargin, dpi);
        layout.gutter = scaled(kGutter, dpi);
        layout.contentRight = clientWidth - layout.margin - layout.gutter;

        layout.originalLabelTop = scaled(kTopPadding, dpi);
        layout.originalLabelBottom =
            layout.originalLabelTop + scaled(kLabelHeight, dpi);
        layout.originalTextTop =
            layout.originalLabelBottom + scaled(kLabelGap, dpi);
        layout.originalTextHeight = std::max(
            originalTextHeight, scaled(kMinPaneText, dpi));

        layout.dividerY =
            layout.originalTextTop + layout.originalTextHeight +
            scaled(kBlockGap, dpi);

        layout.translatedLabelTop =
            layout.dividerY + scaled(kBlockGap, dpi);
        layout.translatedLabelBottom =
            layout.translatedLabelTop + scaled(kLabelHeight, dpi);
        layout.translatedTextTop =
            layout.translatedLabelBottom + scaled(kLabelGap, dpi);
        layout.translatedTextHeight = std::max(
            0,
            clientHeight - scaled(kBottomPadding, dpi) -
                layout.translatedTextTop);

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

    void fillRoundRect(HDC hdc, const RECT &rect, int radius, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
        HGDIOBJ oldPen = SelectObject(hdc, nullPen);

        RoundRect(
            hdc,
            rect.left, rect.top,
            rect.right, rect.bottom,
            radius, radius);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(brush);
    }

    void drawScrollBar(
        HDC hdc,
        UINT dpi,
        const RECT &track,
        const RECT &thumb)
    {
        if (thumb.bottom <= thumb.top)
        {
            return;
        }

        const int radius = scaled(kBarWidth, dpi);

        fillRoundRect(hdc, track, radius, RGB(244, 244, 244));
        fillRoundRect(hdc, thumb, radius, RGB(190, 190, 190));
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
    wc.hCursor = LoadCursorW(nullptr, kArrowCursor);
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

    // 新内容：两栏都回到顶部，分配恢复自动
    originalScroll_ = 0;
    translatedScroll_ = 0;
    originalAlloc_ = 0;
    activePane_ = Pane::Translated;

    if (hwnd_ != nullptr && IsWindow(hwnd_))
    {
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

// 按内容计算窗口大小，并给两栏做默认分配
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
    const int contentWidth =
        clientWidth - 2 * scaled(kMargin, dpi) - scaled(kGutter, dpi);

    const int measuredOriginal = measureTextHeight(
        hwnd, dpi, originalText_, contentWidth);

    const int measuredTranslated = measureTextHeight(
        hwnd, dpi, translatedText_, contentWidth);

    const int naturalOriginal = std::max(
        measuredOriginal, scaled(kMinPaneText, dpi));

    const int naturalTranslated = std::max(
        measuredTranslated, scaled(kMinPaneText, dpi));

    const int fixedHeight = scaled(kFixedHeight, dpi);
    const int maxHeight = maxClientHeight(hwnd, dpi);

    int desired = fixedHeight + naturalOriginal + naturalTranslated;
    int alloc = naturalOriginal;
    int clientHeight = desired;

    if (desired > maxHeight)
    {
        clientHeight = maxHeight;

        const int available = clientHeight - fixedHeight;
        const int total = naturalOriginal + naturalTranslated;

        alloc = available * naturalOriginal / std::max(1, total);

        const int minPane = scaled(kMinPaneText, dpi);

        alloc = std::clamp(
            alloc,
            minPane,
            std::max(minPane, available - minPane));
    }

    clientHeight = std::max(clientHeight, scaled(kMinClientHeight, dpi));
    originalAlloc_ = alloc;

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

    clampScrolls();
}

int PopupWindow::dividerCenterY() const
{
    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return 0;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const Layout layout = computeLayout(
        windowDpi(hwnd_), client.right, client.bottom, originalAlloc_);

    return layout.dividerY;
}

RECT PopupWindow::paneTextRect(Pane pane) const
{
    RECT client{};

    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return client;
    }

    GetClientRect(hwnd_, &client);

    const Layout layout = computeLayout(
        windowDpi(hwnd_), client.right, client.bottom, originalAlloc_);

    if (pane == Pane::Original)
    {
        return RECT{
            layout.margin,
            layout.originalTextTop,
            layout.contentRight,
            layout.originalTextTop + layout.originalTextHeight};
    }

    return RECT{
        layout.margin,
        layout.translatedTextTop,
        layout.contentRight,
        layout.translatedTextTop + layout.translatedTextHeight};
}

int PopupWindow::paneContentHeight(Pane pane) const
{
    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return 0;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int contentWidth =
        client.right - 2 * scaled(kMargin, windowDpi(hwnd_)) -
        scaled(kGutter, windowDpi(hwnd_));

    return measureTextHeight(
        hwnd_,
        windowDpi(hwnd_),
        pane == Pane::Original ? originalText_ : translatedText_,
        contentWidth);
}

int PopupWindow::paneTextHeight(Pane pane) const
{
    const RECT rect = paneTextRect(pane);

    return rect.bottom - rect.top;
}

int PopupWindow::paneScrollMax(Pane pane) const
{
    return std::max(0, paneContentHeight(pane) - paneTextHeight(pane));
}

int &PopupWindow::scrollOf(Pane pane)
{
    return pane == Pane::Original ? originalScroll_ : translatedScroll_;
}

RECT PopupWindow::trackRect(Pane pane) const
{
    const RECT text = paneTextRect(pane);

    if (hwnd_ == nullptr)
    {
        return RECT{};
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const UINT dpi = windowDpi(hwnd_);
    const int barRight = client.right - scaled(kMargin, dpi);
    const int barLeft = barRight - scaled(kBarWidth, dpi);

    return RECT{barLeft, text.top, barRight, text.bottom};
}

RECT PopupWindow::thumbRect(Pane pane) const
{
    const int maxScroll = paneScrollMax(pane);

    if (maxScroll <= 0)
    {
        return RECT{};
    }

    const RECT track = trackRect(pane);
    const int trackHeight = track.bottom - track.top;
    const int contentHeight = paneContentHeight(pane);
    const int textHeight = paneTextHeight(pane);

    if (trackHeight <= 0 || contentHeight <= 0)
    {
        return RECT{};
    }

    const UINT dpi = windowDpi(hwnd_);
    const int minThumb = scaled(24, dpi);

    int thumbHeight = trackHeight * textHeight / contentHeight;
    thumbHeight = std::max(minThumb, thumbHeight);
    thumbHeight = std::min(trackHeight, thumbHeight);

    const int travel = trackHeight - thumbHeight;
    const int offset =
        pane == Pane::Original ? originalScroll_ : translatedScroll_;

    const int thumbTop =
        track.top + travel * offset / std::max(1, maxScroll);

    return RECT{
        track.left,
        thumbTop,
        track.right,
        thumbTop + thumbHeight};
}

bool PopupWindow::isOverDivider(POINT clientPoint) const
{
    if (hwnd_ == nullptr)
    {
        return false;
    }

    const int half = scaled(7, windowDpi(hwnd_));
    const int center = dividerCenterY();

    return clientPoint.y >= center - half &&
           clientPoint.y <= center + half;
}

bool PopupWindow::isOverThumb(Pane pane, POINT clientPoint) const
{
    const RECT thumb = thumbRect(pane);

    return thumb.bottom > thumb.top &&
           PtInRect(&thumb, clientPoint) != FALSE;
}

bool PopupWindow::isOverTrack(Pane pane, POINT clientPoint) const
{
    if (paneScrollMax(pane) <= 0)
    {
        return false;
    }

    const RECT track = trackRect(pane);

    return PtInRect(&track, clientPoint) != FALSE;
}

PopupWindow::Pane PopupWindow::paneAt(POINT clientPoint) const
{
    return clientPoint.y < dividerCenterY()
               ? Pane::Original
               : Pane::Translated;
}

void PopupWindow::clampScrolls()
{
    for (const Pane pane : {Pane::Original, Pane::Translated})
    {
        int &scroll = scrollOf(pane);
        scroll = std::clamp(scroll, 0, paneScrollMax(pane));
    }
}

void PopupWindow::scrollPaneBy(Pane pane, int delta)
{
    int &scroll = scrollOf(pane);

    const int target = std::clamp(
        scroll + delta, 0, paneScrollMax(pane));

    if (target == scroll)
    {
        return;
    }

    scroll = target;
    InvalidateRect(hwnd_, nullptr, TRUE);
}

// 拖动只调整两栏的分配，窗口总高度不变
void PopupWindow::applySplitOffset(int offset)
{
    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const UINT dpi = windowDpi(hwnd_);
    const int minPane = scaled(kMinPaneText, dpi);

    const int maxAlloc =
        client.bottom - scaled(kFixedHeight, dpi) - minPane;

    originalAlloc_ = std::clamp(
        offset,
        minPane,
        std::max(minPane, maxAlloc));

    clampScrolls();
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
        return 1;

    case WM_MOUSEWHEEL:
    {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);

        const UINT dpi = windowDpi(hwnd);
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);

        self->scrollPaneBy(
            self->paneAt(point),
            -delta / WHEEL_DELTA * scaled(kScrollLine, dpi) * 3);

        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);

            const bool draggable =
                self->isOverDivider(point) ||
                self->isOverThumb(Pane::Original, point) ||
                self->isOverThumb(Pane::Translated, point);

            SetCursor(LoadCursorW(
                nullptr,
                draggable ? kSizeCursor : kArrowCursor));

            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
    {
        const POINT point{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)};

        if (self->draggingDivider_)
        {
            self->applySplitOffset(
                self->dividerDragStartAlloc_ +
                (point.y - self->dividerDragStartY_));

            return 0;
        }

        if (self->draggingThumb_)
        {
            const Pane pane = self->thumbPane_;
            const RECT track = self->trackRect(pane);
            const RECT thumb = self->thumbRect(pane);

            const int travel =
                (track.bottom - track.top) - (thumb.bottom - thumb.top);

            const int maxScroll = self->paneScrollMax(pane);

            if (travel > 0 && maxScroll > 0)
            {
                const int delta = point.y - self->thumbDragStartY_;

                int &scroll = self->scrollOf(pane);

                scroll = std::clamp(
                    self->thumbDragStartScroll_ +
                        delta * maxScroll / travel,
                    0,
                    maxScroll);

                InvalidateRect(hwnd, nullptr, TRUE);
            }

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

            self->draggingDivider_ = true;
            self->dividerDragStartY_ = point.y;
            self->dividerDragStartAlloc_ = self->originalAlloc_;
            return 0;
        }

        for (const Pane pane : {Pane::Original, Pane::Translated})
        {
            if (self->isOverThumb(pane, point))
            {
                SetCapture(hwnd);

                self->draggingThumb_ = true;
                self->thumbPane_ = pane;
                self->thumbDragStartY_ = point.y;
                self->thumbDragStartScroll_ = self->scrollOf(pane);
                return 0;
            }
        }

        for (const Pane pane : {Pane::Original, Pane::Translated})
        {
            if (self->isOverTrack(pane, point))
            {
                const RECT text = self->paneTextRect(pane);

                self->scrollPaneBy(
                    pane,
                    point.y < self->thumbRect(pane).top
                        ? -(text.bottom - text.top)
                        : (text.bottom - text.top));

                return 0;
            }
        }

        self->activePane_ = self->paneAt(point);
        break;
    }

    case WM_LBUTTONUP:
        self->draggingDivider_ = false;
        self->draggingThumb_ = false;
        ReleaseCapture();
        break;

    case WM_LBUTTONDBLCLK:
    {
        const POINT point{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)};

        if (self->isOverDivider(point))
        {
            // 双击恢复默认分配
            self->resizeToContent(hwnd);
        }

        break;
    }

    case WM_CAPTURECHANGED:
        self->draggingDivider_ = false;
        self->draggingThumb_ = false;
        break;

    case WM_KEYDOWN:
    {
        const UINT dpi = windowDpi(hwnd);
        const int line = scaled(kScrollLine, dpi);

        int page = 0;

        switch (wParam)
        {
        case VK_ESCAPE:
            DestroyWindow(hwnd);
            return 0;

        case VK_UP:
            self->scrollPaneBy(self->activePane_, -line);
            return 0;

        case VK_DOWN:
            self->scrollPaneBy(self->activePane_, line);
            return 0;

        case VK_PRIOR:
            page = self->paneTextHeight(self->activePane_) - line;
            self->scrollPaneBy(self->activePane_, -page);
            return 0;

        case VK_NEXT:
            page = self->paneTextHeight(self->activePane_) - line;
            self->scrollPaneBy(self->activePane_, page);
            return 0;

        case VK_HOME:
            self->scrollPaneBy(self->activePane_, -1000000);
            return 0;

        case VK_END:
            self->scrollPaneBy(self->activePane_, 1000000);
            return 0;

        default:
            break;
        }

        break;
    }

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

        self->originalScroll_ = 0;
        self->translatedScroll_ = 0;
        self->originalAlloc_ = 0;

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

    HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdc, &clientRect, background);
    DeleteObject(background);

    SetBkMode(hdc, TRANSPARENT);

    const UINT dpi = windowDpi(hwnd);

    const Layout layout = computeLayout(
        dpi, clientRect.right, clientRect.bottom, originalAlloc_);

    HFONT bodyFont = createUiFont(dpi, 14);
    HFONT labelFont = createUiFont(dpi, 12);

    HGDIOBJ oldFont = SelectObject(hdc, labelFont);

    const int margin = layout.margin;
    const int right = layout.contentRight;

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

    // 译文标题
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

    SelectObject(hdc, bodyFont);

    // 两栏正文各自裁剪、各自滚动
    const auto drawPaneText =
        [&](const std::wstring &text, const RECT &rect, int scroll,
            COLORREF color)
    {
        const int contentHeight = measureTextHeight(
            hwnd, dpi, text, rect.right - rect.left);

        const int saved = SaveDC(hdc);

        IntersectClipRect(hdc, rect.left, rect.top, rect.right, rect.bottom);

        SetTextColor(hdc, color);

        RECT textRect{
            rect.left,
            rect.top - scroll,
            rect.right,
            rect.top - scroll + contentHeight};

        DrawTextW(
            hdc,
            text.c_str(),
            -1,
            &textRect,
            DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

        RestoreDC(hdc, saved);
    };

    const RECT originalRect{
        margin,
        layout.originalTextTop,
        right,
        layout.originalTextTop + layout.originalTextHeight};

    drawPaneText(
        originalText_, originalRect, originalScroll_, RGB(70, 70, 70));

    const RECT translatedRect{
        margin,
        layout.translatedTextTop,
        right,
        layout.translatedTextTop + layout.translatedTextHeight};

    drawPaneText(
        translatedText_,
        translatedRect,
        translatedScroll_,
        isError_ ? RGB(190, 45, 45) : RGB(25, 25, 25));

    // 分隔条
    drawDivider(
        hdc,
        dpi,
        margin,
        right,
        layout.dividerY,
        dividerHover_ || draggingDivider_);

    // 两栏的滚动条
    for (const Pane pane : {Pane::Original, Pane::Translated})
    {
        const RECT track = trackRect(pane);
        const RECT thumb = thumbRect(pane);

        drawScrollBar(hdc, dpi, track, thumb);
    }

    SelectObject(hdc, oldFont);

    DeleteObject(bodyFont);
    DeleteObject(labelFont);

    EndPaint(hwnd, &ps);
}
