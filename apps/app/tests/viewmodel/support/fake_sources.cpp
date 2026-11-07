// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/fake_sources.hpp"

#include <utility>

namespace ej::test
{

namespace
{

std::string_view change_kind_name(viewmodel::LogChange::Kind kind)
{
    using Kind = viewmodel::LogChange::Kind;
    switch (kind)
    {
    case Kind::LOADED:
        return "LOADED";
    case Kind::REPLACED:
        return "REPLACED";
    case Kind::ENTRY_ADDED:
        return "ENTRY_ADDED";
    case Kind::ENTRY_EDITED:
        return "ENTRY_EDITED";
    case Kind::ENTRY_DELETED:
        return "ENTRY_DELETED";
    case Kind::ENTRY_RESTORED:
        return "ENTRY_RESTORED";
    case Kind::HEADER_CORRECTED:
        return "HEADER_CORRECTED";
    case Kind::HEADER_CHANGED:
        return "HEADER_CHANGED";
    case Kind::SHARED:
        return "SHARED";
    }
    return "UNKNOWN";
}

} // namespace

std::string describe_log_change(const viewmodel::LogChange& change)
{
    std::string text(change_kind_name(change.m_Kind));
    if (change.m_Entry)
    {
        text += ' ';
        text += change.m_Entry->format();
    }
    if (change.m_Origin == viewmodel::LogChange::Origin::UNDO)
    {
        text += " (undo)";
    }
    return text;
}

void FakeFeedbackSink::acknowledge_touch()
{
    ++m_Touches;
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::HAPTIC, "impact");
    }
}

void FakeFeedbackSink::entry_recorded(EntryId entry)
{
    m_Recorded.push_back(entry);
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::CONFIRMATION, "entry_recorded " + entry.format());
    }
}

void FakeFeedbackSink::note_saved(EntryId entry)
{
    m_Saved.push_back(entry);
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::CONFIRMATION, "note_saved " + entry.format());
    }
}

void FakeLogObserver::note(const viewmodel::LogDocument* document, const viewmodel::LogChange& change)
{
    m_Changes.push_back(change);
    m_LastDocument = document;
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::LOG_CHANGE, describe_log_change(change));
    }
}

void FakeConfigurationObserver::on_configuration_changed(const viewmodel::ConfigurationSource& source)
{
    ++m_Changes;
    m_LastSource = &source;
    if (m_Log != nullptr)
    {
        m_Log->record(EventKind::CONFIGURATION, source.configured() ? "configured" : "unconfigured");
    }
}

void FakeConfigurationSource::set(Configuration configuration)
{
    m_Configuration = std::move(configuration);
    m_Configured = true;
    notify();
}

void FakeConfigurationSource::clear()
{
    m_Configuration = Configuration{};
    m_Configured = false;
    notify();
}

void FakeConfigurationSource::notify()
{
    m_Observers.notify([this](viewmodel::ConfigurationObserver& observer)
                       { observer.on_configuration_changed(*this); });
}

Result<ErrorTypeId, viewmodel::ConfigEditError> FakeConfigurationEditor::add_error_type(std::string name)
{
    if (m_Refusal)
    {
        auto error = std::move(*m_Refusal);
        m_Refusal.reset();
        return fail(std::move(error));
    }
    if (name.empty())
    {
        return fail(viewmodel::ConfigEditError{viewmodel::ConfigEditError::Code::REFUSED, "The name is empty."});
    }
    ErrorType type;
    type.m_Id = m_Ids.next_as<ErrorTypeId>();
    type.m_Name = std::move(name);
    m_Added.push_back(type);
    return type.m_Id;
}

void FakeConfigurationEditor::commit()
{
    if (m_Source != nullptr && pending() > 0)
    {
        Configuration configuration = m_Source->configuration();
        for (std::size_t i = m_Committed; i < m_Added.size(); ++i)
        {
            configuration.m_ErrorTypes.push_back(m_Added[i]);
        }
        m_Source->set(std::move(configuration));
    }
    m_Committed = m_Added.size();
}

void FakeOpenLogSource::set(viewmodel::LogDocument* document)
{
    m_Document = document;
    m_Observers.notify([document](viewmodel::OpenLogObserver& observer) { observer.on_open_log_changed(document); });
}

} // namespace ej::test
