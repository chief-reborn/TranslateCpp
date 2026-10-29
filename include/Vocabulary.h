#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// 一条生词记录：原文、译文、收藏时间
struct VocabularyEntry
{
    std::string original;
    std::string translated;
    std::string addedAt;
};

namespace Vocabulary
{
    // 生词本文件（exe 同目录的 vocabulary.json）
    std::filesystem::path filePath();

    // 读取全部记录，最新的在最前面
    std::vector<VocabularyEntry> load();

    // 追加一条；原文已存在时返回 false
    bool add(const std::string &original, const std::string &translated);

    // 按索引删除（索引对应 load() 的顺序）
    bool removeAt(std::size_t index);

    // 清空
    bool clear();
}
