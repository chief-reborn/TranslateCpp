#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

private:
    enum class Pane
    {
        Original,
        Translated
    };

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;

    std::wstring originalText_;
    std::wstring translatedText_;
    bool isError_ = false;

    // 原文文本区的分配高度；0 表示还没算过
    int originalAlloc_ = 0;

    // 两栏各自的滚动位置（像素）
    int originalScroll_ = 0;
    int translatedScroll_ = 0;
    Pane activePane_ = Pane::Translated;

    // 分隔条拖动
    bool draggingDivider_ = false;
    int dividerDragStartY_ = 0;
    int dividerDragStartAlloc_ = 0;

    // 滚动条滑块拖动
    bool draggingThumb_ = false;
    Pane thumbPane_ = Pane::Original;
    int thumbDragStartY_ = 0;
    int thumbDragStartScroll_ = 0;

    bool dividerHover_ = false;
    bool trackingMouse_ = false;

    void showInternal(
        const std::string &original,
        const std::string &translated,
        bool isError);

    // 计算窗口尺寸和两栏的默认分配
    void resizeToContent(HWND hwnd);

    // 布局
    [[nodiscard]] int dividerCenterY() const;
    [[nodiscard]] RECT paneTextRect(Pane pane) const;
    [[nodiscard]] int paneContentHeight(Pane pane) const;
    [[nodiscard]] int paneTextHeight(Pane pane) const;
    [[nodiscard]] int paneScrollMax(Pane pane) const;
    [[nodiscard]] int &scrollOf(Pane pane);
    [[nodiscard]] RECT trackRect(Pane pane) const;
    [[nodiscard]] RECT thumbRect(Pane pane) const;

    // 命中判断
    [[nodiscard]] bool isOverDivider(POINT clientPoint) const;
    [[nodiscard]] bool isOverThumb(Pane pane, POINT clientPoint) const;
    [[nodiscard]] bool isOverTrack(Pane pane, POINT clientPoint) const;
    [[nodiscard]] Pane paneAt(POINT clientPoint) const;

    // 滚动
    void clampScrolls();
    void scrollPaneBy(Pane pane, int delta);
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

    void paint(HWND hwnd);
};
