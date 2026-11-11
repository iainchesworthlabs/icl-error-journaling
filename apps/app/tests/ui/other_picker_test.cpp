// SPDX-License-Identifier: GPL-3.0-or-later
// OtherPicker (docs/plan/stage-4.md, 4.8): other.sections, other.filter, other.pick, other.add, other.voiceover and the
// cross-cutting checks, against the real ErrorPickerModel and LogSession over the in-memory store and the manual
// gateway. The bridge is a spy that notes what the session had accepted when the haptic fired, and the configuration
// editor is the fake of the Stage 3 tests, which adds a type when the test commits.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort board_test_support.hpp first, so it is
// switched off.
// clang-format off
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/error_picker_model.hpp"
#include "viewmodel/log_session.hpp"

#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QAccessible>
#include <QColor>
#include <QMetaObject>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;
using viewmodel::ErrorPickerModel;
using viewmodel::LogSession;

namespace
{

const QString OFF_GROUP = QStringLiteral("Not on your board");
const QString ON_GROUP = QStringLiteral("On your board");
const QString ECHO = QString::fromUtf8("\xC3\x89"
                                       "cho Delay");

/// The sample configuration with Navigation Failure taken off the board, "Écho Delay" added to the master list without
/// a place on it, and a retired type that no sheet lists.
Configuration picker_configuration()
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[10].m_BoardPosition.reset();

    ErrorType echo;
    echo.m_Id = sample_id<ErrorTypeId>(600);
    echo.m_Name = "\xC3\x89"
                  "cho Delay";
    configuration.m_ErrorTypes.push_back(std::move(echo));

    ErrorType retired;
    retired.m_Id = sample_id<ErrorTypeId>(601);
    retired.m_Name = "Retired Type";
    retired.m_Retired = true;
    configuration.m_ErrorTypes.push_back(std::move(retired));
    return configuration;
}

/// Stands for the `bridge`: counts the impact haptics and notes what the session had accepted when each fired, so a
/// test can tell the haptic from the entry in order.
class SpyBridge : public QObject
{
    Q_OBJECT

public:
    explicit SpyBridge(viewmodel::LogSession& session) : m_Session(session) {}

    Q_INVOKABLE void hapticImpact(double /*pressTimestamp*/ = 0)
    {
        ++m_Impacts;
        m_PendingAtImpact = m_Session.pending_writes();
    }

    int m_Impacts = 0;
    int m_PendingAtImpact = -1;

private:
    viewmodel::LogSession& m_Session;
};

/// An open log with the sample configuration, the real session and picker over it, and the picker sheet in the UI rig.
/// The rig is declared last so that it is destroyed first: the QML holds pointers to the models.
struct Fixture
{
    Fixture()
        : m_Env(m_World.environment()), m_Storage(m_World.m_Clock, &m_Events), m_Gateway(m_Storage.context()),
          m_Registry(m_Env, m_Gateway), m_Feedback(&m_Events), m_Editor(m_World.m_Ids, &m_Config)
    {
        m_Config.set(picker_configuration());
        m_Session = std::make_unique<LogSession>(m_Env, m_Gateway, m_Registry, m_Config, m_Feedback,
                                                 viewmodel::StartupResult{});
        m_Session->load();
        m_Gateway.run_all();
        REQUIRE(m_Session->open_log() != nullptr);
        m_Picker = std::make_unique<ErrorPickerModel>(m_Config, *m_Session, m_Editor);
        m_Bridge = std::make_unique<SpyBridge>(*m_Session);
        m_Rig.attach(m_World.m_Clock);
        m_Rig.attach(m_Gateway);

        m_Sheet = m_Rig.load(QStringLiteral("OtherPicker"),
                             {{QStringLiteral("picker"), QVariant::fromValue(static_cast<QObject*>(m_Picker.get()))},
                              {QStringLiteral("session"), QVariant::fromValue(static_cast<QObject*>(m_Session.get()))},
                              {QStringLiteral("bridge"), QVariant::fromValue(static_cast<QObject*>(m_Bridge.get()))},
                              {QStringLiteral("overrideInsets"), true},
                              {QStringLiteral("overrideTop"), 59},
                              {QStringLiteral("overrideBottom"), 34}});
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_Sheet != nullptr);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void open(std::chrono::milliseconds wait = 400ms)
    {
        QMetaObject::invokeMethod(m_Sheet, "open");
        m_Rig.advance(wait);
    }

