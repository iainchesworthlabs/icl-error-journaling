// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/log_session.hpp"

#include "error_journaling/export_format.hpp"
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/header_history.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/log_repository.hpp"
#include "error_journaling/utf8.hpp"
#include "viewmodel/export_context_builder.hpp"
#include "viewmodel/recovery_notice.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <variant>

namespace ej::viewmodel
{

namespace
{

constexpr int SECONDS_PER_DAY = 24 * 60 * 60;

QString to_qstring(const std::string& text)
{
    return QString::fromStdString(text);
}

std::string to_utf8(const QString& text)
{
    return text.toStdString();
}

/// A text that cannot be an id of any entry the log holds: the entry the preview puts on its scratch copy.
EntryId preview_entry_id()
{
    return EntryId(Uuid::from_random(~std::uint64_t{0}, ~std::uint64_t{0}));
}

template <typename Id>
std::optional<Id> parse_id(const QString& text)
{
    auto uuid = Uuid::parse(to_utf8(text));
    if (!uuid)
    {
        return std::nullopt;
    }
    return Id(*uuid);
}

/// The text of a note counts in code points (Stage 2, decision 7).
bool note_text_fits(const std::string& text)
{
    const auto length = utf8_code_points(text);
    return length && *length <= MAX_NOTE_CODE_POINTS;
}

} // namespace

/// Hears the document and the configuration and passes both on to the session.
class LogSession::Listener final : public LogObserver, public ConfigurationObserver
{
public:
    explicit Listener(LogSession& session) : m_Session(session) {}

    void on_log_changed(const LogDocument&, const LogChange& change) override
    {
        // A share changes nothing the header card or the count shows.
        if (change.m_Kind != LogChange::Kind::SHARED)
        {
            emit m_Session.headerChanged();
        }
        m_Session.update_unshared();
    }

