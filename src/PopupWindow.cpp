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
    constexpr int kGutter = 14;        // 译文区右侧留给滚动条的宽度
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

    // 编辑框自带滚动条的宽度，量原文高度时要扣掉
    constexpr int kEditScrollBar = 17;

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
        int editRight = 0;

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
        layout.editRight = clientWidth - layout.margin;

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

void PopupWindow::setOnRetranslate(
    std::function<void(const std::string &)> callback)
{
    onRetranslate_ = std::move(callback);
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

    // 新内容：译文回到顶部，分配恢复自动
    translatedScroll_ = 0;
    originalAlloc_ = 0;

    if (hwnd_ != nullptr && IsWindow(hwnd_))
    {
        syncEditText();

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

    createEditControl();
    syncEditText();

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

    // 原文用编辑框，宽度要扣掉它自己的滚动条
    const int originalWidth =
        clientWidth - 2 * scaled(kMargin, dpi) -
        scaled(kEditScrollBar, dpi);

    const int translatedWidth =
        clientWidth - 2 * scaled(kMargin, dpi) - scaled(kGutter, dpi);

    const int measuredOriginal = measureTextHeight(
        hwnd, dpi, originalText_, originalWidth);

    const int measuredTranslated = measureTextHeight(
        hwnd, dpi, translatedText_, translatedWidth);

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

    positionEditControl();
    clampScroll();
}

void PopupWindow::createEditControl()
{
    if (edit_ != nullptr)
    {
        return;
    }

    edit_ = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE |
            ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | ES_NOHIDESEL,
        0, 0, 10, 10,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    if (edit_ == nullptr)
    {
        return;
    }

    // 与翻译接口的长度上限保持一致
    SendMessageW(edit_, EM_SETLIMITTEXT, 3000, 0);

    // 去掉编辑框默认的左右内边距，让文字和标题对齐
    SendMessageW(edit_, EM_SETMARGINS, 0x0003, 0);

    SetWindowLongPtrW(
        edit_,
        GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(this));

    editDefaultProc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(
            edit_,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(editWindowProc)));

    editFontDpi_ = 0;
    updateEditFont(windowDpi(hwnd_));
}

void PopupWindow::updateEditFont(UINT dpi)
{
    if (edit_ == nullptr || editFontDpi_ == dpi)
    {
        return;
    }

    if (editFont_ != nullptr)
    {
        DeleteObject(editFont_);
        editFont_ = nullptr;
    }

    editFont_ = createUiFont(dpi, 14);
    editFontDpi_ = dpi;

    SendMessageW(
        edit_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(editFont_),
        TRUE);
}