    void play(std::chrono::milliseconds wait = 400ms)
    {
        m_Rig.advance(wait);
    }

    [[nodiscard]] bool is_open() const
    {
        return m_Sheet->property("isOpen").toBool();
    }

    /// Runs the manual gateway and lets the queued signals through.
    void commit()
    {
        m_Rig.settle();
        m_Rig.layout();
    }

    /// Records `label` through the session, as a tile does, and lets the commit through.
    void record(const QString& label, int times = 1)
    {
        for (int i = 0; i < times; ++i)
        {
            REQUIRE(m_Session->recordError(type_id(label)));
            commit();
        }
    }

    /// The id of the type named `label` in the master list.
    [[nodiscard]] QString type_id(const QString& label) const
    {
        for (const ErrorType& type : m_Config.configuration().m_ErrorTypes)
        {
            if (QString::fromStdString(type.m_Name) == label)
            {
                return QString::fromStdString(type.m_Id.format());
            }
        }
        FAIL("no such type: " << label.toStdString());
        return {};
    }

    [[nodiscard]] QQuickItem* find(const QString& name)
    {
        return require_item(m_Rig, name);
    }

    /// The model's count for `label` today.
    [[nodiscard]] int model_count(const QString& label) const
    {
        for (int row = 0; row < m_Picker->rowCount(); ++row)
        {
            const QModelIndex index = m_Picker->index(row);
            if (m_Picker->data(index, ErrorPickerModel::LABEL).toString() == label)
            {
                return m_Picker->data(index, ErrorPickerModel::COUNT).toInt();
            }
        }
        return -1;
    }

    /// Scrolls the list to its end, where the add row is.
    void scroll_to_add()
    {
        QQuickItem* list = find(QStringLiteral("pickerList"));
        REQUIRE(list != nullptr);
        // The add row and its form are at the top of the list (F10).
        list->setProperty("contentY", 0.0);
        m_Rig.layout();
    }

    TestEnvironment m_World;
    viewmodel::Environment m_Env;
    EventLog m_Events;
    MemoryStorage m_Storage;
    ManualStorageGateway m_Gateway;
    viewmodel::DocumentRegistry m_Registry;
    FakeConfigurationSource m_Config;
    FakeFeedbackSink m_Feedback;
    FakeConfigurationEditor m_Editor;
    std::unique_ptr<LogSession> m_Session;
    std::unique_ptr<ErrorPickerModel> m_Picker;
    std::unique_ptr<SpyBridge> m_Bridge;
    UiRig m_Rig;
    QQuickItem* m_Sheet = nullptr;
};

/// A row of the sheet as it is shown.
struct Row
{
    QQuickItem* m_Item = nullptr;
    QString m_Text;
    QString m_Value;
    QString m_Group;
};

/// The rows that are shown, top to bottom, with the title of the group each is in.
std::vector<Row> rows(Fixture& f)
{
    std::vector<Row> out;
    for (const Node& node : f.m_Rig.tree())
    {
        if (!node.m_ObjectName.startsWith(QStringLiteral("pickerRow:")) || !node.m_Visible)
        {
            continue;
        }
        Row row;
        row.m_Item = node.m_Item;
        row.m_Text = node.m_Item->property("text").toString();
        row.m_Value = node.m_Item->property("value").toString();
        for (const QQuickItem* up = node.m_Item->parentItem(); up != nullptr; up = up->parentItem())
        {
            if (up->property("title").isValid())
            {
                row.m_Group = up->property("title").toString();
                break;
            }
        }
        out.push_back(std::move(row));
    }
    return out;
}