    void on_configuration_changed(const ConfigurationSource&) override
    {
        if (m_Session.m_Document)
        {
            emit m_Session.headerChanged();
        }
        // The first log is started when the configuration first becomes valid.
        if (m_Session.m_State == State::NEEDS_SETUP && m_Session.m_Configuration.configured())
        {
            m_Session.start_load();
        }
    }

private:
    LogSession& m_Session;
};

struct LogSession::Loaded
{
    std::optional<StoredLog> m_Stored;
    std::optional<Draft> m_Draft;
};

LogSession::LogSession(const Environment& environment, StorageGateway& gateway, DocumentRegistry& registry,
                       ConfigurationSource& configuration, FeedbackSink& feedback, StartupResult startup,
                       QObject* parent)
    : QObject(parent), m_Environment(environment), m_Gateway(gateway), m_Registry(registry),
      m_Configuration(configuration), m_Feedback(feedback),
      m_RecoveryNotice(ej::viewmodel::recovery_notice(startup.m_Recovery)),
      m_Listener(std::make_unique<Listener>(*this))
{
    m_Configuration.add_observer(*m_Listener);
}

LogSession::~LogSession()
{
    m_Environment.m_Scheduler.cancel(m_NewLogTimer);
    m_Environment.m_Scheduler.cancel(m_DraftTimer);
    m_Configuration.remove_observer(*m_Listener);
    if (m_Document)
    {
        // A model that still follows the open log takes itself off the document when it is told there is none. This
        // is before the strong reference to the document is released, which is the order the followers rely on.
        m_OpenLogObservers.notify([](OpenLogObserver& observer) { observer.on_open_log_changed(nullptr); });
        m_Document->remove_observer(*m_Listener);
    }
}

// ----------------------------------------------------------------------------------------------------- loading

void LogSession::load()
{
    if (m_State == State::LOADING)
    {
        start_load();
    }
}

void LogSession::set_state(State state)
{
    if (m_State != state)
    {
        m_State = state;
        emit stateChanged();
    }
}

void LogSession::start_load()
{
    if (m_Loading)
    {
        return;
    }
    m_Loading = true;
    set_state(State::LOADING);

    // With a valid configuration, today's log is started if the store has none (HDR-01). The ids and the date are taken
    // here, on the GUI thread; the job does the reading and the writing.
    struct Plan
    {
        LogId m_Id;
        HeaderValues m_Header;
    };
    std::optional<Plan> plan;
    if (m_Configuration.configured())
    {
        HeaderValues header;
        header.m_Facilitator = m_Configuration.configuration().m_Facilitator;
        header.m_LocalDate = today();
        plan = Plan{m_Environment.m_Ids.next_as<LogId>(), std::move(header)};
    }

    auto work = [plan = std::move(plan)](StorageContext& context) -> Result<Loaded, StorageError>
    {
        auto opened = context.m_Logs.open_log();
        if (!opened)
        {
            return fail(opened.error());
        }
        if (!*opened && plan)
        {
            if (auto created = context.m_Logs.create_log(plan->m_Id, plan->m_Header); !created)
            {
                return fail(created.error());
            }
            opened = context.m_Logs.open_log();
            if (!opened)
            {
                return fail(opened.error());
            }
        }
        Loaded loaded;
        loaded.m_Stored = std::move(*opened);
        // A draft that cannot be read is no reason not to open the log.
        if (auto draft = context.m_Logs.get_draft(); draft)
        {
            loaded.m_Draft = std::move(*draft);
        }
        return loaded;
    };
    auto done = [this, alive = std::weak_ptr<void>(m_Alive)](Result<Loaded, StorageError> result)
    {
        if (!alive.expired())
        {
            finish_load(std::move(result));
        }
    };
    m_Gateway.run(std::move(work), std::move(done));
}

void LogSession::finish_load(Result<Loaded, StorageError> result)
{
    m_Loading = false;
    if (!result)
    {
        set_state(State::FAILED);
        fail_write("load", tr("The log could not be opened."));
        return;
    }
    if (!result->m_Stored)
    {
        // No open log and no configuration to start one from. The configuration may have arrived while the job ran.
        set_state(State::NEEDS_SETUP);
        if (m_Configuration.configured())
        {
            start_load();
        }
        return;
    }

    m_Document = m_Registry.make_document(std::move(*result->m_Stored));
    m_Document->add_observer(*m_Listener);

    // What the person typed while the log was loading wins over the stored draft.
    if (result->m_Draft && !result->m_Draft->m_Text.empty() && m_Draft.isEmpty())
    {
        m_Draft = to_qstring(result->m_Draft->m_Text);
        emit draftChanged();
    }

    set_state(State::OPEN);
    LogDocument* const document = m_Document.get();
    m_OpenLogObservers.notify([document](OpenLogObserver& observer) { observer.on_open_log_changed(document); });
    emit headerChanged();
    update_unshared();
}

// ------------------------------------------------------------------------------------------------- properties

LogSession::State LogSession::state() const
{
    return m_State;
}

HeaderValues LogSession::header_in_force() const
{
    if (!m_Document)
    {
        return HeaderValues{};
    }
    return HeaderHistory(m_Document->log()).at(m_Environment.m_Clock.now());
}

QString LogSession::facilitator() const
{
    return to_qstring(header_in_force().m_Facilitator);
}

QString LogSession::unit_text() const
{
    const HeaderValues header = header_in_force();
    if (!header.m_UnitNumber)
    {
        return {};
    }
    if (!m_Configuration.configured())
    {
        return to_qstring(std::string(header.m_UnitNumber->digits()));
    }
    return to_qstring(header.m_UnitNumber->format(m_Configuration.configuration().m_UnitPrefix));
}

QString LogSession::unit_digits() const
{
    const HeaderValues header = header_in_force();
    return header.m_UnitNumber ? to_qstring(std::string(header.m_UnitNumber->digits())) : QString{};
}

void LogSession::update_unshared()
{
    const bool unshared = m_Document && m_Document->ready() && !m_Document->last_shared() && live_entry_count() > 0;
    if (unshared != m_Unshared)
    {
        m_Unshared = unshared;
        emit unsharedChanged();
    }
}

void LogSession::announce_record_failed(const QString& type_id, const QString& type_name)
{
    // The apostrophe is typographic (U+2019), as in the designs.
    emit recordFailed(type_id, type_name, tr("%1 wasn\xE2\x80\x99t saved").arg(type_name),
                      tr("Nothing was added to the log. Tap %1 again to log it.").arg(type_name));
}

QString LogSession::location_text() const
{
    const HeaderValues header = header_in_force();
    if (!header.m_Location)
    {
        return {};
    }
    if (header.m_Location->m_Id)
    {
        for (const Location& location : m_Configuration.configuration().m_Locations)
        {
            if (location.m_Id == *header.m_Location->m_Id)
            {
                return to_qstring(location.m_Name);
            }
        }
        return {};
    }
    return to_qstring(header.m_Location->m_Text);
}

QString LogSession::version_text() const
{
    const HeaderValues header = header_in_force();
    return header.m_SoftwareVersion ? to_qstring(header.m_SoftwareVersion->format()) : QString{};
}

QString LogSession::date_text() const
{
    if (!m_Document)
    {
        return {};
    }
    // "Sat 7 Nov": fixed English like the export's month names, so it does not move with the library's locale data.
    static constexpr std::array<const char*, 7> WEEKDAYS = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const std::chrono::year_month_day date = m_Document->log().initial_header().m_LocalDate;
    if (!date.ok())
    {
        return {};
    }
    const unsigned weekday = std::chrono::weekday{std::chrono::sys_days{date}}.c_encoding();
    const unsigned month = static_cast<unsigned>(date.month());
    return QStringLiteral("%1 %2 %3")
        .arg(QLatin1String(WEEKDAYS.at(weekday)))
        .arg(static_cast<unsigned>(date.day()))
        .arg(QLatin1String(export_format::MONTH_NAMES[month - 1].data(),
                           static_cast<qsizetype>(export_format::MONTH_NAMES[month - 1].size())));
}

QStringList LogSession::missing_fields() const
{
    QStringList missing;
    if (!m_Document)
    {
        return missing;
    }
    const HeaderValues header = header_in_force();
    const auto add = [&missing](HeaderField field)
    { missing.push_back(to_qstring(std::string(header_field_key(field)))); };
    if (!header.m_UnitNumber)
    {
        add(HeaderField::UNIT_NUMBER);
    }
    if (!header.m_Location || (!header.m_Location->m_Id && header.m_Location->m_Text.empty()))
    {
        add(HeaderField::LOCATION);
    }
    if (!header.m_SoftwareVersion)
    {
        add(HeaderField::SOFTWARE_VERSION);
    }
    return missing;
}

int LogSession::live_entry_count() const
{
    if (!m_Document)
    {
        return 0;
    }
    const auto entries = m_Document->log().entries();
    return static_cast<int>(
        std::count_if(entries.begin(), entries.end(), [](const Entry& entry) { return !entry.m_Deleted; }));
}

int LogSession::now_local_seconds() const
{
    const auto local = m_Environment.m_Zones.to_local(now_timestamp());
    if (!local)
    {
        return 0;
    }
    return static_cast<int>(local->m_SinceMidnight.count());
}

QString LogSession::draft() const
{
    return m_Draft;
}

int LogSession::pending_writes() const
{
    return m_OwnPending + (m_StartingNewLog ? 1 : 0);
}

QString LogSession::recovery_notice() const
{
    return m_RecoveryNotice;
}

// ----------------------------------------------------------------------------------------------- time and day

Timestamp LogSession::now_timestamp() const
{
    return Timestamp{m_Environment.m_Clock.now(), m_Environment.m_Zone.current()};
}

std::chrono::year_month_day LogSession::today() const
{
    const Timestamp now = now_timestamp();
    if (const auto local = m_Environment.m_Zones.to_local(now); local)
    {
        return local->m_Date;
    }
    // An unknown zone name: the UTC date, which is wrong by hours at worst and is never a refusal to start a log.
    return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(now.m_Utc)};
}

