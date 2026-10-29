#include "VocabularyWindow.h"
#include "TextConverter.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace
{
    constexpr int ID_SEARCH = 2001;
    constexpr int ID_LIST = 2002;
    constexpr int ID_COPY = 2003;
    constexpr int ID_DELETE = 2004;
    constexpr int ID_CLEAR = 2005;
    constexpr int ID_CLOSE = 2006;
    constexpr int ID_SEARCH_LABEL = 2007;

    constexpr int kMargin = 12;
    constexpr int kSearchHeight = 26;
    constexpr int kButtonWidth = 84;
    constexpr int kButtonHeight = 30;

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

    void copyToClipboard(HWND owner, const std::wstring &text)
    {
        if (!OpenClipboard(owner))
        {
            return;
        }

        EmptyClipboard();

        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);

        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);

        if (memory != nullptr)
        {
            void *target = GlobalLock(memory);

            if (target != nullptr)
            {
                std::memcpy(target, text.c_str(), bytes);
                GlobalUnlock(memory);

                SetClipboardData(CF_UNICODETEXT, memory);
            }
            else
            {
                GlobalFree(memory);
            }
        }

        CloseClipboard();
    }

    std::wstring toWide(const std::string &text)
    {
        return utf8ToWide(text);
    }
}

bool VocabularyWindow::initialize(HINSTANCE instance)
{
    instance_ = instance;

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES;

    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = VocabularyWindow::windowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));

    if (RegisterClassExW(&wc) != 0)
    {
        return true;
    }

    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void VocabularyWindow::show()
{
    if (hwnd_ == nullptr || !IsWindow(hwnd_))
    {
        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            WINDOW_CLASS_NAME,
            L"生词本",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            scaled(760, 96), scaled(520, 96),
            nullptr,
            nullptr,
            instance_,
            this);

        if (hwnd_ == nullptr)
        {
            return;
        }

        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
        return;
    }

    reload();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
}

void VocabularyWindow::close()
{
    if (hwnd_ != nullptr && IsWindow(hwnd_))
    {
        DestroyWindow(hwnd_);
    }

    hwnd_ = nullptr;
}

void VocabularyWindow::onCreate()
{
    dpi_ = windowDpi(hwnd_);

    font_ = CreateFontW(
        -scaled(14, dpi_), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI");

    const DWORD commonStyle = WS_CHILD | WS_VISIBLE;

    search_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        commonStyle | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(ID_SEARCH), instance_, nullptr);

    CreateWindowExW(
        0, L"STATIC", L"搜索：",
        commonStyle | SS_LEFT,
        0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(ID_SEARCH_LABEL), instance_, nullptr);

    list_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        commonStyle | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(ID_LIST), instance_, nullptr);

    ListView_SetExtendedListViewStyle(
        list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    column.iSubItem = 0;
    column.cx = scaled(300, dpi_);
    column.pszText = const_cast<wchar_t *>(L"原文");
    ListView_InsertColumn(list_, 0, &column);

    column.iSubItem = 1;
    column.cx = scaled(300, dpi_);
    column.pszText = const_cast<wchar_t *>(L"译文");
    ListView_InsertColumn(list_, 1, &column);

    column.iSubItem = 2;
    column.cx = scaled(130, dpi_);
    column.pszText = const_cast<wchar_t *>(L"收藏时间");
    ListView_InsertColumn(list_, 2, &column);

    copyButton_ = CreateWindowExW(
        0, L"BUTTON", L"复制",
        commonStyle | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(ID_COPY), instance_, nullptr);

    deleteButton_ = CreateWindowExW(
        0, L"BUTTON", L"删除",
        commonStyle | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(ID_DELETE), instance_, nullptr);

    clearButton_ = CreateWindowExW(
        0, L"BUTTON", L"清空",
        commonStyle | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(ID_CLEAR), instance_, nullptr);

    closeButton_ = CreateWindowExW(
        0, L"BUTTON", L"关闭",
        commonStyle | BS_PUSHBUTTON,
        0, 0, 0, 0,
        hwnd_, reinterpret_cast<HMENU>(ID_CLOSE), instance_, nullptr);

    for (HWND control : {search_, list_, copyButton_,
                         deleteButton_, clearButton_, closeButton_,
                         GetDlgItem(hwnd_, ID_SEARCH_LABEL)})
    {
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font_),
            TRUE);
    }

    // 窗口按当前显示器 DPI 调整大小
    SetWindowPos(
        hwnd_,
        nullptr,
        0, 0,
        scaled(760, dpi_),
        scaled(520, dpi_),
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    reload();
}

