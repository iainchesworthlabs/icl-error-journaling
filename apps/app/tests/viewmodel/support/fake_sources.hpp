// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "support/event_log.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/feedback_sink.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/observer_list.hpp"
#include "viewmodel/open_log_source.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Fakes of the interfaces a service reads its neighbours through, so that a service is tested without the class that
/// implements the interface in the product (docs/plan/stage-3.md, "Work order": each package reads its neighbours
/// through the interfaces and fakes of 3.1, not through each other's classes).
namespace ej::test
{

/// "ENTRY_ADDED <id>", "REPLACED", with " (undo)" for an undo.
[[nodiscard]] std::string describe_log_change(const viewmodel::LogChange& change);

/// A `FeedbackSink` that writes what it is told into the `EventLog`: `touch` as a `HAPTIC` "impact" (what the product
/// sink fires), the confirmations as `CONFIRMATION` "entry_recorded <id>" and "note_saved <id>".
class FakeFeedbackSink final : public viewmodel::FeedbackSink
{
public:
    explicit FakeFeedbackSink(EventLog* log = nullptr) : m_Log(log) {}

    void prepare_haptics() override
    {
        ++m_Prepared;
    }
    void acknowledge_touch() override;
    void entry_recorded(EntryId entry) override;
    void note_saved(EntryId entry) override;

    [[nodiscard]] int prepared() const
    {
        return m_Prepared;
    }
    [[nodiscard]] std::size_t touches() const
    {
        return m_Touches;
    }
    [[nodiscard]] const std::vector<EntryId>& recorded() const
    {
        return m_Recorded;
    }
    [[nodiscard]] const std::vector<EntryId>& saved_notes() const
    {
        return m_Saved;
    }

private:
    EventLog* m_Log;
    int m_Prepared = 0;
    std::size_t m_Touches = 0;
    std::vector<EntryId> m_Recorded;
    std::vector<EntryId> m_Saved;
};

/// Writes each `LogChange` it is told about into the `EventLog` (`LOG_CHANGE`) and keeps them.
///
/// `LogDocument` is work package 3.2's, so the package-3.1 tests cannot make a document to hand to `on_log_changed`;
/// they call `note`, which is what `on_log_changed` does. From 3.2 on a test registers it with a real document.
class FakeLogObserver final : public viewmodel::LogObserver
{
public:
    explicit FakeLogObserver(EventLog* log = nullptr) : m_Log(log) {}

    void on_log_changed(const viewmodel::LogDocument& document, const viewmodel::LogChange& change) override
    {
        note(&document, change);
    }

    void note(const viewmodel::LogDocument* document, const viewmodel::LogChange& change);

    [[nodiscard]] const std::vector<viewmodel::LogChange>& changes() const
    {
        return m_Changes;
    }
    [[nodiscard]] const viewmodel::LogDocument* last_document() const
    {
        return m_LastDocument;
    }

private:
    EventLog* m_Log;
    std::vector<viewmodel::LogChange> m_Changes;
    const viewmodel::LogDocument* m_LastDocument = nullptr;
};

/// Counts the configuration changes it is told about (`CONFIGURATION` in the `EventLog`).
class FakeConfigurationObserver final : public viewmodel::ConfigurationObserver
{
public:
    explicit FakeConfigurationObserver(EventLog* log = nullptr) : m_Log(log) {}

    void on_configuration_changed(const viewmodel::ConfigurationSource& source) override;

    [[nodiscard]] std::size_t changes() const
    {
        return m_Changes;
    }
    [[nodiscard]] const viewmodel::ConfigurationSource* last_source() const
    {
        return m_LastSource;
    }

private:
    EventLog* m_Log;
    std::size_t m_Changes = 0;
    const viewmodel::ConfigurationSource* m_LastSource = nullptr;
};

/// Keeps what it is told, in order: the open document of each change (null for none).
class FakeOpenLogObserver final : public viewmodel::OpenLogObserver
{
public:
    void on_open_log_changed(viewmodel::LogDocument* document) override
    {
        m_Documents.push_back(document);
    }

    [[nodiscard]] const std::vector<viewmodel::LogDocument*>& documents() const
    {
        return m_Documents;
    }

private:
    std::vector<viewmodel::LogDocument*> m_Documents;
};

/// A configuration a test sets. It starts unconfigured and empty.
class FakeConfigurationSource final : public viewmodel::ConfigurationSource
{
public:
    [[nodiscard]] bool configured() const override
    {
        return m_Configured;
    }
    [[nodiscard]] const Configuration& configuration() const override
    {
        return m_Configuration;
    }
    void add_observer(viewmodel::ConfigurationObserver& observer) override
    {
        m_Observers.add(observer);
    }
    void remove_observer(viewmodel::ConfigurationObserver& observer) override
    {
        m_Observers.remove(observer);
    }

    /// Puts `configuration` in force and tells the observers, once.
    void set(Configuration configuration);

    /// Takes the configuration away (a first launch) and tells the observers, once.
    void clear();

    [[nodiscard]] std::size_t observer_count() const
    {
        return m_Observers.size();
    }

private:
    void notify();

    bool m_Configured = false;
    Configuration m_Configuration;
    viewmodel::ObserverList<viewmodel::ConfigurationObserver> m_Observers;
};

/// A `ConfigurationEditor` that remembers what it was asked and, when it has a `FakeConfigurationSource`, adds the
/// types to it when the test calls `commit`, standing for the save that finishes later on the storage thread.
class FakeConfigurationEditor final : public viewmodel::ConfigurationEditor
{
public:
    /// `source` may be null. Neither argument is owned.
    explicit FakeConfigurationEditor(viewmodel::IdGenerator& ids, FakeConfigurationSource* source = nullptr)
        : m_Ids(ids), m_Source(source)
    {
    }

    [[nodiscard]] Result<ErrorTypeId, viewmodel::ConfigEditError> add_error_type(std::string name) override;

    /// The next call is refused with this error.
    void refuse_next(viewmodel::ConfigEditError error)
    {
        m_Refusal = std::move(error);
    }

    /// Applies the types that were added since the last commit to the source, with one notification.
    void commit();

    [[nodiscard]] const std::vector<ErrorType>& added() const
    {
        return m_Added;
    }
    [[nodiscard]] std::size_t pending() const
    {
        return m_Added.size() - m_Committed;
    }

private:
    viewmodel::IdGenerator& m_Ids;
    FakeConfigurationSource* m_Source;
    std::optional<viewmodel::ConfigEditError> m_Refusal;
    std::vector<ErrorType> m_Added;
    std::size_t m_Committed = 0;
};

/// An `OpenLogSource` a test sets. Until 3.2 there is no document to give it but null.
class FakeOpenLogSource final : public viewmodel::OpenLogSource
{
public:
    [[nodiscard]] viewmodel::LogDocument* open_log() const override
    {
        return m_Document;
    }
    void add_observer(viewmodel::OpenLogObserver& observer) override
    {
        m_Observers.add(observer);
    }
    void remove_observer(viewmodel::OpenLogObserver& observer) override
    {
        m_Observers.remove(observer);
    }

    /// Makes `document` the open log and tells the observers, once.
    void set(viewmodel::LogDocument* document);

    [[nodiscard]] std::size_t observer_count() const
    {
        return m_Observers.size();
    }

private:
    viewmodel::LogDocument* m_Document = nullptr;
    viewmodel::ObserverList<viewmodel::OpenLogObserver> m_Observers;
};

} // namespace ej::test