std::string LogSession::facilitator_for_new_log() const
{
    if (m_Configuration.configured())
    {
        return m_Configuration.configuration().m_Facilitator;
    }
    return m_Document ? m_Document->log().initial_header().m_Facilitator : std::string{};
}

// -------------------------------------------------------------------------------------------------- commands

bool LogSession::can_write(const char* operation)
{
    if (m_State != State::OPEN || !m_Document)
    {
        return false;
    }
    if (m_StartingNewLog)
    {
        fail_write(operation, tr("A new log is being started. Try again in a moment."));
        return false;
    }
    return true;
}

void LogSession::fail_write(const char* operation, const QString& message)
{
    emit writeFailed(QString::fromLatin1(operation), message);
}

LogDocument::Done LogSession::make_done(const char* operation, Finished finished)
{
    return [this, alive = std::weak_ptr<void>(m_Alive), operation,
            finished = std::move(finished)](const Result<void, StorageError>& result) mutable
    {
        if (!alive.expired())
        {
            command_finished(operation, result, finished);
        }
    };
}

void LogSession::command_finished(const char* operation, const Result<void, StorageError>& result, Finished& finished)
{
    --m_OwnPending;
    if (!result)
    {
        fail_write(operation, tr("That could not be saved. Please try again."));
    }
    if (finished)
    {
        finished(result.has_value());
    }
    emit pendingWritesChanged();
}

