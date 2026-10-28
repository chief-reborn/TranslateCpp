#pragma once

#include <filesystem>
#include <string>

// 运行配置。文件位于 exe 同目录的 config.json，缺失时全部使用默认值。
struct Config
{
    // 空字符串表示直连；否则形如 "127.0.0.1:7897"，
    // 也接受带 "http://" 前缀的写法。
    std::string proxy;

    // Google 语言代码，例如 "zh-CN"、"en"、"ja"。
    std::string targetLanguage = "zh-CN";
};

// 读取配置。文件不存在或内容有误时返回默认值，不会抛异常。
Config loadConfig();

// 配置文件路径（exe 同目录），供托盘菜单打开。
std::filesystem::path configFilePath();
