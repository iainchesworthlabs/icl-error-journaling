// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/log_session.hpp"

#include "error_journaling/export_format.hpp"
#include "error_journaling/extra_versions.hpp"
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/header_history.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/log_repository.hpp"
#include "error_journaling/utf8.hpp"
#include "viewmodel/export_context_builder.hpp"
#include "viewmodel/interaction_types.hpp"
#include "viewmodel/recovery_notice.hpp"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>
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
        header.m_Tester = m_Configuration.configuration().m_Tester;
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

QString LogSession::tester() const
{
    return to_qstring(header_in_force().m_Tester);
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
        return to_qstring(header.m_UnitNumber->stored());
    }
    return to_qstring(header.m_UnitNumber->format(m_Configuration.configuration().m_UnitPrefix));
}

QString LogSession::unit_environment() const
{
    const HeaderValues header = header_in_force();
    return header.m_UnitNumber ? to_qstring(std::string(header.m_UnitNumber->environment())) : QString{};
}

QString LogSession::unit_digits() const
{
    const HeaderValues header = header_in_force();
    return header.m_UnitNumber ? to_qstring(std::string(header.m_UnitNumber->digits())) : QString{};
}

void LogSession::update_unshared()
{
    const bool ready = m_Document && m_Document->ready();
    const bool unshared = ready && is_unshared(m_Document->meta(), static_cast<std::size_t>(live_entry_count()));
    const bool changed = unshared && m_Document->last_shared().has_value();
    if (unshared != m_Unshared || changed != m_ChangedSinceShare)
    {
        m_Unshared = unshared;
        m_ChangedSinceShare = changed;
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

QString LogSession::commit_text() const
{
    const HeaderValues header = header_in_force();
    return header.m_SoftwareVersion ? to_qstring(std::string(header.m_SoftwareVersion->commit())) : QString{};
}

QString LogSession::version_display_text() const
{
    const HeaderValues header = header_in_force();
    return header.m_SoftwareVersion ? to_qstring(header.m_SoftwareVersion->display()) : QString{};
}

QVariantList LogSession::extra_versions() const
{
    QVariantList rows;
    for (const auto& extra : header_in_force().m_ExtraVersions)
    {
        QVariantMap row;
        row.insert(QStringLiteral("label"), to_qstring(std::string(extra.label())));
        row.insert(QStringLiteral("version"), to_qstring(extra.m_Version.format()));
        row.insert(QStringLiteral("commit"), to_qstring(std::string(extra.m_Version.commit())));
        rows.append(row);
    }
    return rows;
}

QString LogSession::extra_versions_text() const
{
    return to_qstring(summarize_extra_versions(header_in_force().m_ExtraVersions));
}

QString LogSession::versions_summary() const
{
    const HeaderValues header = header_in_force();
    std::string out;
    if (header.m_SoftwareVersion)
    {
        out = std::string(ROBOT_LABEL) + " " + header.m_SoftwareVersion->display();
    }
    if (const std::string extras = summarize_extra_versions(header.m_ExtraVersions); !extras.empty())
    {
        out += out.empty() ? extras : " · " + extras;
    }
    return to_qstring(out);
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
    // The commit is optional (HDR-09): the banner reminds, and nothing else looks at this key.
    if (!header.m_SoftwareVersion || header.m_SoftwareVersion->commit().empty())
    {
        missing.push_back(QStringLiteral("software_commit"));
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

std::string LogSession::tester_for_new_log() const
{
    if (m_Configuration.configured())
    {
        return m_Configuration.configuration().m_Tester;
    }
    return m_Document ? m_Document->log().initial_header().m_Tester : std::string{};
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
    return record_error(errorTypeId).has_value();
}

QString LogSession::recordErrorEntry(const QString& errorTypeId)
{
    const auto id = record_error(errorTypeId);
    return id ? to_qstring(id->format()) : QString();
}

std::optional<EntryId> LogSession::record_error(const QString& errorTypeId)
{
    constexpr const char* OPERATION = "recordError";
    if (!can_write(OPERATION))
    {
        return std::nullopt;
    }
    const auto id = parse_id<ErrorTypeId>(errorTypeId);
    if (!id)
    {
        return std::nullopt;
    }
    const auto& types = m_Configuration.configuration().m_ErrorTypes;
    const auto type = std::ranges::find_if(types, [&id](const ErrorType& candidate) { return candidate.m_Id == *id; });
    if (type == types.end() || type->m_Retired)
    {
        return std::nullopt;
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
        return std::nullopt;
    }
    return entry_id;
}

bool LogSession::attachDraft(const QString& entryId)
{
    constexpr const char* OPERATION = "attachDraft";
    if (!can_write(OPERATION))
    {
        return false;
    }
    const auto id = parse_id<EntryId>(entryId);
    const std::string text = to_utf8(m_Draft.trimmed());
    if (!id || text.empty() || !note_text_fits(text))
    {
        return false;
    }
    // The mirror holds an entry from its commit on; the open log is the only one this session writes.
    const Entry* found = m_Document->log().find(*id);
    if (found == nullptr || found->m_Deleted)
    {
        return false;
    }
    // Worked out against the entry as it is when the command is reached, so an edit still in flight is not overwritten.
    const auto attach = [text](Entry& entry) -> std::optional<std::string>
    {
        if (auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
        {
            if (error->m_AttachedNote.has_value())
            {
                return "That entry has a note already.";
            }
            error->m_AttachedNote = text;
            return std::nullopt;
        }
        if (auto* interaction = std::get_if<InteractionEntry>(&entry.m_Body))
        {
            if (interaction->m_AttachedNote.has_value())
            {
                return "That entry has a note already.";
            }
            interaction->m_AttachedNote = text;
            return std::nullopt;
        }
        return "Only an error or an interaction takes a note.";
    };
    {
        Entry probe = *found;
        if (attach(probe))
        {
            return false;
        }
    }

    const QString submitted = m_Draft;
    const std::uint64_t revision = m_DraftRevision;
    ++m_OwnPending;
    auto done = make_done(OPERATION,
                          [this, entry = *id, text, submitted, revision](bool succeeded)
                          {
                              if (succeeded)
                              {
                                  m_Feedback.note_saved(entry);
                                  draft_attached(to_qstring(entry.format()), to_qstring(text), submitted, revision);
                              }
                          });
    return settle(OPERATION, m_Document->edit_entry(*id, LogDocument::Mutator(attach), std::move(done)));
}

void LogSession::draft_attached(const QString& entry_id, const QString& text, const QString& submitted,
                                std::uint64_t revision)
{
    // The commit does not clear the stored draft (an edit does not, a note entry does), so the empty draft is written
    // now. Text typed while the commit ran is not the text that was attached, and is kept.
    if (m_DraftRevision == revision && m_Draft == submitted)
    {
        set_draft(QString());
        write_draft_now();
    }
    emit draftAttached(entry_id, text);
}

QString LogSession::location_id() const
{
    const HeaderValues header = header_in_force();
    if (!header.m_Location || !header.m_Location->m_Id)
    {
        return {};
    }
    return to_qstring(header.m_Location->m_Id->format());
}

QString LogSession::recordInteractionEntry(const QString& typeId)
{
    constexpr const char* OPERATION = "recordInteraction";
    if (!can_write(OPERATION))
    {
        return {};
    }
    const auto id = parse_id<InteractionTypeId>(typeId);
    if (!id)
    {
        return {};
    }
    const auto types = interaction_types_in_force(m_Configuration.configuration());
    const auto type = std::ranges::find_if(types, [&id](const InteractionType& t) { return t.m_Id == *id; });
    if (type == types.end() || type->m_Retired)
    {
        return {};
    }
    Entry entry;
    entry.m_Id = m_Environment.m_Ids.next_as<EntryId>();
    entry.m_Captured = now_timestamp();
    entry.m_Body = InteractionEntry{type->m_Id, type->m_Name, {}, std::nullopt, std::nullopt};
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
    if (!settle(OPERATION, m_Document->add_entry(std::move(entry), LogChange::Origin::USER, std::move(done))))
    {
        announce_record_failed(type_id, type_name);
        return {};
    }
    return to_qstring(entry_id.format());
}

bool LogSession::setInteractionPeople(const QString& entryId, const QStringList& personIds)
{
    constexpr const char* OPERATION = "setInteractionPeople";
    if (!can_write(OPERATION))
    {
        return false;
    }
    const auto id = parse_id<EntryId>(entryId);
    if (!id)
    {
        return false;
    }
    const Entry* found = m_Document->log().find(*id);
    if (found == nullptr || found->m_Deleted || !std::holds_alternative<InteractionEntry>(found->m_Body))
    {
        return false;
    }
    std::vector<PersonId> people;
    for (const QString& text : personIds)
    {
        const auto person = parse_id<PersonId>(text);
        if (!person || !add_person_pick(people, *person))
        {
            return false;
        }
    }
    ++m_OwnPending;
    const auto put = [people = std::move(people)](Entry& entry) -> std::optional<std::string>
    {
        auto* interaction = std::get_if<InteractionEntry>(&entry.m_Body);
        if (interaction == nullptr)
        {
            return "That entry is not an interaction.";
        }
        interaction->m_People = people;
        return std::nullopt;
    };
    return settle(OPERATION, m_Document->edit_entry(*id, LogDocument::Mutator(put), make_done(OPERATION, {})));
}

bool LogSession::setInteractionValue(const QString& entryId, const QString& value)
{
    constexpr const char* OPERATION = "setInteractionValue";
    if (!can_write(OPERATION))
    {
        return false;
    }
    const auto id = parse_id<EntryId>(entryId);
    const std::string text = to_utf8(value.trimmed());
    const auto length = utf8_code_points(text);
    if (!id || text.empty() || !length || *length > MAX_INTERACTION_VALUE_CODE_POINTS)
    {
        return false;
    }
    const Entry* found = m_Document->log().find(*id);
    if (found == nullptr || found->m_Deleted || !std::holds_alternative<InteractionEntry>(found->m_Body))
    {
        return false;
    }
    ++m_OwnPending;
    const auto put = [text](Entry& entry) -> std::optional<std::string>
    {
        auto* interaction = std::get_if<InteractionEntry>(&entry.m_Body);
        if (interaction == nullptr)
        {
            return "That entry is not an interaction.";
        }
        interaction->m_Value = text;
        return std::nullopt;
    };
    return settle(OPERATION, m_Document->edit_entry(*id, LogDocument::Mutator(put), make_done(OPERATION, {})));
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
        case UnitNumber::Error::BAD_ENVIRONMENT:
            return tr("An environment uses lower-case letters, digits and dashes.");
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
        case SoftwareVersion::Error::COMMIT_NOT_HEX:
            return tr("A commit uses the numbers 0 to 9 and the letters a to f only.");
        case SoftwareVersion::Error::COMMIT_LENGTH:
            return tr("A commit has 7 to 12 characters.");
        }
        break;
    case Kind::INVALID_EXTRA_VERSIONS:
        switch (error.m_ExtraVersions.value_or(ExtraVersionsError::MALFORMED))
        {
        case ExtraVersionsError::EMPTY_LABEL:
            return tr("Give this version a name, like Influencer.");
        case ExtraVersionsError::LABEL_TOO_LONG:
            return tr("A name has at most 20 characters.");
        case ExtraVersionsError::LABEL_CHARACTERS:
            return tr("A name uses letters, numbers, spaces and dashes only.");
        case ExtraVersionsError::LABEL_IS_ROBOT:
            return tr("Robot is the software version above. Use another name.");
        case ExtraVersionsError::DUPLICATE_LABEL:
            return tr("Another version already has that name.");
        case ExtraVersionsError::INVALID_VERSION:
        case ExtraVersionsError::MALFORMED:
            return tr("Use three numbers separated by dots, like 1.4.2.");
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

QString LogSession::validateUnit(const QString& digits, const QString& environment) const
{
    const std::string number = to_utf8(digits.trimmed());
    const std::string env = to_utf8(environment.trimmed());
    if (const auto parsed = UnitNumber::parse(number, env); !parsed)
    {
        HeaderError error;
        error.m_Kind = HeaderError::Kind::INVALID_UNIT_NUMBER;
        error.m_UnitNumber = parsed.error();
        return header_message(error);
    }
    if (!env.empty() && m_Configuration.configured())
    {
        const auto& choices = m_Configuration.configuration().m_UnitEnvironments;
        const bool offered = std::ranges::find(choices, env) != choices.end();
        if (!offered && to_utf8(unit_environment()) != env)
        {
            return tr("Choose an environment from the list.");
        }
    }
    return {};
}

bool LogSession::correctUnit(const QString& digits, const QString& environment)
{
    if (!validateUnit(digits, environment).isEmpty())
    {
        return false;
    }
    const QString env = environment.trimmed();
    return correctHeader(QStringLiteral("unit_number"),
                         env.isEmpty() ? digits.trimmed() : digits.trimmed() + QLatin1Char('-') + env);
}

namespace
{

/// The stored text of a version from its two parts, which `validateVersion` has accepted.
QString stored_version(const QString& version, const QString& commit)
{
    const QString kept = commit.trimmed();
    return kept.isEmpty() ? version.trimmed() : version.trimmed() + QLatin1Char('+') + kept;
}

} // namespace

QString LogSession::validateCommit(const QString& commit) const
{
    if (const auto kept = SoftwareVersion::normalize_commit(to_utf8(commit.trimmed())); !kept)
    {
        HeaderError error;
        error.m_Kind = HeaderError::Kind::INVALID_SOFTWARE_VERSION;
        error.m_SoftwareVersion = kept.error();
        return header_message(error);
    }
    return {};
}

QString LogSession::normalizeCommit(const QString& commit) const
{
    const QString trimmed = commit.trimmed();
    const auto kept = SoftwareVersion::normalize_commit(to_utf8(trimmed));
    return kept ? to_qstring(*kept) : trimmed;
}

QString LogSession::validateVersion(const QString& version, const QString& commit) const
{
    // The components are judged on their own, so that a plus sign in the version field is refused.
    const QString trimmed = version.trimmed();
    if (trimmed.contains(QLatin1Char('+')))
    {
        return tr("Use digits and dots only, like 1.4.2.");
    }
    if (const QString refusal = validateHeaderValue(QStringLiteral("software_version"), trimmed); !refusal.isEmpty())
    {
        return refusal;
    }
    return validateCommit(commit);
}

QString LogSession::versionDisplay(const QString& version, const QString& commit) const
{
    if (!validateVersion(version, commit).isEmpty())
    {
        return {};
    }
    const auto parsed = SoftwareVersion::parse(to_utf8(version.trimmed()), to_utf8(commit.trimmed()));
    return parsed ? to_qstring(parsed->display()) : QString{};
}

bool LogSession::correctVersion(const QString& version, const QString& commit)
{
    if (!validateVersion(version, commit).isEmpty())
    {
        return false;
    }
    return correctHeader(QStringLiteral("software_version"), stored_version(version, commit));
}

bool LogSession::changeVersion(const QString& version, const QString& commit, int localSeconds)
{
    if (!validateVersion(version, commit).isEmpty())
    {
        return false;
    }
    return changeHeader(QStringLiteral("software_version"), stored_version(version, commit), localSeconds);
}

QString LogSession::previewVersionChange(const QString& version, const QString& commit, int localSeconds) const
{
    if (const QString refusal = validateVersion(version, commit); !refusal.isEmpty())
    {
        return refusal;
    }
    return previewHeaderChange(QStringLiteral("software_version"), stored_version(version, commit), localSeconds);
}

namespace
{

/// The rows of the other-versions editor as the model keeps them. A row with nothing in it is skipped. `first_error` is
/// the index of the row that was refused, or -1; `message` says why.
struct ExtraRows
{
    ExtraVersions m_Versions;
    std::vector<int> m_RowOfVersion;
    int m_BadRow = -1;
    bool m_BadIsVersion = false;
    std::optional<HeaderError> m_Error;
};

ExtraRows read_extra_rows(const QVariantList& rows)
{
    ExtraRows out;
    for (int i = 0; i < rows.size(); ++i)
    {
        const QVariantMap row = rows.at(i).toMap();
        const QString label = row.value(QStringLiteral("label")).toString().trimmed();
        const QString version = row.value(QStringLiteral("version")).toString().trimmed();
        const QString commit = row.value(QStringLiteral("commit")).toString().trimmed();
        if (label.isEmpty() && version.isEmpty())
        {
            continue;
        }
        const auto fail_row = [&](ExtraVersionsError error)
        {
            if (out.m_BadRow < 0)
            {
                out.m_BadRow = i;
                out.m_BadIsVersion = error == ExtraVersionsError::INVALID_VERSION;
                HeaderError e;
                e.m_Kind = HeaderError::Kind::INVALID_EXTRA_VERSIONS;
                e.m_ExtraVersions = error;
                out.m_Error = e;
            }
        };
        const auto kept = normalize_label(to_utf8(label));
        if (!kept)
        {
            fail_row(kept.error());
            continue;
        }
        const auto parsed = SoftwareVersion::parse(to_utf8(version), to_utf8(commit));
        if (version.contains(QLatin1Char('+')) || !parsed)
        {
            fail_row(ExtraVersionsError::INVALID_VERSION);
            continue;
        }
        const bool twice = std::ranges::any_of(out.m_Versions, [&](const LabelledVersion& other)
                                               { return QString::fromStdString(std::string(other.label())).compare(to_qstring(*kept), Qt::CaseInsensitive) == 0; });
        if (twice)
        {
            fail_row(ExtraVersionsError::DUPLICATE_LABEL);
            continue;
        }
        out.m_Versions.push_back(LabelledVersion{*kept, *parsed});
        out.m_RowOfVersion.push_back(i);
    }
    return out;
}

} // namespace

QString LogSession::validateExtraRow(const QVariantList& rows, int index) const
{
    const ExtraRows read = read_extra_rows(rows);
    if (read.m_Error && read.m_BadRow == index)
    {
        return header_message(*read.m_Error);
    }
    return {};
}

QString LogSession::extraRowField(const QVariantList& rows, int index) const
{
    const ExtraRows read = read_extra_rows(rows);
    if (!read.m_Error || read.m_BadRow != index)
    {
        return {};
    }
    return read.m_BadIsVersion ? QStringLiteral("version") : QStringLiteral("label");
}

bool LogSession::correctExtraVersions(const QVariantList& rows)
{
    const ExtraRows read = read_extra_rows(rows);
    if (read.m_Error)
    {
        return false;
    }
    return correctHeader(QStringLiteral("extra_versions"), to_qstring(stored_extra_versions(read.m_Versions)));
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
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment).build();
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

    // The new log's header: the tester and today's date, nothing else (HDR-05).
    HeaderValues header;
    header.m_Tester = tester_for_new_log();
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