bool LogSession::settle(const char* operation, const Result<void, DocumentError>& submitted)
{
    if (!submitted)
    {
        --m_OwnPending;
        if (submitted.error().m_Code == DocumentError::Code::QUEUE_FULL)
        {
            fail_write(operation, tr("Too many changes are waiting to be saved. Try again in a moment."));
        }
        return false;
    }
    emit pendingWritesChanged();
    return true;
}

bool LogSession::recordError(const QString& errorTypeId)
{
    constexpr const char* OPERATION = "recordError";
    if (!can_write(OPERATION))
    {
        return false;
    }
    const auto id = parse_id<ErrorTypeId>(errorTypeId);
    if (!id)
    {
        return false;
    }
    const auto& types = m_Configuration.configuration().m_ErrorTypes;
    const auto type = std::ranges::find_if(types, [&id](const ErrorType& candidate) { return candidate.m_Id == *id; });
    if (type == types.end() || type->m_Retired)
    {
        return false;
    }

    // The instant and the id of the tap are taken now, not when the commit finishes.
    Entry entry;
    entry.m_Id = m_Environment.m_Ids.next_as<EntryId>();
    entry.m_Captured = now_timestamp();
    entry.m_Body = ErrorEntry{type->m_Id, std::nullopt, type->m_Name};
    const EntryId entry_id = entry.m_Id;

    ++m_OwnPending;
    const QString type_id = to_qstring(type->m_Id.format());
    const QString type_name = to_qstring(type->m_Name);
    auto done = make_done(OPERATION,
                          [this, entry_id, type_id, type_name](bool succeeded)
                          {
                              if (succeeded)
                              {
                                  m_Feedback.entry_recorded(entry_id);
                                  emit entryRecorded(to_qstring(entry_id.format()));
                              }
                              else
                              {
                                  announce_record_failed(type_id, type_name);
                              }
                          });
    const bool accepted =
        settle(OPERATION, m_Document->add_entry(std::move(entry), LogChange::Origin::USER, std::move(done)));
    if (!accepted)
    {
        announce_record_failed(type_id, type_name);
    }
    return accepted;
}

bool LogSession::addNote(const QString& text)
{
    const QString trimmed = text.trimmed();
    const std::string value = to_utf8(trimmed);
    if (value.empty() || !note_text_fits(value))
    {
        return false;
    }
    return add_note_entry("addNote", NoteEntry{std::nullopt, value}, text, true);
}

bool LogSession::addTemplatedNote(const QString& templateId, const QString& value)
{
    const std::string text = to_utf8(value.trimmed());
    if (text.empty() || !note_text_fits(text))
    {
        return false;
    }
    const auto id = parse_id<NoteTemplateId>(templateId);
    if (!id)
    {
        return false;
    }
    const auto& templates = m_Configuration.configuration().m_Templates;
    if (std::ranges::none_of(templates, [&id](const NoteTemplate& candidate) { return candidate.m_Id == *id; }))
    {
        return false;
    }
    return add_note_entry("addTemplatedNote", NoteEntry{*id, text}, QString{}, false);
}

