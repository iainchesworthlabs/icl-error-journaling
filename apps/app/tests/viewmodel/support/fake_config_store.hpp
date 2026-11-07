// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viewmodel/config_store.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ej::test
{

/// A `ConfigStore` in memory: the saved text, the files the person chose, and what was asked. A test makes the next
/// load or save fail to see that a service changes nothing after a failed save.
class FakeConfigStore final : public viewmodel::ConfigStore
{
public:
    [[nodiscard]] Result<std::optional<std::string>, StorageError> load() override
    {
        ++m_Loads;
        if (m_FailLoad)
        {
            return fail(std::exchange(m_FailLoad, std::nullopt).value());
        }
        return m_Saved;
    }

    Result<void, StorageError> save(std::string_view text) override
    {
        if (m_FailSave)
        {
            return fail(std::exchange(m_FailSave, std::nullopt).value());
        }
        m_Saved = std::string(text);
        m_Saves.emplace_back(text);
        return {};
    }

    [[nodiscard]] Result<std::string, StorageError> read_file(std::string_view path, std::size_t max_bytes) override
    {
        const auto it = m_Files.find(std::string(path));
        if (it == m_Files.end())
        {
            return fail(make_error(StorageErrorKind::NOT_FOUND, "no such file"));
        }
        if (it->second.size() > max_bytes)
        {
            return fail(make_error(StorageErrorKind::LIMIT_EXCEEDED, "file too large"));
        }
        return it->second;
    }

    Result<void, StorageError> write_file(std::string_view path, std::string_view text) override
    {
        m_Files[std::string(path)] = std::string(text);
        return {};
    }

    /// The file on disk as the app finds it at launch (nothing, for a first launch).
    void set_saved(std::optional<std::string> text)
    {
        m_Saved = std::move(text);
    }

    /// A file the person could pick, for `read_file`.
    void add_file(std::string path, std::string text)
    {
        m_Files[std::move(path)] = std::move(text);
    }

    void fail_next_load(StorageError error)
    {
        m_FailLoad = std::move(error);
    }
    void fail_next_save(StorageError error)
    {
        m_FailSave = std::move(error);
    }

    [[nodiscard]] const std::optional<std::string>& saved() const
    {
        return m_Saved;
    }
    [[nodiscard]] const std::vector<std::string>& saves() const
    {
        return m_Saves;
    }
    [[nodiscard]] std::size_t loads() const
    {
        return m_Loads;
    }
    [[nodiscard]] const std::map<std::string, std::string>& files() const
    {
        return m_Files;
    }

private:
    std::optional<std::string> m_Saved;
    std::vector<std::string> m_Saves;
    std::map<std::string, std::string> m_Files;
    std::optional<StorageError> m_FailLoad;
    std::optional<StorageError> m_FailSave;
    std::size_t m_Loads = 0;
};

} // namespace ej::test
