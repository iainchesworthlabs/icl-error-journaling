// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/draft_store.hpp"

#include "error_journaling/stored_log.hpp"

namespace ej
{

DraftStore::DraftStore(LogRepository& repository, const Clock& clock, DraftTimer& timer, Options options)
    : m_Repository(repository), m_Clock(clock), m_Timer(timer), m_Options(std::move(options))
{
}

DraftStore::~DraftStore()
{
    m_Timer.stop();
}

Result<std::optional<std::string>, StorageError> DraftStore::load()
{
    auto stored = m_Repository.get_draft();
    if (!stored)
    {
        return fail(stored.error());
    }
    m_Written = *stored ? std::optional<std::string>((*stored)->m_Text) : std::nullopt;
    m_Pending.reset();
    m_Timer.stop();
    m_Armed = false;
    return m_Written;
}

Result<void, StorageError> DraftStore::set(std::string_view text)
{
    if (auto valid = validate_draft(text); !valid)
    {
        return valid;
    }
    const bool same_as_row = m_Written ? *m_Written == text : text.empty();
    if (same_as_row)
    {
        // The field is back to what the row holds: nothing to write, and a write that was waiting would only undo it.
        m_Pending.reset();
        return {};
    }
    m_Pending = std::string(text);
    arm();
    return {};
}

void DraftStore::arm()
{
    if (m_Armed)
    {
        return;
    }
    m_Armed = true;
    m_Timer.start(m_Options.m_Delay, [this] { on_timer(); });
}

void DraftStore::on_timer()
{
    m_Armed = false;
    if (auto written = write_pending(); !written)
    {
        if (m_Options.m_OnWriteError)
        {
            m_Options.m_OnWriteError(written.error());
        }
    }
}

Result<void, StorageError> DraftStore::write_pending()
{
    if (!m_Pending)
    {
        return {};
    }
    if (auto written = m_Repository.set_draft(*m_Pending, m_Clock.now()); !written)
    {
        arm();
        return written;
    }
    m_Written = m_Pending->empty() ? std::nullopt : std::optional<std::string>(*m_Pending);
    m_Pending.reset();
    return {};
}

Result<void, StorageError> DraftStore::flush()
{
    m_Timer.stop();
    m_Armed = false;
    return write_pending();
}

void DraftStore::committed()
{
    m_Timer.stop();
    m_Armed = false;
    m_Pending.reset();
    m_Written.reset();
}

std::optional<std::string> DraftStore::current() const
{
    if (m_Pending)
    {
        return m_Pending->empty() ? std::nullopt : m_Pending;
    }
    return m_Written;
}

} // namespace ej