bool LogSession::add_note_entry(const char* operation, NoteEntry body, const QString& submitted, bool free_text)
{
    if (!can_write(operation))
    {
        return false;
    }
    Entry entry;
    entry.m_Id = m_Environment.m_Ids.next_as<EntryId>();
    entry.m_Captured = now_timestamp();
    entry.m_Body = std::move(body);
    const EntryId entry_id = entry.m_Id;
    const std::uint64_t revision = m_DraftRevision;

    ++m_OwnPending;
    auto done = make_done(operation,
                          [this, entry_id, free_text, submitted, revision](bool succeeded)
                          {
                              if (succeeded)
                              {
                                  m_Feedback.note_saved(entry_id);
                              }
                              note_finished(succeeded, free_text, submitted, revision);
                          });
    if (!settle(operation, m_Document->add_entry(std::move(entry), LogChange::Origin::USER, std::move(done))))
    {
        return false;
    }
    // The commit clears the stored draft in the same transaction, so the text that is being committed must not be
    // written to the store again by the debounce.
    if (free_text && m_Draft == submitted)
    {
        m_Environment.m_Scheduler.cancel(m_DraftTimer);
        m_DraftTimer = NO_TIMER;
        m_DraftDirty = false;
    }
    return true;
}

void LogSession::note_finished(bool succeeded, bool free_text, const QString& submitted, std::uint64_t revision)
{
    if (succeeded && free_text && m_DraftRevision == revision && m_Draft == submitted)
    {
        // The store has cleared the draft with the commit. Whatever was typed meanwhile is not this text.
        m_Draft.clear();
        m_DraftDirty = false;
        emit draftChanged();
        return;
    }
    if (!m_Draft.isEmpty())
    {
        // The draft that remains is not in the store: a commit clears it, and a failed commit stopped the debounce
        // from writing it. Write it now (after the commit, in the gateway's order).
        m_DraftDirty = true;
        write_draft_now();
    }
}

// ----------------------------------------------------------------------------------------------------- header

QString LogSession::header_message(const HeaderError& error) const
{
    using Kind = HeaderError::Kind;
    switch (error.m_Kind)
    {
    case Kind::INVALID_UNIT_NUMBER:
        switch (error.m_UnitNumber.value_or(UnitNumber::Error::NOT_DIGITS))
        {
        case UnitNumber::Error::EMPTY:
            return tr("Enter a unit number.");
        case UnitNumber::Error::NOT_DIGITS:
            return tr("A unit number uses digits only.");
        case UnitNumber::Error::LEADING_ZERO:
            return tr("A unit number does not start with 0.");
        case UnitNumber::Error::TOO_LONG:
            return tr("A unit number has at most 9 digits.");
        }
        break;
    case Kind::INVALID_SOFTWARE_VERSION:
        switch (error.m_SoftwareVersion.value_or(SoftwareVersion::Error::NOT_DIGITS))
        {
        case SoftwareVersion::Error::EMPTY:
            return tr("Enter a software version, like 1.4.2.");
        case SoftwareVersion::Error::WRONG_COMPONENT_COUNT:
            return tr("Use three numbers separated by dots, like 1.4.2.");
        case SoftwareVersion::Error::NOT_DIGITS:
            return tr("Use digits and dots only, like 1.4.2.");
        case SoftwareVersion::Error::LEADING_ZERO:
            return tr("A number in a version does not start with 0.");
        case SoftwareVersion::Error::TOO_LONG:
            return tr("A number in a version has at most 9 digits.");
        }
        break;
    case Kind::EMPTY_LOCATION:
        return tr("Enter a location.");
    case Kind::UNCHANGED:
        return tr("That is already the current value.");
    case Kind::UNKNOWN_ENTRY:
    case Kind::NOT_A_HEADER_CHANGE:
    case Kind::DUPLICATE_ID:
        break;
    }
    return tr("That value is not accepted.");
}

QString LogSession::validateHeaderValue(const QString& fieldKey, const QString& text) const
{
    const auto field = header_field_from_key(to_utf8(fieldKey));
    if (!field)
    {
        return tr("That field cannot be changed.");
    }
    const std::string value = to_utf8(text.trimmed());
    HeaderValues scratch;
    if (const auto set = set_header_field(scratch, *field, value); !set)
    {
        return header_message(set.error());
    }
    if (*field == HeaderField::LOCATION)
    {
        const auto length = utf8_code_points(value);
        if (!length || *length > MAX_HEADER_TEXT_CODE_POINTS)
        {
            return tr("That location is too long.");
        }
    }
    return {};
}

