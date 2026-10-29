#include "Vocabulary.h"

#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

namespace
{
    std::filesystem::path executableDirectory()
    {
        std::wstring buffer(MAX_PATH, L'\0');

        for (;;)
        {
            const DWORD length = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));

            if (length == 0)
            {
                return {};
            }

            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }

            if (buffer.size() >= 32768)
            {
                return {};
            }

            buffer.resize(buffer.size() * 2);
        }

        return std::filesystem::path(buffer).parent_path();
    }

    std::string currentTimestamp()
    {
        const std::time_t now = std::time(nullptr);

        std::tm local{};

        if (const std::tm *value = std::localtime(&now); value != nullptr)
        {
            local = *value;
        }

        std::ostringstream stream;
        stream << std::put_time(&local, "%Y-%m-%d %H:%M");

        return stream.str();
    }

    // 先写临时文件再替换，避免写到一半断电把生词本写坏
    bool save(const std::vector<VocabularyEntry> &entries)
    {
        const std::filesystem::path path = Vocabulary::filePath();

        if (path.empty())
        {
            return false;
        }

        nlohmann::json array = nlohmann::json::array();

        for (const VocabularyEntry &entry : entries)
        {
            array.push_back({
                {"original", entry.original},
                {"translated", entry.translated},
                {"added_at", entry.addedAt}});
        }

        std::filesystem::path temporary = path;
        temporary += L".tmp";

        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);

            if (!file)
            {
                return false;
            }

            file << array.dump(4);

            if (!file)
            {
                return false;
            }
        }

        return MoveFileExW(
                   temporary.c_str(),
                   path.c_str(),
                   MOVEFILE_REPLACE_EXISTING) != FALSE;
    }
}

std::filesystem::path Vocabulary::filePath()
{
    return executableDirectory() / L"vocabulary.json";
}

std::vector<VocabularyEntry> Vocabulary::load()
{
    std::vector<VocabularyEntry> entries;

    const std::filesystem::path path = filePath();

    std::error_code error;

    if (!std::filesystem::exists(path, error))
    {
        return entries;
    }

    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return entries;
    }

    try
    {
        const nlohmann::json data = nlohmann::json::parse(file);

        if (!data.is_array())
        {
            throw std::runtime_error("not an array");
        }

        for (const auto &item : data)
        {
            if (!item.is_object())
            {
                continue;
            }

            VocabularyEntry entry;

            if (item.contains("original") && item["original"].is_string())
            {
                entry.original = item["original"].get<std::string>();
            }

            if (item.contains("translated") &&
                item["translated"].is_string())
            {
                entry.translated = item["translated"].get<std::string>();
            }

            if (item.contains("added_at") && item["added_at"].is_string())
            {
                entry.addedAt = item["added_at"].get<std::string>();
            }

            if (!entry.original.empty())
            {
                entries.push_back(std::move(entry));
            }
        }
    }
    catch (const std::exception &)
    {
        // 文件被改坏了：备份一份再从空开始，免得下次保存直接覆盖掉内容
        std::filesystem::path backup = path;
        backup += L".bak";

        std::error_code backupError;
        std::filesystem::copy_file(
            path, backup,
            std::filesystem::copy_options::overwrite_existing,
            backupError);

        return {};
    }

    return entries;
}

bool Vocabulary::add(
    const std::string &original,
    const std::string &translated)
{
    if (original.empty())
    {
        return false;
    }

    std::vector<VocabularyEntry> entries = load();

    for (const VocabularyEntry &entry : entries)
    {
        if (entry.original == original)
        {
            return false;
        }
    }

    entries.insert(
        entries.begin(),
        VocabularyEntry{original, translated, currentTimestamp()});

    return save(entries);
}

bool Vocabulary::removeAt(std::size_t index)
{
    std::vector<VocabularyEntry> entries = load();

    if (index >= entries.size())
    {
        return false;
    }

    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));

    return save(entries);
}

bool Vocabulary::clear()
{
    return save({});
}