void VocabularyWindow::layout()
{
    if (hwnd_ == nullptr)
    {
        return;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int margin = scaled(kMargin, dpi_);
    const int searchHeight = scaled(kSearchHeight, dpi_);
    const int buttonWidth = scaled(kButtonWidth, dpi_);
    const int buttonHeight = scaled(kButtonHeight, dpi_);
    const int labelWidth = scaled(52, dpi_);

    const int width = client.right;

    MoveWindow(
        search_,
        margin + labelWidth,
        margin,
        width - 2 * margin - labelWidth,
        searchHeight,
        TRUE);

    MoveWindow(
        GetDlgItem(hwnd_, ID_SEARCH_LABEL),
        margin,
        margin + searchHeight / 4,
        labelWidth,
        searchHeight,
        TRUE);

    const int listTop = margin * 2 + searchHeight;
    const int listBottom = client.bottom - margin - buttonHeight - margin;

    MoveWindow(
        list_,
        margin,
        listTop,
        width - 2 * margin,
        std::max(0, listBottom - listTop),
        TRUE);

    // 列宽跟着窗口走：时间和固定，剩下两列平分
    const int listWidth = width - 2 * margin - scaled(24, dpi_);
    const int timeWidth = scaled(130, dpi_);
    const int other = std::max(0, (listWidth - timeWidth) / 2);

    ListView_SetColumnWidth(list_, 0, other);
    ListView_SetColumnWidth(list_, 1, other);
    ListView_SetColumnWidth(list_, 2, timeWidth);

    int x = width - margin - buttonWidth;
    const int y = client.bottom - margin - buttonHeight;

    for (HWND button : {closeButton_, clearButton_,
                        deleteButton_, copyButton_})
    {
        MoveWindow(button, x, y, buttonWidth, buttonHeight, TRUE);
        x -= buttonWidth + scaled(8, dpi_);
    }
}

void VocabularyWindow::reload()
{
    entries_ = Vocabulary::load();
    applyFilter();
}

void VocabularyWindow::applyFilter()
{
    if (list_ == nullptr)
    {
        return;
    }

    wchar_t filterBuffer[128]{};
    GetWindowTextW(search_, filterBuffer, 127);

    std::wstring filter = filterBuffer;
    std::transform(
        filter.begin(), filter.end(), filter.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });

    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list_);

    int row = 0;

    for (std::size_t index = 0; index < entries_.size(); ++index)
    {
        const VocabularyEntry &entry = entries_[index];
        const std::wstring original = toWide(entry.original);
        const std::wstring translated = toWide(entry.translated);

        if (!filter.empty())
        {
            std::wstring haystack = original + L" " + translated;
            std::transform(
                haystack.begin(), haystack.end(), haystack.begin(),
                [](wchar_t ch)
                { return static_cast<wchar_t>(std::towlower(ch)); });

            if (haystack.find(filter) == std::wstring::npos)
            {
                continue;
            }
        }

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.lParam = static_cast<LPARAM>(index);
        item.pszText = const_cast<wchar_t *>(original.c_str());

        ListView_InsertItem(list_, &item);

        ListView_SetItemText(
            list_, row, 1, const_cast<wchar_t *>(translated.c_str()));

        ListView_SetItemText(
            list_, row, 2, const_cast<wchar_t *>(toWide(entry.addedAt).c_str()));

        ++row;
    }

    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);

    const std::wstring title =
        L"生词本 - " + std::to_wstring(entries_.size()) + L" 条";

    SetWindowTextW(hwnd_, title.c_str());

    updateButtons();
}

