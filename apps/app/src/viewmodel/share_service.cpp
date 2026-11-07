// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/share_service.hpp"

#include "viewmodel/export_context_builder.hpp"

#include <QPointer>
#include <algorithm>
#include <string>
#include <utility>

namespace ej::viewmodel
{

namespace bridge = error_journaling::bridge;

namespace
{

QString outcome_name(bridge::share_outcome outcome)
{
    switch (outcome)
    {
    case bridge::share_outcome::shared:
        return QStringLiteral("shared");
    case bridge::share_outcome::cancelled:
        return QStringLiteral("cancelled");
    case bridge::share_outcome::unavailable:
        return QStringLiteral("unavailable");
    case bridge::share_outcome::failed:
        break;
    }
    return QStringLiteral("failed");
}

QString id_text(LogId id)
{
    return QString::fromStdString(id.format());
}

} // namespace

ShareService::ShareService(const Environment& environment, OpenLogSource& open_log, DocumentRegistry& registry,
                           const ConfigurationSource& configuration, bridge::platform_services& platform,
                           bool available, QObject* parent)
    : QObject(parent), m_Environment(environment), m_OpenLog(open_log), m_Registry(registry),
      m_Configuration(configuration), m_Platform(platform), m_Available(available)
{
}

ShareService::~ShareService() = default;

bool ShareService::shareOpenLog()
{
    if (m_Sharing)
    {
        return false;
    }
    const LogDocument* open = m_OpenLog.open_log();
    if (open == nullptr)
    {
        emit shareFinished(QString(), QStringLiteral("failed"));
        return false;
    }
    return begin(open->id());
}

bool ShareService::shareLog(const QString& logId)
{
    if (m_Sharing)
    {
        return false;
    }
    const auto parsed = Uuid::parse(logId.toStdString());
    if (!parsed)
    {
        emit shareFinished(logId, QStringLiteral("failed"));
        return false;
    }
    return begin(LogId(*parsed));
}

bool ShareService::begin(LogId id)
{
    m_Sharing = true;
    emit sharingChanged();
    // A live document (the open log, or a past log someone holds) is delivered before acquire returns; otherwise the
    // log is loaded through the gateway. Either way `sharing` is true meanwhile.
    m_Registry.acquire(id,
                       [self = QPointer<ShareService>(this), id](Loaded loaded)
                       {
                           if (self)
                           {
                               self->acquired(id, std::move(loaded));
                           }
                       });
    return true;
}

void ShareService::acquired(LogId id, Loaded loaded)
{
    if (!loaded || !(*loaded)->ready())
    {
        end(id, QStringLiteral("failed")); // an unknown log: no sheet
        return;
    }
    std::shared_ptr<LogDocument> document = std::move(*loaded);

    // Render on the GUI thread from the mirror: what the screen shows, which is what has committed.
    const ExportContext context = ExportContextBuilder(m_Configuration, m_Environment.m_Zones).build();
    const Result<std::string, RenderError> text = export_text(document->log(), context);
    if (!text)
    {
        end(id, QStringLiteral("failed")); // no sheet, and no partial text
        return;
    }

    m_Shown = std::move(document);
    // The completion may run before this call returns (the desktop stub) or later (a phone): the state is set above.
    m_Platform.present_share_sheet(*text,
                                   [self = QPointer<ShareService>(this), id](const bridge::share_result& result)
                                   {
                                       if (self)
                                       {
                                           self->sheet_ended(id, result);
                                       }
                                   });
}

void ShareService::sheet_ended(LogId id, const bridge::share_result& result)
{
    // The time the sheet completed, read before anything else (a load for a past log can take a while).
    const UtcMillis when = m_Environment.m_Clock.now();
    std::shared_ptr<LogDocument> document = std::move(m_Shown);
    m_Shown.reset();

    if (result.outcome == bridge::share_outcome::shared && document)
    {
        if (document->id() == id)
        {
            record(document, id, when);
        }
        else
        {
            // A new day took the place of the open log while the sheet was up: the document now holds another log.
            // The log that was shared is archived by now and is loaded for the write.
            m_Registry.acquire(id,
                               [self = QPointer<ShareService>(this), id, when](Loaded loaded)
                               {
                                   if (!self)
                                   {
                                       return;
                                   }
                                   if (loaded)
                                   {
                                       self->record(*loaded, id, when);
                                   }
                                   else
                                   {
                                       self->recording_failed();
                                   }
                               });
        }
    }
    end(id, outcome_name(result.outcome));
}

void ShareService::record(const std::shared_ptr<LogDocument>& document, LogId id, UtcMillis when)
{
    m_Recording.push_back(document);
    const auto accepted = document->mark_shared(
        when,
        [self = QPointer<ShareService>(this), raw = document.get(), id](const Result<void, StorageError>& result)
        {
            if (self)
            {
                self->recorded(raw, id, result);
            }
        });
    if (!accepted)
    {
        forget(document.get());
        recording_failed();
    }
}

void ShareService::recorded(const LogDocument* document, LogId id, const Result<void, StorageError>& result)
{
    forget(document);
    if (result)
    {
        emit shareRecorded(id_text(id));
    }
    else
    {
        recording_failed();
    }
}

void ShareService::forget(const LogDocument* document)
{
    const auto it = std::ranges::find_if(m_Recording, [document](const auto& held) { return held.get() == document; });
    if (it != m_Recording.end())
    {
        m_Recording.erase(it);
    }
}

void ShareService::recording_failed()
{
    emit writeFailed(QStringLiteral("markShared"),
                     tr("The log was shared, but the share could not be saved. Share again to save it."));
}

void ShareService::end(LogId id, const QString& outcome)
{
    m_Shown.reset();
    m_Sharing = false;
    emit sharingChanged();
    emit shareFinished(id_text(id), outcome);
}

} // namespace ej::viewmodel