void PopupWindow::positionEditControl()
{
    if (edit_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const UINT dpi = windowDpi(hwnd_);
    updateEditFont(dpi);

    const Layout layout = computeLayout(
        dpi, client.right, client.bottom, originalAlloc_);

    MoveWindow(
        edit_,
        layout.margin,
        layout.originalTextTop,
        layout.editRight - layout.margin,
        layout.originalTextHeight,
        TRUE);

    updateEditScrollBar();
}

void PopupWindow::syncEditText()
{
    if (edit_ == nullptr)
    {
        return;
    }

    const int length = GetWindowTextLengthW(edit_);

    std::wstring current(static_cast<std::size_t>(length), L'\0');

    if (length > 0)
    {
        GetWindowTextW(edit_, current.data(), length + 1);
    }

    // 内容一样就不要重设，避免打断用户正在编辑的光标位置
    if (current == originalText_)
    {
        return;
    }

    SendMessageW(
        edit_,
        WM_SETTEXT,
        0,
        reinterpret_cast<LPARAM>(originalText_.c_str()));
}

std::wstring PopupWindow::editTextWide() const
{
    if (edit_ == nullptr)
    {
        return {};
    }

    const int length = GetWindowTextLengthW(edit_);

    if (length <= 0)
    {
        return {};
    }

    std::wstring text(static_cast<std::size_t>(length), L'\0');

    GetWindowTextW(edit_, text.data(), length + 1);

    return text;
}

std::string PopupWindow::editTextUtf8() const
{
    const std::wstring text = editTextWide();

    if (text.empty())
    {
        return {};
    }

    return wideToUtf8(text);
}

// 文本放得下就收起滚动条，避免短文本旁边挂一条空滚动条
void PopupWindow::updateEditScrollBar()
{
    if (edit_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }

    RECT rect{};
    GetWindowRect(edit_, &rect);

    const UINT dpi = windowDpi(hwnd_);
    const int height = rect.bottom - rect.top;
    const int width =
        rect.right - rect.left - scaled(kEditScrollBar, dpi);

    const int content = measureTextHeight(
        hwnd_, dpi, editTextWide(), width);

    ShowScrollBar(edit_, SB_VERT, content > height);
}

void PopupWindow::requestRetranslate()
{
    const std::string text = editTextUtf8();

    if (text.empty() || !onRetranslate_)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    onRetranslate_(text);
}

// 编辑框子类化：Ctrl+Enter 重新翻译，其它消息交回给 EDIT
LRESULT CALLBACK PopupWindow::editWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    auto *self = reinterpret_cast<PopupWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self != nullptr &&
        message == WM_KEYDOWN &&
        wParam == VK_RETURN)
    {
        const bool ctrlPressed =
            (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
            (GetKeyState(VK_RCONTROL) & 0x8000) != 0;

        if (ctrlPressed)
        {
            self->requestRetranslate();
            return 0;
        }
    }

    if (self != nullptr && self->editDefaultProc_ != nullptr)
    {
        return CallWindowProcW(
            self->editDefaultProc_, hwnd, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
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

RECT PopupWindow::translationTextRect() const
{
    RECT client{};

    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return client;
    }

    GetClientRect(hwnd_, &client);

    const Layout layout = computeLayout(
        windowDpi(hwnd_), client.right, client.bottom, originalAlloc_);

    return RECT{
        layout.margin,
        layout.translatedTextTop,
        layout.contentRight,
        layout.translatedTextTop + layout.translatedTextHeight};
}

int PopupWindow::translationContentHeight() const
{
    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        return 0;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const UINT dpi = windowDpi(hwnd_);
    const int width =
        client.right - 2 * scaled(kMargin, dpi) - scaled(kGutter, dpi);

    return measureTextHeight(hwnd_, dpi, translatedText_, width);
}

int PopupWindow::translationScrollMax() const
{
    const RECT rect = translationTextRect();

    return std::max(
        0,
        translationContentHeight() -
            static_cast<int>(rect.bottom - rect.top));
}

RECT PopupWindow::translationTrackRect() const
{
    const RECT text = translationTextRect();

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

RECT PopupWindow::translationThumbRect() const
{
    const int maxScroll = translationScrollMax();

    if (maxScroll <= 0)
    {
        return RECT{};
    }

    const RECT track = translationTrackRect();
    const int trackHeight = track.bottom - track.top;
    const int contentHeight = translationContentHeight();
    const RECT text = translationTextRect();
    const int textHeight = text.bottom - text.top;

    if (trackHeight <= 0 || contentHeight <= 0 || textHeight <= 0)
    {
        return RECT{};
    }

    const UINT dpi = windowDpi(hwnd_);
    const int minThumb = scaled(24, dpi);

    int thumbHeight = trackHeight * textHeight / contentHeight;
    thumbHeight = std::clamp(thumbHeight, std::min(minThumb, trackHeight), trackHeight);

    const int travel = trackHeight - thumbHeight;
    const int thumbTop =
        track.top + travel * translatedScroll_ / std::max(1, maxScroll);

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

bool PopupWindow::isOverThumb(POINT clientPoint) const
{
    const RECT thumb = translationThumbRect();

    return thumb.bottom > thumb.top &&
           PtInRect(&thumb, clientPoint) != FALSE;
}

bool PopupWindow::isOverTrack(POINT clientPoint) const
{
    if (translationScrollMax() <= 0)
    {
        return false;
    }

    const RECT track = translationTrackRect();

    return PtInRect(&track, clientPoint) != FALSE;
}

void PopupWindow::clampScroll()
{
    translatedScroll_ = std::clamp(
        translatedScroll_, 0, translationScrollMax());
}

void PopupWindow::scrollTranslationBy(int delta)
{
    const int target = std::clamp(
        translatedScroll_ + delta, 0, translationScrollMax());

    if (target == translatedScroll_)
    {
        return;
    }

    translatedScroll_ = target;
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
        static_cast<int>(client.bottom) -
        scaled(kFixedHeight, dpi) - minPane;

    originalAlloc_ = std::clamp(
        offset,
        minPane,
        std::max(minPane, maxAlloc));

    clampScroll();

    positionEditControl();
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
    edit_ = nullptr;
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

    case WM_COMMAND:
        // 用户修改原文时，按需显示编辑框的滚动条
        if (HIWORD(wParam) == EN_CHANGE &&
            reinterpret_cast<HWND>(lParam) == self->edit_)
        {
            self->updateEditScrollBar();
        }
        break;

    case WM_CTLCOLOREDIT:
    {
        const HDC dc = reinterpret_cast<HDC>(wParam);

        SetTextColor(dc, RGB(70, 70, 70));
        SetBkColor(dc, RGB(255, 255, 255));

        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }

    case WM_MOUSEWHEEL:
    {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);

        // 原文区的滚轮由编辑框自己处理，这里只管译文区
        if (point.y < self->dividerCenterY())
        {
            break;
        }

        const UINT dpi = windowDpi(hwnd);
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);

        self->scrollTranslationBy(
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
                self->isOverDivider(point) || self->isOverThumb(point);

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
            const RECT track = self->translationTrackRect();
            const RECT thumb = self->translationThumbRect();

            const int travel =
                (track.bottom - track.top) - (thumb.bottom - thumb.top);

            const int maxScroll = self->translationScrollMax();

            if (travel > 0 && maxScroll > 0)
            {
                const int delta = point.y - self->thumbDragStartY_;

                self->translatedScroll_ = std::clamp(
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

        if (self->isOverThumb(point))
        {
            SetCapture(hwnd);

            self->draggingThumb_ = true;
            self->thumbDragStartY_ = point.y;
            self->thumbDragStartScroll_ = self->translatedScroll_;
            return 0;
        }

        if (self->isOverTrack(point))
        {
            const RECT text = self->translationTextRect();

            self->scrollTranslationBy(
                point.y < self->translationThumbRect().top
                    ? -(text.bottom - text.top)
                    : (text.bottom - text.top));

            return 0;
        }

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

        switch (wParam)
        {
        case VK_ESCAPE:
            DestroyWindow(hwnd);
            return 0;

        case VK_UP:
            self->scrollTranslationBy(-line);
            return 0;

        case VK_DOWN:
            self->scrollTranslationBy(line);
            return 0;

        case VK_PRIOR:
        {
            const RECT text = self->translationTextRect();

            self->scrollTranslationBy(-(text.bottom - text.top - line));
            return 0;
        }

        case VK_NEXT:
        {
            const RECT text = self->translationTextRect();

            self->scrollTranslationBy(text.bottom - text.top - line);
            return 0;
        }

        case VK_HOME:
            self->scrollTranslationBy(-1000000);
            return 0;

        case VK_END:
            self->scrollTranslationBy(1000000);
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
            self->edit_ = nullptr;
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

    // 提示：原文可直接编辑
    SetTextColor(hdc, RGB(165, 165, 165));

    DrawTextW(
        hdc,
        L"可直接编辑 · Ctrl+Enter 重新翻译",
        -1,
        &originalLabel,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 译文标题
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

    SelectObject(hdc, bodyFont);

    // 译文正文：按滚动位置裁剪绘制
    const RECT translatedRect{
        margin,
        layout.translatedTextTop,
        right,
        layout.translatedTextTop + layout.translatedTextHeight};

    const int contentHeight = translationContentHeight();

    const int saved = SaveDC(hdc);

    IntersectClipRect(
        hdc,
        translatedRect.left,
        translatedRect.top,
        translatedRect.right,
        translatedRect.bottom);

    SetTextColor(
        hdc,
        isError_ ? RGB(190, 45, 45) : RGB(25, 25, 25));

    RECT textRect{
        translatedRect.left,
        translatedRect.top - translatedScroll_,
        translatedRect.right,
        translatedRect.top - translatedScroll_ + contentHeight};

    DrawTextW(
        hdc,
        translatedText_.c_str(),
        -1,
        &textRect,
        DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    RestoreDC(hdc, saved);

    // 分隔条
    drawDivider(
        hdc,
        dpi,
        margin,
        right,
        layout.dividerY,
        dividerHover_ || draggingDivider_);

    // 译文的滚动条
    drawScrollBar(
        hdc,
        dpi,
        translationTrackRect(),
        translationThumbRect());

    SelectObject(hdc, oldFont);

    DeleteObject(bodyFont);
    DeleteObject(labelFont);

    EndPaint(hwnd, &ps);
}