QStringList labels_in(const std::vector<Row>& all, const QString& group)
{
    QStringList out;
    for (const Row& row : all)
    {
        if (row.m_Group == group)
        {
            out.push_back(row.m_Text);
        }
    }
    return out;
}

const Row* row_for(const std::vector<Row>& all, const QString& label)
{
    for (const Row& row : all)
    {
        if (row.m_Text == label)
        {
            return &row;
        }
    }
    return nullptr;
}

/// The text of the value of the row for `label`; fails the test when there is no such row.
QString value_of(const std::vector<Row>& all, const QString& label)
{
    const Row* row = row_for(all, label);
    REQUIRE(row != nullptr);
    return row->m_Value;
}

/// The colour of the swatch in `row`.
QColor swatch_colour(const Row& row)
{
    for (QQuickItem* child : row.m_Item->childItems())
    {
        if (child->objectName() == QStringLiteral("pickerSwatch"))
        {
            return child->property("color").value<QColor>();
        }
    }
    return {};
}

QColor tile_colour(UiRig& rig, int slot)
{
    QColor result;
    const bool called =
        QMetaObject::invokeMethod(rig.theme(), "tileColour", Q_RETURN_ARG(QColor, result), Q_ARG(int, slot));
    REQUIRE(called);
    return result;
}

QColor theme_colour(UiRig& rig, const char* name)
{
    const QObject* theme = rig.theme();
    REQUIRE(theme != nullptr);
    const QObject* colours = theme->property("colour").value<QObject*>();
    return colours != nullptr ? colours->property(name).value<QColor>() : QColor();
}

} // namespace

TEST_CASE("other.sections: the types by section, the counts, the swatches and the headings", "[ui][other]")
{
    Fixture f;
    f.record(QStringLiteral("Wrong Response"), 2);
    f.record(QStringLiteral("Mute Delay"));
    f.open();

    const std::vector<Row> all = rows(f);
    REQUIRE(all.size() == 12);

    // Not on your board first, then on your board, each in master-list order; the retired type is not listed.
    CHECK(labels_in(all, OFF_GROUP) == QStringList{QStringLiteral("Navigation Failure"), ECHO});
    const QStringList on = labels_in(all, ON_GROUP);
    REQUIRE(on.size() == 10);
    CHECK(on.first() == QStringLiteral("Wrong Response"));
    CHECK(on.last() == QStringLiteral("Track Delay"));
    CHECK_FALSE(row_for(all, QStringLiteral("Retired Type")) != nullptr);
    CHECK(all.front().m_Group == OFF_GROUP);
    CHECK(all.back().m_Group == ON_GROUP);
    CHECK(f.m_Rig.geometry(all.front().m_Item).top() < f.m_Rig.geometry(all.back().m_Item).top());

    // A count shows as "n today" and a type with none shows nothing.
    CHECK(value_of(all, QStringLiteral("Wrong Response")) == QStringLiteral("2 today"));
    CHECK(value_of(all, QStringLiteral("Mute Delay")) == QStringLiteral("1 today"));
    CHECK(value_of(all, QStringLiteral("Track Delay")).isEmpty());
    CHECK(value_of(all, QStringLiteral("Navigation Failure")).isEmpty());

    // The swatch is the colour of the tile on the board and neutral off it.
    CHECK(swatch_colour(*row_for(all, QStringLiteral("Navigation Failure"))) == theme_colour(f.m_Rig, "swatch"));
    CHECK(swatch_colour(*row_for(all, ECHO)) == theme_colour(f.m_Rig, "swatch"));
    const Row* wrong_response = row_for(all, QStringLiteral("Wrong Response"));
    REQUIRE(wrong_response != nullptr);
    const Row* sequence = row_for(all, QStringLiteral("Sequence Out of Order"));
    REQUIRE(sequence != nullptr);
    CHECK(swatch_colour(*wrong_response) == tile_colour(f.m_Rig, 1));
    CHECK(swatch_colour(*sequence) == tile_colour(f.m_Rig, 2));
    CHECK(swatch_colour(*wrong_response) != swatch_colour(*sequence));

    // Rows are 52 pt ("not on your board") or 46 pt ("on your board") and the width of the group, less the column of
    // the mark.
    for (const Row& row : all)
    {
        CHECK(f.m_Rig.geometry(row.m_Item).height() >= 46);
        CHECK(f.m_Rig.geometry(row.m_Item).width() == 402 - 2 * 16 - 26);
    }

    // The counts follow a commit while the sheet is open.
    f.record(QStringLiteral("Navigation Failure"));
    CHECK(value_of(rows(f), QStringLiteral("Navigation Failure")) == QStringLiteral("1 today"));
    f.record(QStringLiteral("Mute Delay"));
    CHECK(value_of(rows(f), QStringLiteral("Mute Delay")) == QStringLiteral("2 today"));
}

