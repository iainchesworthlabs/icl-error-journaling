// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/day_watcher.hpp"

#include "error_journaling/day_prompt.hpp"
#include "error_journaling/timestamp.hpp"

#include <QDate>
#include <QLocale>
#include <algorithm>
#include <chrono>
#include <utility>

namespace ej::viewmodel
{

namespace
{

/// The wording is English (Australia) whatever language the device is set to: the log and its export are in English.
QLocale prompt_locale()
{
    return QLocale(QLocale::English, QLocale::Australia);
}

QString format_date(std::chrono::year_month_day date, const QString& format)
{
    if (!date.ok())
    {
        return {};
    }
    const QDate qdate(static_cast<int>(date.year()), static_cast<int>(static_cast<unsigned>(date.month())),
                      static_cast<int>(static_cast<unsigned>(date.day())));
    return prompt_locale().toString(qdate, format);
}

/// What a new log would archive: the live entries, every kind (the figure `LogSession::liveEntryCount` gives).
int live_entry_count(const LogDocument& document)
{
    const auto entries = document.log().entries();
    return static_cast<int>(
        std::count_if(entries.begin(), entries.end(), [](const Entry& entry) { return !entry.m_Deleted; }));
}

}  // namespace

DayWatcher::DayWatcher(const Environment& environment, LogSession& session, ShareService& share, QObject* parent)
    : QObject(parent), m_Environment(environment), m_Session(session), m_Share(share),
      m_Log(session, [this](const LogChange& change) { on_open_log_change(change); })
{
    connect(&m_Session, &LogSession::stateChanged, this, &DayWatcher::on_session_state_changed);
}

DayWatcher::~DayWatcher() = default;

// ------------------------------------------------------------------------------------------------------ invokables

bool DayWatcher::acceptNewLog()
{
    if (!m_Prompt.m_Visible)
    {
        return false;
    }
    return m_Session.startNewLog();
}

void DayWatcher::keepOpen()
{
    hide();
}

bool DayWatcher::shareFirst()
{
    return m_Share.shareOpenLog();
}

// ------------------------------------------------------------------------------------------------------- the check

void DayWatcher::check()
{
    if (ready_document() == nullptr)
    {
        // The log is still loading (or there is none yet): the check is made once the session is `OPEN`.
        m_CheckWaiting = true;
        return;
    }
    m_CheckWaiting = false;
    run_check();
}

void DayWatcher::run_check()
{
    const LogDocument* const document = ready_document();
    if (document == nullptr)
    {
        hide();
        return;
    }

    const Timestamp now{m_Environment.m_Clock.now(), m_Environment.m_Zone.current()};
    const std::chrono::year_month_day log_date = document->log().initial_header().m_LocalDate;
    const auto verdict = day_prompt(log_date, now, m_Environment.m_Zones);
    if (!verdict)
    {
        hide();
        report_failure();
        return;
    }
    m_FailureReported = false;

    if (*verdict != DayPrompt::OFFER_NEW_LOG)
    {
        hide();
        return;
    }
    const auto today = local_date(now, m_Environment.m_Zones);
    if (!today)
    {
        hide();
        report_failure();
        return;
    }

    Prompt prompt;
    prompt.m_Visible = true;
    prompt.m_NewDateText = format_date(*today, QStringLiteral("dddd d MMMM"));
    prompt.m_OpenLogDayText = format_date(log_date, QStringLiteral("dddd"));
    prompt.m_EntryCount = live_entry_count(*document);
    prompt.m_Unshared = !document->last_shared() && prompt.m_EntryCount > 0;
    show(std::move(prompt));
}

void DayWatcher::report_failure()
{
    if (m_FailureReported)
    {
        return;
    }
    m_FailureReported = true;
    emit checkFailed(tr("The date could not be checked."));
}

const LogDocument* DayWatcher::ready_document() const
{
    if (m_Session.state() != LogSession::State::OPEN)
    {
        return nullptr;
    }
    const LogDocument* const document = m_Session.open_log();
    return document != nullptr && document->ready() ? document : nullptr;
}

// ------------------------------------------------------------------------------------------------ what is heard

void DayWatcher::on_session_state_changed()
{
    if (m_Session.state() == LogSession::State::OPEN)
    {
        if (m_CheckWaiting)
        {
            m_CheckWaiting = false;
            run_check();
        }
    }
    else
    {
        hide();
    }
}

void DayWatcher::on_open_log_change(const LogChange& change)
{
    if (!m_Prompt.m_Visible)
    {
        return;
    }
    if (m_Log.document() == nullptr)
    {
        hide();
        return;
    }
    if (change.m_Kind == LogChange::Kind::REPLACED)
    {
        // Another log is the open one (a new day started, here or elsewhere): is the prompt still true of it?
        run_check();
        return;
    }
    refresh_content();
}

void DayWatcher::refresh_content()
{
    const LogDocument* const document = ready_document();
    if (document == nullptr)
    {
        return;
    }
    Prompt prompt = m_Prompt;
    prompt.m_EntryCount = live_entry_count(*document);
    prompt.m_Unshared = !document->last_shared() && prompt.m_EntryCount > 0;
    show(std::move(prompt));
}

// ---------------------------------------------------------------------------------------------------------- state

void DayWatcher::hide()
{
    if (m_Prompt.m_Visible)
    {
        Prompt prompt = m_Prompt;
        prompt.m_Visible = false;
        show(std::move(prompt));
    }
}

void DayWatcher::show(Prompt prompt)
{
    if (prompt == m_Prompt)
    {
        return;
    }
    m_Prompt = std::move(prompt);
    emit promptChanged();
}

}  // namespace ej::viewmodel
