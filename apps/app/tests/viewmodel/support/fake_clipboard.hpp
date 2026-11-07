// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "support/event_log.hpp"
#include "viewmodel/clipboard.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ej::test
{

/// A clipboard that keeps what was put on it, byte for byte, and every earlier text too.
class FakeClipboard final : public viewmodel::Clipboard
{
public:
    explicit FakeClipboard(EventLog* log = nullptr) : m_Log(log) {}

    void set_text(std::string_view text) override
    {
        m_History.emplace_back(text);
        if (m_Log != nullptr)
        {
            m_Log->record(EventKind::CLIPBOARD, std::string(text));
        }
    }

    /// The text on the clipboard now; empty before anything was copied.
    [[nodiscard]] const std::string& text() const
    {
        return m_History.empty() ? m_Empty : m_History.back();
    }
    [[nodiscard]] const std::vector<std::string>& history() const
    {
        return m_History;
    }
    [[nodiscard]] std::size_t copies() const
    {
        return m_History.size();
    }

private:
    EventLog* m_Log;
    std::vector<std::string> m_History;
    std::string m_Empty;
};

} // namespace ej::test