TEST_CASE("other.sections: a board with nothing off it shows no empty section", "[ui][other]")
{
    Fixture f;
    f.m_Config.set(sample_configuration());
    f.open();

    const std::vector<Row> all = rows(f);
    CHECK(all.size() == 11);
    CHECK(labels_in(all, OFF_GROUP).isEmpty());
    CHECK(labels_in(all, ON_GROUP).size() == 11);
}

TEST_CASE("other.filter: typing narrows both sections with one reset for each change, and the field keeps focus",
          "[ui][other]")
{
    Fixture f;
    f.open();
    QQuickItem* input = f.find(QStringLiteral("searchInput"));
    REQUIRE(input != nullptr);
    QSignalSpy resets(f.m_Picker.get(), &QAbstractItemModel::modelReset);

    input->forceActiveFocus();
    f.m_Rig.type(QStringLiteral("MUTE"));
    CHECK(resets.count() == 4);
    CHECK(f.m_Picker->filter() == QStringLiteral("MUTE"));
    CHECK(input->property("activeFocus").toBool());
    CHECK(input->property("text").toString() == QStringLiteral("MUTE"));

    // Case does not matter; the one type that matches is on the board, so the other section is not shown at all.
    std::vector<Row> all = rows(f);
    REQUIRE(all.size() == 1);
    CHECK(all.front().m_Text == QStringLiteral("Mute Delay"));
    CHECK(all.front().m_Group == ON_GROUP);

    // A filter that matches only a type off the board.
    for (int i = 0; i < 4; ++i)
    {
        f.m_Rig.key(Qt::Key_Backspace);
    }
    f.m_Rig.type(QStringLiteral("navig"));
    all = rows(f);
    REQUIRE(all.size() == 1);
    CHECK(all.front().m_Text == QStringLiteral("Navigation Failure"));
    CHECK(all.front().m_Group == OFF_GROUP);
    CHECK(input->property("activeFocus").toBool());

    // Both sections at once.
    for (int i = 0; i < 5; ++i)
    {
        f.m_Rig.key(Qt::Key_Backspace);
    }
    f.m_Rig.type(QStringLiteral("delay"));
    all = rows(f);
    CHECK(labels_in(all, OFF_GROUP) == QStringList{ECHO});
    CHECK(labels_in(all, ON_GROUP).size() == 3); // Mute Delay, Speech Delay, Track Delay
}