bool LogSession::correctHeader(const QString& fieldKey, const QString& text)
{
    constexpr const char* OPERATION = "correctHeader";
    if (!can_write(OPERATION))
    {
        return false;
    }
    const auto field = header_field_from_key(to_utf8(fieldKey));
    if (!field)
    {
        return false;
    }
    ++m_OwnPending;
    auto done = make_done(OPERATION, Finished{});
    return settle(OPERATION, m_Document->correct_header(*field, to_utf8(text.trimmed()), std::move(done)));
}

std::optional<Timestamp> LogSession::timestamp_for_local(int local_seconds) const
{
    if (!m_Document || local_seconds < 0 || local_seconds >= SECONDS_PER_DAY)
    {
        return std::nullopt;
    }
    // The change takes the zone the device is in now, like every entry; the time is read on the log's own date.
    std::string zone = m_Environment.m_Zone.current();
    const LocalTime local{m_Document->log().initial_header().m_LocalDate, std::chrono::seconds(local_seconds)};
    const auto utc = m_Environment.m_Zones.to_utc(local, zone);
    if (!utc)
    {
        return std::nullopt;
    }
    return Timestamp{*utc, std::move(zone)};
}

bool LogSession::change_header_at(const QString& field_key, const QString& text, Timestamp when)
{
    constexpr const char* OPERATION = "changeHeader";
    if (!can_write(OPERATION))
    {
        return false;
    }
    const auto field = header_field_from_key(to_utf8(field_key));
    if (!field)
    {
        return false;
    }
    const EntryId id = m_Environment.m_Ids.next_as<EntryId>();
    ++m_OwnPending;
    auto done = make_done(OPERATION, Finished{});
    return settle(OPERATION,
                  m_Document->change_header(*field, to_utf8(text.trimmed()), std::move(when), id, std::move(done)));
}

bool LogSession::changeHeader(const QString& fieldKey, const QString& text, int localSeconds)
{
    auto when = timestamp_for_local(localSeconds);
    if (!when)
    {
        return false;
    }
    return change_header_at(fieldKey, text, std::move(*when));
}

bool LogSession::changeHeaderAtUtc(const QString& fieldKey, const QString& text, qint64 atUtcMs)
{
    Timestamp when = now_timestamp();
    if (atUtcMs != 0)
    {
        when.m_Utc = UtcMillis{std::chrono::milliseconds(atUtcMs)};
    }
    return change_header_at(fieldKey, text, std::move(when));
}

QString LogSession::previewHeaderChange(const QString& fieldKey, const QString& text, int localSeconds) const
{
    if (m_State != State::OPEN || !m_Document)
    {
        return tr("There is no log to change yet.");
    }
    if (const QString refusal = validateHeaderValue(fieldKey, text); !refusal.isEmpty())
    {
        return refusal;
    }
    const auto field = header_field_from_key(to_utf8(fieldKey));
    const auto when = timestamp_for_local(localSeconds);
    if (!field || !when)
    {
        return tr("That time is not valid.");
    }

    // The change is applied to a copy of the mirror, and the copy is rendered by the code that renders the timeline.
    Log scratch = m_Document->log();
    const EntryId id = preview_entry_id();
    if (const auto changed = change_header(scratch, *field, to_utf8(text.trimmed()), *when, id); !changed)
    {
        return header_message(changed.error());
    }
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment.m_Zones).build();
    const auto line = render_entry_line(scratch, id, context);
    if (!line)
    {
        return tr("This change cannot be shown yet.");
    }
    return to_qstring(*line);
}

// ------------------------------------------------------------------------------------------------------- draft

void LogSession::set_draft(const QString& text)
{
    if (text == m_Draft)
    {
        return;
    }
    m_Draft = text;
    ++m_DraftRevision;
    m_DraftDirty = true;
    emit draftChanged();
    arm_draft_timer();
}

