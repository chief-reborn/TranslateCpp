#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include "Config.h"
#include "GoogleTranslator.h"
#include "PopupWindow.h"
#include "TextConverter.h"
#include "TrayIcon.h"
#include "Vocabulary.h"
#include "VocabularyWindow.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    constexpr UINT WM_TRANSLATE_CLIPBOARD = WM_APP + 1;
    constexpr UINT WM_TRANSLATION_RESULT = WM_APP + 2;
    constexpr UINT WM_HIDE_POPUP = WM_APP + 3;

    constexpr ULONGLONG DOUBLE_COPY_INTERVAL_MS = 500;

    constexpr int CLIPBOARD_RETRY_COUNT = 10;
    constexpr DWORD CLIPBOARD_RETRY_DELAY_MS = 30;

    TrayIcon g_trayIcon;
    PopupWindow g_popupWindow;
    VocabularyWindow g_vocabularyWindow;

    Config g_config;

    HHOOK g_keyboardHook = nullptr;
    DWORD g_mainThreadId = 0;

    ULONGLONG g_lastCopyTime = 0;
    bool g_cKeyDown = false;
    bool g_popupVisible = false;

    // 只显示最新一次请求的结果，避免连续触发时旧结果覆盖新结果
    std::atomic<std::uint64_t> g_requestCounter{0};
    std::atomic<std::uint64_t> g_latestRequest{0};

    struct TranslationResult
    {
        std::uint64_t id = 0;
        std::string original;
        std::string translated;
        std::string error;
    };

    void showFatalError(const std::wstring &message)
    {
        MessageBoxW(
            nullptr,
            message.c_str(),
            L"TranslateCpp",
            MB_OK | MB_ICONERROR);
    }

    // 读取系统剪贴板中的 Unicode 文本。
    // 剪贴板常被其它程序短暂占用，这里重试几次而不是直接失败。
    std::string getClipboardText()
    {
        std::string result;

        for (int attempt = 0; attempt < CLIPBOARD_RETRY_COUNT; ++attempt)
        {
            if (!OpenClipboard(nullptr))
            {
                Sleep(CLIPBOARD_RETRY_DELAY_MS);
                continue;
            }

            HANDLE data = GetClipboardData(CF_UNICODETEXT);

            if (data != nullptr)
            {
                const wchar_t *text =
                    static_cast<const wchar_t *>(GlobalLock(data));

                if (text != nullptr)
                {
                    result = wideToUtf8(std::wstring_view(text));
                    GlobalUnlock(data);
                }
            }

            CloseClipboard();

            return result;
        }

        throw std::runtime_error(
            "无法读取剪贴板，可能被其它程序占用，请重试。");
    }

    // 全局键盘钩子：Ctrl+C 连按两次触发翻译，Esc 关闭浮窗
    LRESULT CALLBACK keyboardHookProc(
        int nCode,
        WPARAM wParam,
        LPARAM lParam)
    {
        if (nCode == HC_ACTION)
        {
            const auto *keyInfo =
                reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);

            if (wParam == WM_KEYDOWN ||
                wParam == WM_SYSKEYDOWN)
            {
                if (keyInfo->vkCode == VK_ESCAPE && g_popupVisible)
                {
                    PostThreadMessageW(
                        g_mainThreadId,
                        WM_HIDE_POPUP,
                        0,
                        0);
                }

                if (keyInfo->vkCode == 'C' && !g_cKeyDown)
                {
                    g_cKeyDown = true;

                    const bool ctrlPressed =
                        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

                    if (ctrlPressed)
                    {
                        const ULONGLONG now = GetTickCount64();

                        if (g_lastCopyTime != 0 &&
                            now - g_lastCopyTime <=
                                DOUBLE_COPY_INTERVAL_MS)
                        {
                            g_lastCopyTime = 0;

                            const auto requestId =
                                g_requestCounter.fetch_add(1) + 1;

                            g_latestRequest.store(requestId);

                            PostThreadMessageW(
                                g_mainThreadId,
                                WM_TRANSLATE_CLIPBOARD,
                                static_cast<WPARAM>(requestId),
                                0);
                        }
                        else
                        {
                            g_lastCopyTime = now;
                        }
                    }
                    else
                    {
                        g_lastCopyTime = 0;
                    }
                }
            }
            else if (wParam == WM_KEYUP ||
                     wParam == WM_SYSKEYUP)
            {
                if (keyInfo->vkCode == 'C')
                {
                    g_cKeyDown = false;
                }
            }
        }

        return CallNextHookEx(
            g_keyboardHook,
            nCode,
            wParam,
            lParam);
    }

    // 把结果投递给 UI 线程，所有权随之转移
    void postResult(std::unique_ptr<TranslationResult> result)
    {
        if (PostThreadMessageW(
                g_mainThreadId,
                WM_TRANSLATION_RESULT,
                0,
                reinterpret_cast<LPARAM>(result.get())))
        {
            // 所有权交给主线程
            result.release();
        }
    }

    // 工作线程：翻译给定文本
    void translateText(std::string text, std::uint64_t requestId)
    {
        auto result = std::make_unique<TranslationResult>();

        result->id = requestId;
        result->original = text;

        try
        {
            if (text.empty())
            {
                throw std::runtime_error("没有可翻译的文本。");
            }

            GoogleTranslator translator(g_config.proxy);

            result->translated = translator.translate(
                text,
                "auto",
                g_config.targetLanguage);
        }
        catch (const std::exception &e)
        {
            result->error = e.what();
        }

        postResult(std::move(result));
    }

    // 工作线程：读取剪贴板并翻译
    void translateClipboard(std::uint64_t requestId)
    {
        std::string text;

        try
        {
            text = getClipboardText();
        }
        catch (const std::exception &e)
        {
            auto result = std::make_unique<TranslationResult>();

            result->id = requestId;
            result->error = e.what();

            postResult(std::move(result));
            return;
        }

        translateText(std::move(text), requestId);
    }
}

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    HANDLE singleInstanceMutex = CreateMutexW(
        nullptr,
        TRUE,
        L"Local\\TranslateCpp_SingleInstance_Mutex");

    if (singleInstanceMutex == nullptr)
    {
        showFatalError(L"无法创建单实例互斥量。");
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(singleInstanceMutex);
        return 0;
    }

    g_config = loadConfig();
    g_mainThreadId = GetCurrentThreadId();

    // 创建线程消息队列，工作线程才能向主线程 PostThreadMessage
    MSG msg{};

    PeekMessageW(
        &msg,
        nullptr,
        WM_USER,
        WM_USER,
        PM_NOREMOVE);

    HINSTANCE instance = GetModuleHandleW(nullptr);

    if (!g_popupWindow.initialize(instance))
    {
        showFatalError(L"无法创建翻译浮窗。");
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    // 原文被编辑后按 Ctrl+Enter，用编辑框里的文本重新翻译
    g_popupWindow.setOnRetranslate(
        [](const std::string &text)
        {
            const auto requestId = g_requestCounter.fetch_add(1) + 1;

            g_latestRequest.store(requestId);

            std::thread(translateText, text, requestId).detach();
        });

    // 弹窗里点「☆ 收藏」或按 Ctrl+D，把当前原文/译文存进生词本
    g_popupWindow.setOnBookmark(
        [](const std::string &original, const std::string &translated)
        {
            return Vocabulary::add(original, translated);
        });

    if (!g_vocabularyWindow.initialize(instance))
    {
        showFatalError(L"无法创建生词本窗口。");
        g_trayIcon.shutdown();
        g_popupWindow.close();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    // 托盘菜单打开生词本
    g_trayIcon.setOnOpenVocabulary(
        []()
        {
            g_vocabularyWindow.show();
        });

    if (!g_trayIcon.initialize(instance))
    {
        showFatalError(L"无法创建系统托盘图标。");
        g_popupWindow.close();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    // 安装全局键盘钩子
    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        keyboardHookProc,
        instance,
        0);

    if (g_keyboardHook == nullptr)
    {
        showFatalError(
            L"无法安装全局键盘钩子（错误码 " +
            std::to_wstring(GetLastError()) +
            L"）。");

        g_trayIcon.shutdown();
        g_popupWindow.close();
        CloseHandle(singleInstanceMutex);
        return 1;
    }

    for (;;)
    {
        const BOOL status = GetMessageW(
            &msg,
            nullptr,
            0,
            0);

        if (status <= 0)
        {
            break;
        }

        if (msg.message == WM_TRANSLATE_CLIPBOARD)
        {
            // 网络请求放在工作线程，避免阻塞 UI 线程
            const auto requestId =
                static_cast<std::uint64_t>(msg.wParam);

            std::thread(translateClipboard, requestId).detach();
        }
        else if (msg.message == WM_TRANSLATION_RESULT)
        {
            // 主线程接管结果对象
            std::unique_ptr<TranslationResult> result(
                reinterpret_cast<TranslationResult *>(msg.lParam));

            if (result && result->id == g_latestRequest.load())
            {
                if (result->error.empty())
                {
                    g_popupWindow.show(
                        result->original,
                        result->translated);
                }
                else
                {
                    g_popupWindow.showError(
                        result->original,
                        result->error);
                }

                g_popupVisible = true;
            }
        }
        else if (msg.message == WM_HIDE_POPUP)
        {
            g_popupWindow.close();
            g_popupVisible = false;
        }
        else
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_keyboardHook != nullptr)
    {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }

    g_popupWindow.close();
    g_vocabularyWindow.close();
    g_trayIcon.shutdown();

    CloseHandle(singleInstanceMutex);
    return 0;
}