TEST_CASE("other.filter: no match shows the message, and the clear button brings every type back", "[ui][other]")
{
    Fixture f;
    f.open();
    QQuickItem* input = f.find(QStringLiteral("searchInput"));
    REQUIRE(input != nullptr);
    const QQuickItem* empty = f.find(QStringLiteral("pickerEmpty"));
    REQUIRE(empty != nullptr);
    CHECK_FALSE(empty->isVisible());

    input->forceActiveFocus();
    f.m_Rig.type(QStringLiteral("zzz"));
    CHECK(rows(f).empty());
    QQuickItem* message = f.find(QStringLiteral("pickerEmpty"));
    REQUIRE(message != nullptr);
    CHECK(message->isVisible());
    CHECK(message->property("text").toString().contains(QStringLiteral("zzz")));
    CHECK(message->property("text").toString().contains(QStringLiteral("No error types match")));

    f.m_Rig.tap(f.find(QStringLiteral("clearButton")));
    CHECK(f.m_Picker->filter().isEmpty());
    CHECK(input->property("text").toString().isEmpty());
    CHECK(rows(f).size() == 12);
    CHECK_FALSE(message->isVisible());
}

TEST_CASE("other.pick: a tap fires the impact, records once with that id, and closes the sheet", "[ui][other]")
{
    Fixture f;
    f.record(QStringLiteral("Wrong Response"));
    f.open();
    const int entries_before = f.m_Session->live_entry_count();
    QSignalSpy closed(f.m_Sheet, SIGNAL(closed()));
    REQUIRE(f.is_open());

    const std::vector<Row> all = rows(f);
    const Row* mute = row_for(all, QStringLiteral("Mute Delay"));
    REQUIRE(mute != nullptr);
    f.m_Rig.tap(mute->m_Item);

    // The haptic came first: when it fired, the session had accepted nothing yet. Then the entry was accepted.
    CHECK(f.m_Bridge->m_Impacts == 1);
    CHECK(f.m_Bridge->m_PendingAtImpact == 0);
    CHECK(f.m_Session->pending_writes() == 1);

    // The count changes only after the commit.
    CHECK(f.m_Session->live_entry_count() == entries_before);
    CHECK(f.model_count(QStringLiteral("Mute Delay")) == 0);
    f.commit();
    CHECK(f.m_Session->live_entry_count() == entries_before + 1);
    CHECK(f.model_count(QStringLiteral("Mute Delay")) == 1);
    CHECK(f.model_count(QStringLiteral("Wrong Response")) == 1);

    // The sheet closes and its owner is told once.
    f.play(500ms);
    CHECK_FALSE(f.is_open());
    CHECK(closed.count() == 1);
    CHECK(f.m_Bridge->m_Impacts == 1);
}

TEST_CASE("other.pick: a type that is not on the board is recorded like any other", "[ui][other]")
{
    Fixture f;
    f.open();
    const std::vector<Row> listed = rows(f);
    const Row* navigation = row_for(listed, QStringLiteral("Navigation Failure"));
    REQUIRE(navigation != nullptr);
    f.m_Rig.tap(navigation->m_Item);
    f.commit();

    CHECK(f.m_Bridge->m_Impacts == 1);
    CHECK(f.model_count(QStringLiteral("Navigation Failure")) == 1);
    CHECK(f.model_count(QStringLiteral("Wrong Response")) == 0);
}

TEST_CASE("other.pick: Cancel closes the sheet and records nothing, and the filter is cleared for next time",
          "[ui][other]")
{
    Fixture f;
    f.open();
    QQuickItem* input = f.find(QStringLiteral("searchInput"));
    input->forceActiveFocus();
    f.m_Rig.type(QStringLiteral("mute"));
    REQUIRE(f.m_Picker->filter() == QStringLiteral("mute"));

    f.m_Rig.tap(f.find(QStringLiteral("navLeading")));
    f.play(500ms);
    f.commit();

    CHECK_FALSE(f.is_open());
    CHECK(f.m_Bridge->m_Impacts == 0);
    CHECK(f.m_Session->live_entry_count() == 0);
    CHECK(f.m_Session->pending_writes() == 0);
    CHECK(f.m_Picker->filter().isEmpty());
    for (int row = 0; row < f.m_Picker->rowCount(); ++row)
    {
        CHECK(f.m_Picker->data(f.m_Picker->index(row), ErrorPickerModel::COUNT).toInt() == 0);
    }

    // It opens again with every type listed.
    f.open();
    CHECK(rows(f).size() == 12);
}

