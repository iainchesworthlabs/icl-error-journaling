// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/undo_controller.hpp"

#include "error_journaling/configuration.hpp"
#include "error_journaling/entry.hpp"
#include "error_journaling/time_format.hpp"
#include "error_journaling/timestamp.hpp"

#include <algorithm>
#include <utility>
#include <variant>

namespace ej::viewmodel
{

namespace
{

/// The multiplication sign of "logged x2", as UTF-8.
constexpr const char* TIMES = "\xC3\x97";

QString from_utf8(const std::string& text)
{
    return QString::fromStdString(text);
}

} // namespace

UndoController::UndoController(const Environment& environment, const ConfigurationSource& configuration,
                               QObject* parent)
    : QObject(parent), m_Environment(environment), m_Configuration(configuration)
{
}

UndoController::~UndoController()
{
    m_Environment.m_Scheduler.cancel(m_Timer);
    for (LogDocument* document : m_Documents)
    {
        document->remove_observer(*this);
    }
}

void UndoController::observe(LogDocument& document)
{
    if (std::ranges::find(m_Documents, &document) == m_Documents.end())
    {
        m_Documents.push_back(&document);
    }
    document.add_observer(*this);
}

void UndoController::stop_observing(LogDocument& document)
{
    document.remove_observer(*this);
    std::erase(m_Documents, &document);
    if (m_Active && m_Document == &document)
    {
        close_toast();
    }
}

void UndoController::set_note_undo_target(MoveOnlyFunction<void(const std::string&)> target)
{
    m_NoteTarget = std::move(target);
}

void UndoController::note_attached(EntryId entry, const std::string& text)
{
    expire_if_due();
    if (!m_Active || m_Kind != Kind::TAP || m_Entry != entry || text.empty())
    {
        return;
    }
    m_NoteText = text;
    m_Text += QStringLiteral(" with note");
    emit toastChanged();
}

LogDocument* UndoController::observed(const LogDocument& document) const
{
    const auto found = std::ranges::find(m_Documents, &document);
    return found == m_Documents.end() ? nullptr : *found;
}

// -------------------------------------------------------------------------------------------------- what is told

void UndoController::on_log_changed(const LogDocument& document, const LogChange& change)
{
    LogDocument* source = observed(document);
    if (source == nullptr)
    {
        return;
    }
    if (change.m_Kind == LogChange::Kind::REPLACED)
    {
        if (m_Active && m_Document == source)
        {
            close_toast();
        }
        return;
    }
    // The undo of an action is not an action: no toast, and the toast it belongs to is closed by its own completion.
    if (change.m_Origin == LogChange::Origin::UNDO || !change.m_Entry)
    {
        return;
    }

    const Entry* entry = document.log().find(*change.m_Entry);
    if (entry == nullptr)
    {
        return;
    }
    switch (change.m_Kind)
    {
    case LogChange::Kind::ENTRY_ADDED:
        open_for_added(*source, *entry);
        break;
    case LogChange::Kind::ENTRY_DELETED:
        open_toast(Kind::DELETED, *source, entry->m_Id, QStringLiteral("Entry deleted"));
        break;
    default:
        break;
    }
}

void UndoController::open_for_added(LogDocument& document, const Entry& entry)
{
    if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
    {
        expire_if_due();
        const bool repeat =
            m_Active && m_Kind == Kind::TAP && m_Document == &document && m_ErrorType == error->m_ErrorType;
        const int count = repeat ? m_Repeat + 1 : 1;
        std::string line = type_name(*error) + " logged ";
        if (count > 1)
        {
            line += TIMES + std::to_string(count) + " ";
        }
        line += "at " + time_text(entry.m_Captured);
        open_toast(Kind::TAP, document, entry.m_Id, from_utf8(line));
        m_ErrorType = error->m_ErrorType;
        m_Repeat = count;
    }
    else if (const auto* interaction = std::get_if<InteractionEntry>(&entry.m_Body))
    {
        // Stage 14: a tap on the Interactions board. Same toast and the same undo as an error tap; no repeat count.
        open_toast(Kind::TAP, document, entry.m_Id,
                   from_utf8(interaction->m_TypeLabel + " logged at " + time_text(entry.m_Captured)));
        m_ErrorType = ErrorTypeId{};
    }
    else if (const auto* note = std::get_if<NoteEntry>(&entry.m_Body))
    {
        open_toast(Kind::NOTE, document, entry.m_Id, from_utf8("Note added at " + time_text(entry.m_Captured)));
        m_NoteText = note->m_Value;
    }
    // A header change is not an action with an undo toast.
}

void UndoController::open_toast(Kind kind, LogDocument& document, EntryId entry, QString text)
{
    m_Environment.m_Scheduler.cancel(m_Timer);
    m_Kind = kind;
    m_Document = &document;
    m_Entry = entry;
    m_Repeat = 1;
    m_NoteText.clear();
    m_Text = std::move(text);
    m_Active = true;
    m_Undoing = false;
    ++m_Generation;
    ++m_Serial;

    // The window starts now: this runs when the commit has finished.
    m_Deadline = m_Environment.m_Scheduler.monotonic_now() + WINDOW;
    const std::uint64_t serial = m_Serial;
    m_Timer = m_Environment.m_Scheduler.after(WINDOW,
                                              [this, serial]
                                              {
                                                  if (m_Active && m_Serial == serial)
                                                  {
                                                      close_toast();
                                                  }
                                              });
    emit toastChanged();
}

void UndoController::close_toast()
{
    m_Environment.m_Scheduler.cancel(m_Timer);
    m_Timer = NO_TIMER;
    if (!m_Active)
    {
        return;
    }
    m_Active = false;
    m_Undoing = false;
    m_Document = nullptr;
    m_Repeat = 1;
    m_Text.clear();
    m_NoteText.clear();
    emit toastChanged();
}

void UndoController::expire_if_due()
{
    // The timer closes the toast; this is for a timer that runs late, so that the window is exact all the same.
    if (m_Active && m_Environment.m_Scheduler.monotonic_now() >= m_Deadline)
    {
        close_toast();
    }
}

void UndoController::dismiss()
{
    close_toast();
}

// -------------------------------------------------------------------------------------------------------- undo

bool UndoController::undo()
{
    expire_if_due();
    if (!m_Active || m_Undoing || m_Document == nullptr)
    {
        return false;
    }

    const std::uint64_t serial = m_Serial;
    // A note, and a tap that was held with a draft, give their text back to the composer.
    const bool is_note = m_Kind == Kind::NOTE || (m_Kind == Kind::TAP && !m_NoteText.empty());
    const bool restore = m_Kind == Kind::DELETED;
    std::string note_text = is_note ? m_NoteText : std::string();
    const std::weak_ptr<int> alive = m_Alive;

    // Set first: a completion that ran before `set_deleted` returned would otherwise find the flag unset.
    m_Undoing = true;
    auto queued = m_Document->set_deleted(
        m_Entry, !restore, LogChange::Origin::UNDO,
        [this, alive, serial, is_note, note_text = std::move(note_text)](const Result<void, StorageError>& result)
        {
            if (!alive.expired())
            {
                finish_undo(serial, is_note, note_text, result);
            }
        });
    if (!queued)
    {
        m_Undoing = false;
        return false;
    }
    if (m_Undoing)
    {
        emit toastChanged();
    }
    return true;
}

void UndoController::finish_undo(std::uint64_t serial, bool is_note, const std::string& note_text,
                                 const Result<void, StorageError>& result)
{
    const bool same_toast = m_Active && m_Serial == serial;
    if (result)
    {
        if (same_toast)
        {
            close_toast();
        }
        if (is_note && m_NoteTarget)
        {
            m_NoteTarget(note_text);
        }
        return;
    }

    // The window is not extended and the toast is not closed: the press can be repeated until the window ends.
    if (same_toast)
    {
        m_Undoing = false;
        emit toastChanged();
    }
    emit writeFailed(QStringLiteral("undo"), QStringLiteral("That could not be undone. Try again."));
}

// ------------------------------------------------------------------------------------------------------ wording

std::string UndoController::time_text(const Timestamp& when) const
{
    const TimeFormat format = m_Configuration.configuration().m_TimeFormat;
    auto local = m_Environment.m_Zones.to_local(when);
    if (!local)
    {
        local = m_Environment.m_Zones.to_local(Timestamp{when.m_Utc, "UTC"});
    }
    return local ? format_time(*local, format) : std::string();
}

std::string UndoController::type_name(const ErrorEntry& entry) const
{
    // The snapshot of the name when the entry was written; the document refuses an error entry without one.
    return entry.m_ErrorTypeLabel;
}

} // namespace ej::viewmodel
