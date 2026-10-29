#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

#include "Vocabulary.h"

// 生词本窗口：列表 + 搜索 + 复制/删除/清空
class VocabularyWindow
{
public:
    VocabularyWindow() = default;

    bool initialize(HINSTANCE instance);

    // 显示窗口（已存在则刷新并前置）
    void show();

    void close();

private:
    HWND hwnd_ = nullptr;
    HWND search_ = nullptr;
    HWND list_ = nullptr;
    HWND copyButton_ = nullptr;
    HWND deleteButton_ = nullptr;
    HWND clearButton_ = nullptr;
    HWND closeButton_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;

    std::vector<VocabularyEntry> entries_;

    static constexpr wchar_t WINDOW_CLASS_NAME[] =
        L"TranslateCppVocabularyWindow";

    static LRESULT CALLBACK windowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    void onCreate();
    void layout();
    void reload();
    void applyFilter();
    void updateButtons();

    [[nodiscard]] int selectedEntry() const;
    void copySelected();
    void deleteSelected();
    void clearAll();
};