TEST_CASE("other.add: a valid name is added, nothing is recorded, and the type is listed once the save has committed",
          "[ui][other]")
{
    Fixture f;
    f.open();
    QQuickItem* input = f.find(QStringLiteral("searchInput"));
    input->forceActiveFocus();
    f.m_Rig.type(QStringLiteral("mute"));
    f.scroll_to_add();

    QQuickItem* name = f.find(QStringLiteral("pickerNewName"));
    REQUIRE(name != nullptr);
    CHECK_FALSE(name->isVisible());
    f.m_Rig.tap(f.find(QStringLiteral("pickerAdd")));
    f.m_Rig.layout();
    CHECK(name->isVisible());
    const QQuickItem* field = f.find(QStringLiteral("formInput"));
    REQUIRE(field != nullptr);
    CHECK(field->property("activeFocus").toBool());

    f.m_Rig.type(QStringLiteral("Wrong Greeting"));
    f.m_Rig.tap(f.find(QStringLiteral("pickerAddButton")));

    // The model took the name, no entry was recorded and no haptic fired; the sheet is still open.
    REQUIRE(f.m_Editor.added().size() == 1);
    CHECK(f.m_Editor.added().front().m_Name == "Wrong Greeting");
    CHECK(f.m_Bridge->m_Impacts == 0);
    CHECK(f.m_Session->pending_writes() == 0);
    CHECK(f.m_Session->live_entry_count() == 0);
    CHECK(f.is_open());
    CHECK_FALSE(name->isVisible());
    // The filter that would hide the new type is cleared.
    CHECK(f.m_Picker->filter().isEmpty());
    CHECK(input->property("text").toString().isEmpty());

    // Until the save commits the type is not in the list.
    CHECK(row_for(rows(f), QStringLiteral("Wrong Greeting")) == nullptr);
    f.m_Editor.commit();
    f.commit();
    const std::vector<Row> all = rows(f);
    const Row* added = row_for(all, QStringLiteral("Wrong Greeting"));
    REQUIRE(added != nullptr);
    CHECK(added->m_Group == OFF_GROUP);
    CHECK(added->m_Value.isEmpty());
    CHECK(f.m_Session->live_entry_count() == 0);

    // The user taps it like any other.
    f.m_Rig.tap(added->m_Item);
    f.commit();
    CHECK(f.m_Bridge->m_Impacts == 1);
    CHECK(f.model_count(QStringLiteral("Wrong Greeting")) == 1);
}

TEST_CASE("other.add: a refused name stays in the field with the model's message, until it is edited", "[ui][other]")
{
    Fixture f;
    f.open();
    f.scroll_to_add();
    f.m_Rig.tap(f.find(QStringLiteral("pickerAdd")));
    f.m_Rig.layout();
    QQuickItem* name = f.find(QStringLiteral("pickerNewName"));
    REQUIRE(name != nullptr);
    CHECK(name->property("errorText").toString().isEmpty());

    SECTION("an empty name")
    {
        f.m_Rig.tap(f.find(QStringLiteral("pickerAddButton")));
        CHECK(name->property("errorText").toString() == QStringLiteral("The name is empty."));
        CHECK(name->property("errorText").toString() == f.m_Picker->add_type_error());
        CHECK(name->isVisible());
        CHECK(f.m_Editor.added().empty());
    }

    SECTION("a duplicate name, added with the Return key")
    {
        f.m_Editor.refuse_next(viewmodel::ConfigEditError{viewmodel::ConfigEditError::Code::REFUSED,
                                                          "There is already an error type with that name."});
        QQuickItem* field = f.find(QStringLiteral("formInput"));
        REQUIRE(field != nullptr);
        field->forceActiveFocus();
        f.m_Rig.type(QStringLiteral("wrong response"));
        f.m_Rig.key(Qt::Key_Return);
        CHECK(name->property("errorText").toString() ==
              QStringLiteral("There is already an error type with that name."));
        CHECK(name->property("text").toString() == QStringLiteral("wrong response"));
        CHECK(f.m_Editor.added().empty());

        // Editing the name takes the message away; a name that is accepted closes the form.
        f.m_Rig.type(QStringLiteral("x"));
        CHECK(name->property("errorText").toString().isEmpty());
        f.m_Rig.tap(f.find(QStringLiteral("pickerAddButton")));
        CHECK(f.m_Editor.added().size() == 1);
        CHECK_FALSE(name->isVisible());
    }

    SECTION("an over-long name")
    {
        f.m_Editor.refuse_next(viewmodel::ConfigEditError{viewmodel::ConfigEditError::Code::REFUSED,
                                                          "An error type name can be 200 characters at most."});
        QQuickItem* field = f.find(QStringLiteral("formInput"));
        REQUIRE(field != nullptr);
        field->forceActiveFocus();
        f.m_Rig.type(QStringLiteral("A very long name"));
        f.m_Rig.tap(f.find(QStringLiteral("pickerAddButton")));
        CHECK(name->property("errorText").toString() ==
              QStringLiteral("An error type name can be 200 characters at most."));
        CHECK(f.m_Bridge->m_Impacts == 0);
        CHECK(f.m_Session->live_entry_count() == 0);
    }
}