void VocabularyWindow::updateButtons()
{
    const bool hasSelection = selectedEntry() >= 0;

    EnableWindow(copyButton_, hasSelection);
    EnableWindow(deleteButton_, hasSelection);
    EnableWindow(clearButton_, entries_.empty() ? FALSE : TRUE);
}

int VocabularyWindow::selectedEntry() const
{
    if (list_ == nullptr)
    {
        return -1;
    }

    const int row = ListView_GetNextItem(list_, -1, LVNI_SELECTED);

    if (row < 0)
    {
        return -1;
    }

    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;

    if (!ListView_GetItem(list_, &item))
    {
        return -1;
    }

    return static_cast<int>(item.lParam);
}

void VocabularyWindow::copySelected()
{
    const int index = selectedEntry();

    if (index < 0 || static_cast<std::size_t>(index) >= entries_.size())
    {
        return;
    }

    const VocabularyEntry &entry = entries_[static_cast<std::size_t>(index)];

    copyToClipboard(
        hwnd_,
        toWide(entry.original) + L"\n" + toWide(entry.translated));
}

void VocabularyWindow::deleteSelected()
{
    const int index = selectedEntry();

    if (index < 0)
    {
        return;
    }

    if (Vocabulary::removeAt(static_cast<std::size_t>(index)))
    {
        reload();
    }
}

void VocabularyWindow::clearAll()
{
    if (entries_.empty())
    {
        return;
    }

    if (MessageBoxW(
            hwnd_,
            L"确定清空生词本？此操作不可恢复。",
            L"TranslateCpp",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    if (Vocabulary::clear())
    {
        reload();
    }
}

LRESULT CALLBACK VocabularyWindow::windowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    VocabularyWindow *self = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto *createStruct =
            reinterpret_cast<const CREATESTRUCTW *>(lParam);

        self = static_cast<VocabularyWindow *>(
            createStruct->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));

        self->hwnd_ = hwnd;
    }
    else
    {
        self = reinterpret_cast<VocabularyWindow *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message)
    {
    case WM_CREATE:
        self->onCreate();
        self->layout();
        return 0;

    case WM_SIZE:
        self->layout();
        return 0;

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);

        if (id == ID_SEARCH && notification == EN_CHANGE)
        {
            self->applyFilter();
            return 0;
        }

        switch (id)
        {
        case ID_COPY:
            self->copySelected();
            return 0;

        case ID_DELETE:
            self->deleteSelected();
            return 0;

        case ID_CLEAR:
            self->clearAll();
            return 0;

        case ID_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        default:
            break;
        }

        break;
    }

    case WM_NOTIFY:
    {
        const auto *header = reinterpret_cast<const NMHDR *>(lParam);

        if (header != nullptr && header->hwndFrom == self->list_)
        {
            if (header->code == NM_DBLCLK)
            {
                self->copySelected();
                return 0;
            }

            if (header->code == LVN_ITEMCHANGED)
            {
                self->updateButtons();
                return 0;
            }

            if (header->code == LVN_KEYDOWN)
            {
                const auto *key = reinterpret_cast<const NMLVKEYDOWN *>(lParam);

                if (key->wVKey == VK_DELETE)
                {
                    self->deleteSelected();
                    return 0;
                }

                if (key->wVKey == 'C' &&
                    (GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    self->copySelected();
                    return 0;
                }
            }
        }

        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (self->font_ != nullptr)
        {
            DeleteObject(self->font_);
            self->font_ = nullptr;
        }

        self->hwnd_ = nullptr;
        self->list_ = nullptr;
        self->search_ = nullptr;
        self->copyButton_ = nullptr;
        self->deleteButton_ = nullptr;
        self->clearButton_ = nullptr;
        self->closeButton_ = nullptr;
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
