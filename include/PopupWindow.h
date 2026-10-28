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
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;

    std::wstring originalText_;
    std::wstring translatedText_;
    bool isError_ = false;

    // 分隔条拖动：正数表示原文区加高、译文区相应变矮
    int splitOffset_ = 0;
    bool dragging_ = false;
    bool dividerHover_ = false;
    bool trackingMouse_ = false;
    POINT dragStart_{};
    int dragStartOffset_ = 0;

    void showInternal(
        const std::string &original,
        const std::string &translated,
        bool isError);

    // 按文本实际高度调整窗口大小
    void resizeToContent(HWND hwnd);

    // 分隔条位置（客户区坐标）与命中判断
    [[nodiscard]] int dividerCenterY() const;
    [[nodiscard]] bool isOverDivider(POINT clientPoint) const;

    // 调整原文/译文的高度分配
    void applySplitOffset(int offset);

    // 96 DPI 下的基准尺寸，实际按窗口所在显示器的 DPI 缩放
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