TEST_CASE("other.voiceover: rows read \"label, n today\" and a screen reader's activation records like a tap",
          "[ui][other]")
{
    Fixture f;
    f.record(QStringLiteral("Wrong Response"), 2);
    f.open();

    const std::vector<Node> tree = f.m_Rig.accessible_tree();
    QStringList names;
    for (const Node& node : tree)
    {
        if (node.m_ObjectName.startsWith(QStringLiteral("pickerRow:")))
        {
            CHECK(node.m_Role == QAccessible::Button);
            names.push_back(node.m_Name);
        }
    }
    REQUIRE(names.size() == 12);
    CHECK(names.contains(QStringLiteral("Wrong Response, 2 today")));
    CHECK(names.contains(QStringLiteral("Navigation Failure")));

    const std::vector<Row> listed = rows(f);
    const Row* track = row_for(listed, QStringLiteral("Track Skipped"));
    REQUIRE(track != nullptr);
    CHECK(accessible_press(track->m_Item));
    CHECK(f.m_Bridge->m_Impacts == 1);
    CHECK(f.m_Bridge->m_PendingAtImpact == 0);
    f.commit();
    CHECK(f.model_count(QStringLiteral("Track Skipped")) == 1);

    // The sheet is a dialog with a name, its search field is a search edit, and the swatches are not read.
    f.play(500ms);
    f.open();
    for (const Node& node : f.m_Rig.accessible_tree())
    {
        CHECK(node.m_ObjectName != QStringLiteral("pickerSwatch"));
    }
    QQuickItem* panel = f.find(QStringLiteral("sheetPanel"));
    REQUIRE(panel != nullptr);
    const QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(panel);
    REQUIRE(iface != nullptr);
    CHECK(iface->text(QAccessible::Name) == QStringLiteral("Log another error"));
}

TEST_CASE("other.checks: names, targets, reading order and text scale, in light and dark", "[ui][other]")
{
    Fixture f;
    f.open();

    SECTION("light")
    {
        expect_clean(check_everything(f.m_Rig));
    }

    SECTION("dark")
    {
        f.m_Rig.set_dark(true);
        expect_clean(check_everything(f.m_Rig));
    }

    SECTION("with the add form open")
    {
        // A short list, so that no row is scrolled out of its frame: the reading-order check knows nothing of clipping.
        f.m_Picker->set_filter(QStringLiteral("mute"));
        f.m_Rig.layout();
        f.scroll_to_add();
        f.m_Rig.tap(f.find(QStringLiteral("pickerAdd")));
        f.m_Rig.layout();
        f.m_Rig.tap(f.find(QStringLiteral("pickerAddButton")));
        expect_clean(check_everything(f.m_Rig));
    }

    SECTION("with no match")
    {
        QQuickItem* search = f.find(QStringLiteral("searchInput"));
        REQUIRE(search != nullptr);
        search->forceActiveFocus();
        f.m_Rig.type(QStringLiteral("zzz"));
        f.m_Rig.layout();
        // The message is under the add row, which offers the typed name.
        CHECK(f.m_Rig.geometry(f.find(QStringLiteral("pickerEmpty"))).top() >= f.m_Rig.geometry(f.find(QStringLiteral("pickerAdd"))).bottom());
        expect_clean(check_everything(f.m_Rig));
    }
}

