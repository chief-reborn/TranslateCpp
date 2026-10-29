#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>

class PopupWindow
{
public:
    PopupWindow() = default;

    // 注册窗口类
    bool initialize(HINSTANCE instance);

    // 显示翻译结果；如果窗口已存在，则更新内容
    void show(
        const std::string &original,
        const std::string &translated);

    // 显示失败原因，译文区域用红色标注
    void showError(
        const std::string &original,
        const std::string &message);

    // 关闭悬浮窗
    void close();

    // 原文被修改后按 Ctrl+Enter 时回调，参数是编辑框里的当前文本
    void setOnRetranslate(std::function<void(const std::string &)> callback);

    // 收藏当前原文和译文；返回 false 表示已在生词本里或保存失败
    void setOnBookmark(
        std::function<bool(const std::string &, const std::string &)> callback);

private:
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HINSTANCE instance_ = nullptr;

    WNDPROC editDefaultProc_ = nullptr;
    HFONT editFont_ = nullptr;
    UINT editFontDpi_ = 0;

    std::wstring originalText_;
    std::wstring translatedText_;
    bool isError_ = false;

    // 原文文本区的分配高度；0 表示还没算过
    int originalAlloc_ = 0;

    // 译文区的滚动位置
    int translatedScroll_ = 0;

    // 分隔条拖动
    bool draggingDivider_ = false;
    int dividerDragStartY_ = 0;
    int dividerDragStartAlloc_ = 0;

    // 译文滚动条拖动
    bool draggingThumb_ = false;
    int thumbDragStartY_ = 0;
    int thumbDragStartScroll_ = 0;

    bool dividerHover_ = false;
    bool trackingMouse_ = false;

    std::function<void(const std::string &)> onRetranslate_;
    std::function<bool(const std::string &, const std::string &)> onBookmark_;

    // 收藏后的临时提示（"已收藏" / "已在生词本中"）
    std::wstring bookmarkMessage_;

    void showInternal(
        const std::string &original,
        const std::string &translated,
        bool isError);

    // 计算窗口尺寸和两栏的默认分配
    void resizeToContent(HWND hwnd);

    // 原文编辑框
    void createEditControl();
    void positionEditControl();
    void updateEditFont(UINT dpi);
    void updateEditScrollBar();
    void syncEditText();
    [[nodiscard]] std::wstring editTextWide() const;
    [[nodiscard]] std::string editTextUtf8() const;
    void requestRetranslate();

    // 布局
    [[nodiscard]] int dividerCenterY() const;
    [[nodiscard]] RECT translationTextRect() const;
    [[nodiscard]] int translationContentHeight() const;
    [[nodiscard]] int translationScrollMax() const;
    [[nodiscard]] RECT translationTrackRect() const;
    [[nodiscard]] RECT translationThumbRect() const;
    [[nodiscard]] RECT bookmarkRect() const;

    void requestBookmark();

    // 命中判断
    [[nodiscard]] bool isOverDivider(POINT clientPoint) const;
    [[nodiscard]] bool isOverThumb(POINT clientPoint) const;
    [[nodiscard]] bool isOverTrack(POINT clientPoint) const;

    // 滚动
    void clampScroll();
    void scrollTranslationBy(int delta);
    void applySplitOffset(int offset);

    static constexpr int BASE_WIDTH = 560;
    static constexpr int BASE_HEIGHT = 300;

    static constexpr wchar_t WINDOW_CLASS_NAME[] =
        L"TranslateCppPopupWindow";

    static LRESULT CALLBACK windowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    static LRESULT CALLBACK editWindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    void paint(HWND hwnd);
};