void LogSession::arm_draft_timer()
{
    m_Environment.m_Scheduler.cancel(m_DraftTimer);
    m_DraftTimer = m_Environment.m_Scheduler.after(DRAFT_DEBOUNCE,
                                                   [this, alive = std::weak_ptr<void>(m_Alive)]
                                                   {
                                                       if (!alive.expired())
                                                       {
                                                           m_DraftTimer = NO_TIMER;
                                                           write_draft_now();
                                                       }
                                                   });
}

void LogSession::flushDraft()
{
    write_draft_now();
}

void LogSession::write_draft_now()
{
    m_Environment.m_Scheduler.cancel(m_DraftTimer);
    m_DraftTimer = NO_TIMER;
    if (!m_DraftDirty)
    {
        return;
    }
    m_DraftDirty = false;
    auto work = [text = to_utf8(m_Draft), when = m_Environment.m_Clock.now()](StorageContext& context)
    { return context.m_Logs.set_draft(text, when); };
    auto done = [this, alive = std::weak_ptr<void>(m_Alive)](Result<void, StorageError> result)
    {
        if (!alive.expired() && !result)
        {
            fail_write("saveDraft", tr("Your note could not be kept. Please try again."));
        }
    };
    m_Gateway.run(std::move(work), std::move(done));
}

// --------------------------------------------------------------------------------------------------- new log

bool LogSession::startNewLog()
{
    if (m_State != State::OPEN || !m_Document || m_StartingNewLog)
    {
        return false;
    }
    m_StartingNewLog = true;
    emit pendingWritesChanged();

    // The new log's header: the facilitator and today's date, nothing else (HDR-05).
    HeaderValues header;
    header.m_Facilitator = facilitator_for_new_log();
    header.m_LocalDate = today();
    attempt_new_log(m_Environment.m_Ids.next_as<LogId>(), std::move(header));
    return true;
}

void LogSession::attempt_new_log(LogId new_id, HeaderValues header)
{
    // The writes that were accepted before this go first: they belong to the log being archived, and a command still
    // waiting in the document when it is replaced would be lost.
    if (m_Document->pending() > 0)
    {
        m_NewLogTimer = m_Environment.m_Scheduler.after(
            NEW_LOG_POLL,
            [this, alive = std::weak_ptr<void>(m_Alive), new_id, header = std::move(header)]() mutable
            {
                if (!alive.expired())
                {
                    m_NewLogTimer = NO_TIMER;
                    attempt_new_log(new_id, std::move(header));
                }
            });
        return;
    }

    const LogId old_id = m_Document->id();
    auto work = [old_id, new_id, header = std::move(header)](StorageContext& context) -> Result<StoredLog, StorageError>
    {
        if (auto started = context.m_Logs.start_new_log(old_id, new_id, header); !started)
        {
            return fail(started.error());
        }
        if (auto loaded = context.m_Logs.load_log(new_id); loaded)
        {
            return std::move(*loaded);
        }
        // The new log is there; only reading it back failed. It starts as it was made.
        return StoredLog{LogMeta{new_id, UtcMillis{}, std::nullopt, false, old_id}, Log(header)};
    };
    auto done = [this, alive = std::weak_ptr<void>(m_Alive)](Result<StoredLog, StorageError> result)
    {
        if (alive.expired())
        {
            return;
        }
        m_StartingNewLog = false;
        if (!result)
        {
            // The store keeps the old log open when it fails.
            fail_write("startNewLog", tr("A new log could not be started. Your current log is unchanged."));
            emit pendingWritesChanged();
            return;
        }
        m_Document->replace(std::move(*result));
        emit pendingWritesChanged();
        emit newLogStarted();
    };
    m_Gateway.run(std::move(work), std::move(done));
}

// --------------------------------------------------------------------------------------------- OpenLogSource

LogDocument* LogSession::open_log() const
{
    return m_Document.get();
}

void LogSession::add_observer(OpenLogObserver& observer)
{
    m_OpenLogObservers.add(observer);
}

void LogSession::remove_observer(OpenLogObserver& observer)
{
    m_OpenLogObservers.remove(observer);
}

} // namespace ej::viewmodel