TEST_CASE("other.safe_area: nothing interactive enters an inset, at 59 and 34 and at 79 and 54", "[ui][other]")
{
    Fixture f;
    // A short list, so that nothing scrolls out of the window.
    f.m_Picker->set_filter(QStringLiteral("mute"));

    SECTION("59 and 34")
    {
        f.open();
        CHECK(f.m_Rig.geometry(f.find(QStringLiteral("sheetPanel"))).top() == 54);
        expect_clean(check_safe_area(f.m_Rig, 59, 34));
    }

    SECTION("79 and 54")
    {
        f.m_Sheet->setProperty("overrideTop", 79);
        f.m_Sheet->setProperty("overrideBottom", 54);
        f.open();
        CHECK(f.m_Rig.geometry(f.find(QStringLiteral("sheetPanel"))).top() == 74);
        expect_clean(check_safe_area(f.m_Rig, 79, 54));
    }
}

TEST_CASE("motion.reduce: with Reduce Motion the sheet fades in 100 ms and does not slide", "[ui][other][motion]")
{
    Fixture f;
    f.m_Rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(f.m_Rig));
    f.open(250ms);
    expect_clean(check_motion_reduced(f.m_Rig));
    CHECK(f.is_open());
    CHECK(f.m_Rig.geometry(f.find(QStringLiteral("sheetPanel"))).top() == 54);
}

#include "other_picker_test.moc"

TEST_CASE("other.add_row: adding a type is the first row, above the lists, and a search that finds nothing offers to add it (F10)",
          "[ui][other]")
{
    Fixture f;
    f.open();
    QQuickItem* add = f.find(QStringLiteral("pickerAdd"));
    REQUIRE(add != nullptr);

    // Above the first type row, and in view with the full list of twelve types.
    const std::vector<Row> all = rows(f);
    REQUIRE_FALSE(all.empty());
    CHECK(f.m_Rig.geometry(add).bottom() <= f.m_Rig.geometry(all.front().m_Item).top());
    const QRectF sheet = f.m_Rig.geometry(f.find(QStringLiteral("pickerList")));
    CHECK(f.m_Rig.geometry(add).bottom() <= sheet.bottom());
    CHECK(add->property("text").toString() == QStringLiteral("Add a new error type"));

    // A search with no match: the row offers the typed name, and a tap fills the form with it.
    QQuickItem* input = f.find(QStringLiteral("searchInput"));
    input->forceActiveFocus();
    f.m_Rig.type(QStringLiteral("Zebra"));
    CHECK(rows(f).empty());
    CHECK(add->property("text").toString() == QString::fromUtf8("No match? Add \xE2\x80\x9CZEBRA\xE2\x80\x9D").replace(QStringLiteral("ZEBRA"), f.m_Picker->filter()));
    f.m_Rig.tap(add);
    f.m_Rig.layout();
    QQuickItem* name = f.find(QStringLiteral("pickerNewName"));
    REQUIRE(name != nullptr);
    CHECK(name->isVisible());
    CHECK(name->property("text").toString() == f.m_Picker->filter());

    // A search that finds something keeps the plain label.
    f.m_Rig.tap(f.find(QStringLiteral("clearButton")));
    input->forceActiveFocus();
    f.m_Rig.type(QStringLiteral("mute"));
    CHECK(add->property("text").toString() == QStringLiteral("Add a new error type"));
}
