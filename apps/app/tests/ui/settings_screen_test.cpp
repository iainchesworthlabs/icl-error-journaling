// SPDX-License-Identifier: GPL-3.0-or-later
// SettingsScreen (docs/plan/stage-4.md, 4.12): settings.rows, settings.facilitator, settings.prefix,
// settings.time_format, settings.totals, settings.board_layout, settings.board_limit, settings.export,
// settings.import_row, settings.loading, settings.voiceover, settings.about (PRF-12) and the cross-cutting checks,
// against the real SettingsModel and BoardModel over the fakes of Stage 3 and the sample configuration.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort the includes otherwise, so it is switched
// off.
// clang-format off
#include "settings_rig.hpp"
#include "support/fake_sources.hpp"
#include "viewmodel/board_model.hpp"

#include "about_support.hpp"
#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QGuiApplication>
#include <QMetaObject>
#include <QObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QStyleHints>
#include <QTest>
#include <QVariant>
#include <algorithm>
#include <chrono>
#include <cstddef>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

constexpr const char* WRONG_RESPONSE = "00000000-0000-4000-8000-0000000000a1";
constexpr const char* MUTE_DELAY = "00000000-0000-4000-8000-0000000000a8";
constexpr int OTHER_ROW = 11;

/// The Settings screen over a real SettingsModel that holds the sample configuration. The gateway and the clock of the
/// model are attached to the rig, so `settle` runs the queued save.
class Fixture
{
public:
    explicit Fixture(bool loaded = true, bool with_about = true) : m_WithAbout(with_about)
    {
        if (loaded)
        {
            m_Models.load_sample();
        }
        m_Ui.attach(m_Models.m_World.m_Clock);
        m_Ui.attach(m_Models.m_Gateway);
        load_screen();
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void load_screen()
    {
        QVariantMap given{
            {QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject*>(&m_Models.m_Settings))},
            {QStringLiteral("timingText"), QString::fromLatin1(TIMING_LINE)}};
        if (m_WithAbout)
        {
            given.insert(QStringLiteral("about"), QVariant::fromValue(static_cast<QObject*>(&m_About)));
        }
        m_Screen = m_Ui.load(QStringLiteral("SettingsScreen"), given);
        REQUIRE(m_Screen != nullptr);
        INFO(m_Ui.errors().toStdString());
        m_Ui.layout();
    }

    [[nodiscard]] viewmodel::SettingsModel& settings()
    {
        return m_Models.m_Settings;
    }

    /// The first item with this name that is shown: a page under the top one is in the tree but hidden.
    [[nodiscard]] QQuickItem* shown(const QString& name)
    {
        for (const Node& node : m_Ui.tree())
        {
            if (node.m_ObjectName == name && node.m_Visible)
            {
                return node.m_Item;
            }
        }
        return nullptr;
    }

    [[nodiscard]] QQuickItem* require(const QString& name)
    {
        QQuickItem* item = shown(name);
        INFO("looking for " << name.toStdString());
        REQUIRE(item != nullptr);
        return item;
    }

    /// A tap, and the layout that follows it. A row far down the list is scrolled to first, as the person does.
    void tap(const QString& name)
    {
        QQuickItem* item = require(name);
        if (QQuickItem* flickable = shown(QStringLiteral("settingsFlickable")))
        {
            auto* content = flickable->property("contentItem").value<QQuickItem*>();
            if (content != nullptr && content->isAncestorOf(item))
            {
                const double top = item->mapToItem(content, QPointF(0, 0)).y();
                const double most = std::max(0.0, flickable->property("contentHeight").toDouble() - flickable->height());
                flickable->setProperty("contentY", std::clamp(top - flickable->height() / 3, 0.0, most));
                m_Ui.layout();
            }
        }
        m_Ui.tap(item);
        m_Ui.layout();
    }

    /// Taps the row, and waits for the page to arrive.
    void open_page(const QString& row)
    {
        tap(row);
        m_Ui.advance(400ms);
    }

    /// Taps the Back of the page that is on top, and waits.
    void go_back()
    {
        // The title bar scrolls with the page: bring it back into view, as the person does.
        if (QQuickItem* flickable = shown(QStringLiteral("settingsFlickable")))
        {
            flickable->setProperty("contentY", 0.0);
            m_Ui.layout();
        }
        tap(QStringLiteral("navLeading"));
        m_Ui.advance(400ms);
    }

    /// Whether a text with this wording is shown anywhere.
    [[nodiscard]] bool shows(const QString& needle)
    {
        for (const Node& node : m_Ui.tree())
        {
            if (node.m_Visible && node.m_TypeName.startsWith(QStringLiteral("QQuickText")) &&
                node.m_Item->property("text").toString().contains(needle))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int depth() const
    {
        return m_Screen->property("depth").toInt();
    }

    [[nodiscard]] QString tile_label(int row)
    {
        QQuickItem* tile = require_item(m_Ui, QStringLiteral("miniTile%1").arg(row));
        return require_child(tile, QStringLiteral("miniLabel"))->property("text").toString();
    }

    [[nodiscard]] QString model_label(int row)
    {
        QAbstractItemModel* layout = m_Models.m_Settings.property("boardLayout").value<QAbstractItemModel*>();
        REQUIRE(layout != nullptr);
        return layout->data(layout->index(row, 0), Qt::DisplayRole).toString();
    }

    [[nodiscard]] std::size_t saves()
    {
        return m_Models.store().saves().size();
    }

    /// Scrolls the page that is on top until the item is in view, as the person does.
    void reveal(const QString& name)
    {
        QQuickItem* item = require(name);
        QQuickItem* flickable = require(QStringLiteral("settingsFlickable"));
        auto* content = flickable->property("contentItem").value<QQuickItem*>();
        REQUIRE(content != nullptr);
        const double top = item->mapToItem(content, QPointF(0, 0)).y();
        const double most = std::max(0.0, flickable->property("contentHeight").toDouble() - flickable->height());
        flickable->setProperty("contentY", std::clamp(top - flickable->height() / 3, 0.0, most));
        m_Ui.layout();
    }

    /// Scrolls to the item, taps it, and waits for the page to arrive.
    void reveal_and_open(const QString& name)
    {
        reveal(name);
        open_page(name);
    }

    /// How many items with this name are shown.
    [[nodiscard]] int count_shown(const QString& name)
    {
        int count = 0;
        for (const Node& node : m_Ui.tree())
        {
            if (node.m_ObjectName == name && node.m_Visible)
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] QString text_of(const QString& name)
    {
        return require(name)->property("text").toString();
    }

    [[nodiscard]] QString value_of(const QString& name)
    {
        return require(name)->property("value").toString();
    }

    static constexpr const char* TIMING_LINE = "launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst 77 ms";

    SettingsRig m_Models;
    viewmodel::AboutModel m_About{about_facts(), [] { return about_notices(); }};
    UiRig m_Ui;
    QQuickItem* m_Screen = nullptr;
    bool m_WithAbout = true;
};

/// Types `text` into the field of the page that is on top, in place of what is there.
void replace_text(Fixture& fixture, const QString& text)
{
    QQuickItem* input = fixture.require(QStringLiteral("formInput"));
    fixture.m_Ui.tap(input);
    input->setProperty("text", QString());
    fixture.m_Ui.type(text);
    fixture.m_Ui.layout();
}

/// The hold is a QML Timer, which runs on the wall clock and not on the animation clock of the rig, so a test waits for
/// it. The platform's interval is 500 to 800 ms; a test shortens it for its own duration.
class ShortHold
{
public:
    ShortHold() : m_Before(QGuiApplication::styleHints()->mousePressAndHoldInterval())
    {
        QGuiApplication::styleHints()->setMousePressAndHoldInterval(HOLD_MS);
    }
    ~ShortHold()
    {
        QGuiApplication::styleHints()->setMousePressAndHoldInterval(m_Before);
    }

    ShortHold(const ShortHold&) = delete;
    ShortHold& operator=(const ShortHold&) = delete;

    static constexpr int HOLD_MS = 120;

private:
    int m_Before;
};

/// Presses a tile, waits for the hold, and drags to `to_row`; returns the finger, still down.
int hold_and_drag(Fixture& fixture, int from_row, int to_row)
{
    UiRig& rig = fixture.m_Ui;
    QQuickItem* source = fixture.require(QStringLiteral("miniTile%1").arg(from_row));
    QQuickItem* target = fixture.require(QStringLiteral("miniTile%1").arg(to_row));
    const int finger = rig.press(source);
    rig.wait_real(std::chrono::milliseconds(ShortHold::HOLD_MS + 80));
    rig.advance(16ms);
    const QPointF from = target->mapFromScene(source->mapToScene(QPointF(10, 10)));
    const QPointF to(target->width() / 2, target->height() / 2);
    for (int step = 1; step <= 4; ++step)
    {
        // The window holds back some touch updates until its frame, so each position is sent three times.
        const QPointF at = from + (to - from) * (step / 4.0);
        for (int repeat = 0; repeat < 3; ++repeat)
        {
            rig.move(finger, target, at);
        }
        rig.advance(16ms);
    }
    return finger;
}

double min_target(const Node& /*node*/)
{
    return 44.0;
}

} // namespace

TEST_CASE("settings.rows: the rows show the values of the model, with no counts, and open what they name",
          "[ui][settings_screen]")
{
    Fixture fixture;
    QSignalSpy types(fixture.m_Screen, SIGNAL(errorTypesRequested()));
    QSignalSpy templates(fixture.m_Screen, SIGNAL(templatesRequested()));
    QSignalSpy locations(fixture.m_Screen, SIGNAL(locationsRequested()));

    CHECK(fixture.shows(QStringLiteral("Settings")));
    // The mock-up has no counts on the three lists, no value on Import or export, and six rows of 44 pt.
    CHECK(fixture.require(QStringLiteral("settingsErrorTypes"))->property("value").toString().isEmpty());
    CHECK(fixture.require(QStringLiteral("settingsTemplates"))->property("value").toString().isEmpty());
    CHECK(fixture.require(QStringLiteral("settingsLocations"))->property("value").toString().isEmpty());
    CHECK(fixture.require(QStringLiteral("settingsFacilitator"))->property("value").toString() == "Sam Example");
    CHECK(fixture.require(QStringLiteral("settingsTimeFormat"))->property("value").toString() == "24-hour");
    CHECK(fixture.require(QStringLiteral("settingsImportExport"))->property("value").toString().isEmpty());
    for (const QString& row : {QStringLiteral("settingsErrorTypes"), QStringLiteral("settingsTemplates"),
                               QStringLiteral("settingsLocations"), QStringLiteral("settingsFacilitator"),
                               QStringLiteral("settingsTimeFormat"), QStringLiteral("settingsImportExport")})
    {
        INFO(row.toStdString());
        CHECK(fixture.m_Ui.geometry(fixture.require(row)).height() == 44.0);
    }
    // The unit prefix and the totals line are on the list, and the Facilitator page does not repeat them (F4).
    CHECK(fixture.shown(QStringLiteral("settingsUnitPrefixRow")) != nullptr);
    CHECK(fixture.shown(QStringLiteral("settingsTotalsRow")) != nullptr);
    CHECK(fixture.shown(QStringLiteral("settingsSharedLog")) == nullptr);

    // The rows of the three lists leave the editors to the shell.
    fixture.tap(QStringLiteral("settingsErrorTypes"));
    fixture.tap(QStringLiteral("settingsTemplates"));
    fixture.tap(QStringLiteral("settingsLocations"));
    CHECK(types.count() == 1);
    CHECK(templates.count() == 1);
    CHECK(locations.count() == 1);
    CHECK(fixture.depth() == 1);
}

TEST_CASE("settings.rows: the pages the screen has itself are pushed, and Back returns to the list",
          "[ui][settings_screen]")
{
    Fixture fixture;
    for (const QString& row : {QStringLiteral("settingsFacilitator"), QStringLiteral("settingsTimeFormat"),
                               QStringLiteral("settingsImportExport")})
    {
        INFO(row.toStdString());
        fixture.open_page(row);
        CHECK(fixture.depth() == 2);
        CHECK(fixture.shown(QStringLiteral("settingsNavBar")) != nullptr);
        CHECK(fixture.shown(QStringLiteral("settingsBoardHint")) == nullptr);
        fixture.go_back();
        CHECK(fixture.depth() == 1);
        CHECK(fixture.shown(QStringLiteral("settingsBoardHint")) != nullptr);
    }

    // The shell goes back to the list from any depth.
    fixture.open_page(QStringLiteral("settingsTimeFormat"));
    QMetaObject::invokeMethod(fixture.m_Screen, "popToRoot");
    fixture.m_Ui.advance(400ms);
    CHECK(fixture.depth() == 1);
}

TEST_CASE("settings.facilitator: Save asks the model once, a refusal shows inline and the old name stays",
          "[ui][settings_screen]")
{
    Fixture fixture;
    fixture.open_page(QStringLiteral("settingsFacilitator"));

    // The field starts with the name in force, and Save waits for a change. The log that is open keeps its own name.
    QQuickItem* input = fixture.require(QStringLiteral("formInput"));
    CHECK(input->property("text").toString() == "Sam Example");
    CHECK_FALSE(fixture.require(QStringLiteral("navTrailing"))->isEnabled());
    CHECK(fixture.shows(QStringLiteral("applies from the next log")));

    // An empty name is refused by the model: its words are under the field, the page stays and nothing was saved.
    const std::size_t before = fixture.saves();
    replace_text(fixture, QStringLiteral(" "));
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.settle();
    CHECK(fixture.depth() == 2);
    CHECK(fixture.saves() == before);
    CHECK(fixture.settings().facilitator() == "Sam Example");
    QQuickItem* field = fixture.require(QStringLiteral("settingsTextField"));
    CHECK(field->property("invalid").toBool());
    const QString refusal = field->property("errorText").toString();
    CHECK_FALSE(refusal.isEmpty());
    CHECK(fixture.shows(refusal));

    // Typing again takes the refusal away; a good name is saved once and the page closes.
    replace_text(fixture, QStringLiteral("Alex Example"));
    CHECK_FALSE(field->property("invalid").toBool());
    CHECK(fixture.require(QStringLiteral("navTrailing"))->isEnabled());
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.advance(400ms);
    CHECK(fixture.depth() == 1);
    // The name shows in the list when it has been saved, and not before.
    CHECK(fixture.require(QStringLiteral("settingsFacilitator"))->property("value").toString() == "Sam Example");
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().facilitator() == "Alex Example");
    CHECK(fixture.require(QStringLiteral("settingsFacilitator"))->property("value").toString() == "Alex Example");
}

TEST_CASE("settings.prefix: Save asks the model once and a refusal shows inline", "[ui][settings_screen]")
{
    Fixture fixture;
    // The unit prefix is a row of the list, which opens a page of its own.
    CHECK(fixture.require(QStringLiteral("settingsUnitPrefixRow"))->property("value").toString() == "UNIT");
    fixture.open_page(QStringLiteral("settingsUnitPrefixRow"));
    CHECK(fixture.depth() == 2);
    CHECK(fixture.require(QStringLiteral("formInput"))->property("text").toString() == "UNIT");

    // A prefix with a dash is not one the importer would accept.
    const std::size_t before = fixture.saves();
    replace_text(fixture, QStringLiteral("UNIT-"));
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.settle();
    CHECK(fixture.depth() == 2);
    CHECK(fixture.saves() == before);
    CHECK(fixture.settings().unit_prefix() == "UNIT");
    CHECK(fixture.require(QStringLiteral("settingsTextField"))->property("invalid").toBool());

    replace_text(fixture, QStringLiteral("CELL"));
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.advance(400ms);
    fixture.m_Ui.settle();
    // Back on the list, which shows the prefix once it has been saved.
    CHECK(fixture.depth() == 1);
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().unit_prefix() == "CELL");
    CHECK(fixture.require(QStringLiteral("settingsUnitPrefixRow"))->property("value").toString() == "CELL");
}

TEST_CASE("settings.error_prefix: a row on the list opens a page with a field, an example line and Reset (PRF-13)",
          "[ui][settings_screen][error_prefix]")
{
    Fixture fixture;
    CHECK(fixture.require(QStringLiteral("settingsErrorPrefixRow"))->property("value").toString() == "T|");
    fixture.open_page(QStringLiteral("settingsErrorPrefixRow"));
    CHECK(fixture.depth() == 2);
    CHECK(fixture.require(QStringLiteral("formInput"))->property("text").toString() == "T|");
    QQuickItem* example = fixture.require(QStringLiteral("errorPrefixExample"));
    CHECK(example->property("text").toString() == "10:05 T| Wrong Response");

    // The example follows what is typed, before it is saved.
    replace_text(fixture, QStringLiteral("ERR:"));
    CHECK(example->property("text").toString() == "10:05 ERR: Wrong Response");

    // A prefix of more than 12 characters is refused inline and nothing is saved.
    const std::size_t before = fixture.saves();
    replace_text(fixture, QStringLiteral("1234567890123"));
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.settle();
    CHECK(fixture.depth() == 2);
    CHECK(fixture.saves() == before);
    CHECK(fixture.settings().error_prefix() == "T|");
    CHECK(fixture.require(QStringLiteral("errorPrefixField"))->property("invalid").toBool());

    replace_text(fixture, QStringLiteral("ERR:"));
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.advance(400ms);
    fixture.m_Ui.settle();
    CHECK(fixture.depth() == 1);
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().error_prefix() == "ERR:");
    CHECK(fixture.require(QStringLiteral("settingsErrorPrefixRow"))->property("value").toString() == "ERR:");

    // Reset puts T| back.
    fixture.open_page(QStringLiteral("settingsErrorPrefixRow"));
    fixture.tap(QStringLiteral("errorPrefixReset"));
    fixture.m_Ui.settle();
    CHECK(fixture.settings().error_prefix() == "T|");
    CHECK(fixture.require(QStringLiteral("formInput"))->property("text").toString() == "T|");

    // No prefix at all is allowed, and the list says None.
    CHECK(fixture.settings().setErrorPrefix("").isEmpty());
    fixture.m_Ui.settle();
    CHECK(fixture.require(QStringLiteral("errorPrefixExample"))->property("text").toString() == "10:05 Wrong Response");
}

TEST_CASE("settings.time_format: each choice calls setTimeFormat once and the sample time follows",
          "[ui][settings_screen]")
{
    Fixture fixture;
    fixture.open_page(QStringLiteral("settingsTimeFormat"));
    QQuickItem* twenty_four = fixture.require(QStringLiteral("settingsTime24"));
    QQuickItem* twelve = fixture.require(QStringLiteral("settingsTime12"));
    CHECK(twenty_four->property("checked").toBool());
    CHECK_FALSE(twelve->property("checked").toBool());
    CHECK(fixture.shows(QStringLiteral("14:05")));

    const std::size_t before = fixture.saves();
    fixture.tap(QStringLiteral("settingsTime12"));
    // Until it is saved nothing on the page has changed.
    CHECK(twenty_four->property("checked").toBool());
    CHECK(fixture.shows(QStringLiteral("14:05")));
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().time_style() == "12h");
    CHECK(fixture.settings().time_seconds());
    CHECK_FALSE(twenty_four->property("checked").toBool());
    CHECK(twelve->property("checked").toBool());
    CHECK(fixture.shows(QStringLiteral("02:05:09")));
    CHECK_FALSE(fixture.shows(QStringLiteral("14:05")));

    // Back on the list the row says what the format is.
    fixture.go_back();
    CHECK(fixture.require(QStringLiteral("settingsTimeFormat"))->property("value").toString() ==
          "12-hour with seconds");

    // And back to 24-hour.
    fixture.open_page(QStringLiteral("settingsTimeFormat"));
    fixture.tap(QStringLiteral("settingsTime24"));
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 2);
    CHECK(fixture.settings().time_style() == "24h");
    CHECK_FALSE(fixture.settings().time_seconds());
    CHECK(fixture.shows(QStringLiteral("14:05")));
}

TEST_CASE("settings.totals: the switch calls setIncludeTotals and moves when the setting is saved",
          "[ui][settings_screen]")
{
    Fixture fixture;
    // The switch is a row of the list.
    QQuickItem* row = fixture.require(QStringLiteral("settingsTotalsRow"));
    CHECK_FALSE(row->property("checked").toBool());
    CHECK_FALSE(fixture.settings().include_totals());

    const std::size_t before = fixture.saves();
    fixture.tap(QStringLiteral("settingsTotalsRow"));
    CHECK_FALSE(row->property("checked").toBool());
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().include_totals());
    CHECK(row->property("checked").toBool());
    // The knob is on the other side, so the state is not shown by colour alone.
    QQuickItem* knob = fixture.m_Ui.find_item(QStringLiteral("settingsSwitchKnob"), row);
    QQuickItem* track = fixture.m_Ui.find_item(QStringLiteral("settingsSwitchTrack"), row);
    REQUIRE(knob != nullptr);
    REQUIRE(track != nullptr);
    CHECK(knob->x() + knob->width() / 2 > track->width() / 2);

    fixture.tap(QStringLiteral("settingsTotalsRow"));
    fixture.m_Ui.settle();
    CHECK_FALSE(fixture.settings().include_totals());
    CHECK(knob->x() + knob->width() / 2 < track->width() / 2);
}

TEST_CASE("settings.board_layout: Move later calls moveBoardTile once and the board follows, with each type keeping "
          "its colour",
          "[ui][settings_screen]")
{
    Fixture fixture;
    // The Log screen's board, over the same model.
    FakeOpenLogSource no_log;
    viewmodel::BoardModel log_board(fixture.settings(), no_log);
    const auto log_label = [&log_board](int row)
    { return log_board.data(log_board.index(row), viewmodel::BoardModel::LABEL).toString(); };
    const auto log_slot = [&log_board](int row)
    { return log_board.data(log_board.index(row), viewmodel::BoardModel::COLOUR_SLOT).toInt(); };
    const QString third = fixture.tile_label(2);
    const QString fourth = fixture.tile_label(3);
    const int third_slot = log_slot(2);
    const int fourth_slot = log_slot(3);
    CHECK(third == "Wrong Track Played");
    CHECK(fourth == "Track Not Found");

    fixture.tap(QStringLiteral("miniTile2"));
    fixture.tap(QStringLiteral("miniMoveLater"));
    // Asked once; nothing has moved until the model has saved the move.
    CHECK(fixture.tile_label(2) == third);
    CHECK(log_label(2) == third);
    const std::size_t before = fixture.saves();
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.tile_label(2) == fourth);
    CHECK(fixture.tile_label(3) == third);
    CHECK(fixture.tile_label(OTHER_ROW) == QString::fromUtf8("Other\xE2\x80\xA6 (fixed)"));
    // The selection went with the tile.
    CHECK(fixture.require(QStringLiteral("miniSelectedName"))->property("text").toString() == third);

    // The Log board follows, and the colours stayed with their types.
    CHECK(log_label(2) == fourth);
    CHECK(log_label(3) == third);
    CHECK(log_slot(2) == fourth_slot);
    CHECK(log_slot(3) == third_slot);
    CHECK(fixture.shown(QStringLiteral("settingsMessage")) == nullptr);
}

TEST_CASE("settings.board_layout: a hold and a drag moves a tile, and the page does not scroll while it is dragged",
          "[ui][settings_screen]")
{
    const ShortHold short_hold;
    Fixture fixture;
    QQuickItem* flickable = fixture.require(QStringLiteral("settingsFlickable"));
    const QString dragged = fixture.tile_label(2);
    CHECK(flickable->property("interactive").toBool());

    const int finger = hold_and_drag(fixture, 2, 6);
    CHECK_FALSE(flickable->property("interactive").toBool());
    fixture.m_Ui.release(finger);
    fixture.m_Ui.settle();
    CHECK(flickable->property("interactive").toBool());
    CHECK(fixture.tile_label(6) == dragged);
}

TEST_CASE(
    "settings.board_layout: Change error type opens the picker, a choice calls assignBoardTile and the picker closes",
    "[ui][settings_screen]")
{
    Fixture fixture;
    // A type that is not on the board.
    REQUIRE(fixture.settings().addErrorType(QStringLiteral("Echo Delay")).isEmpty());
    fixture.m_Ui.settle();
    const std::size_t before = fixture.saves();

    fixture.tap(QStringLiteral("miniTile0"));
    fixture.tap(QStringLiteral("miniReplace"));
    fixture.m_Ui.advance(500ms);
    QQuickItem* panel = fixture.shown(QStringLiteral("sheetPanel"));
    REQUIRE(panel != nullptr);
    CHECK(fixture.shows(QStringLiteral("Not on the board")));
    // The tile's own type is ticked, and each row says where its type is.
    QQuickItem* own = fixture.require(QStringLiteral("settingsPickerType_%1").arg(WRONG_RESPONSE));
    CHECK(own->property("checked").toBool());
    CHECK(own->property("value").toString() == "Position 1");
    CHECK_FALSE(fixture.require(QStringLiteral("settingsPickerType_%1").arg(MUTE_DELAY))->property("checked").toBool());

    // A type that is on the board elsewhere swaps places with the tile's.
    fixture.m_Ui.tap(fixture.require(QStringLiteral("settingsPickerType_%1").arg(MUTE_DELAY)));
    fixture.m_Ui.settle();
    fixture.m_Ui.advance(500ms);
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.tile_label(0) == "Mute Delay");
    CHECK(fixture.tile_label(7) == "Wrong Response");
    CHECK(fixture.shown(QStringLiteral("sheetPanel")) == nullptr);

    // A type that is not on the board takes the tile and sends the tile's type off it.
    fixture.tap(QStringLiteral("miniReplace"));
    fixture.m_Ui.advance(500ms);
    REQUIRE(fixture.shown(QStringLiteral("sheetPanel")) != nullptr);
    for (const Node& node : fixture.m_Ui.tree())
    {
        if (node.m_ObjectName.startsWith(QStringLiteral("settingsPickerType_")) && node.m_Visible &&
            node.m_Item->property("text").toString() == "Echo Delay")
        {
            fixture.m_Ui.tap(node.m_Item);
            break;
        }
    }
    fixture.m_Ui.settle();
    fixture.m_Ui.advance(500ms);
    CHECK(fixture.saves() == before + 2);
    CHECK(fixture.tile_label(0) == "Echo Delay");
    CHECK(fixture.shown(QStringLiteral("sheetPanel")) == nullptr);
}

/// The sample with Track Skipped retired, so that position 5 is empty.
void empty_position_five(Fixture& fixture)
{
    REQUIRE(fixture.settings().retireErrorType(QStringLiteral("00000000-0000-4000-8000-0000000000a5")).isEmpty());
    fixture.m_Ui.settle();
    REQUIRE(fixture.model_label(4).isEmpty());
    REQUIRE(fixture.tile_label(4) == QStringLiteral("Add error type"));
}

TEST_CASE("settings.board_empty: an empty tile opens the picker at once, with New error type at the top",
          "[ui][settings_screen]")
{
    Fixture fixture;
    empty_position_five(fixture);
    const std::size_t before = fixture.saves();

    fixture.tap(QStringLiteral("miniTile4"));
    fixture.m_Ui.advance(500ms);
    // No panel first: the picker is up, for the empty position.
    CHECK(fixture.shown(QStringLiteral("miniPanel")) == nullptr);
    REQUIRE(fixture.shown(QStringLiteral("sheetPanel")) != nullptr);
    CHECK(fixture.require(QStringLiteral("settingsPickerBar"))->property("title").toString() == "Add error type");
    CHECK(fixture.shows(QStringLiteral("For position 5 of 11.")));
    QQuickItem* new_row = fixture.require(QStringLiteral("settingsPickerNewType"));
    CHECK(new_row->property("text").toString() == QStringLiteral("New error type…"));
    // The new row is above the list of types.
    QQuickItem* first_type = fixture.require(QStringLiteral("settingsPickerType_%1").arg(WRONG_RESPONSE));
    CHECK(new_row->mapToScene(QPointF(0, 0)).y() < first_type->mapToScene(QPointF(0, 0)).y());

    // A type from the list fills the tile.
    fixture.m_Ui.tap(first_type);
    fixture.m_Ui.settle();
    fixture.m_Ui.advance(500ms);
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.tile_label(4) == "Wrong Response");
    CHECK(fixture.model_label(0).isEmpty());
}

TEST_CASE("settings.new_type: New error type names a type and puts it on the tile in one save", "[ui][settings_screen]")
{
    Fixture fixture;
    empty_position_five(fixture);
    QSignalSpy added(&fixture.settings(), &viewmodel::SettingsModel::errorTypeAdded);
    const std::size_t before = fixture.saves();

    fixture.tap(QStringLiteral("miniTile4"));
    fixture.m_Ui.advance(500ms);
    fixture.tap(QStringLiteral("settingsPickerNewType"));
    fixture.m_Ui.advance(500ms);

    // The name sheet, with the colour the type will get and where it goes.
    QQuickItem* sheet = fixture.require(QStringLiteral("settingsNewType"));
    CHECK(sheet->property("boardPosition").toInt() == 5);
    CHECK(fixture.shown(QStringLiteral("settingsPickerNewType")) == nullptr);
    CHECK(fixture.require(QStringLiteral("colourGroup"))->property("footer").toString() ==
          QStringLiteral("It goes on the board at position 5."));
    // The twelfth type in the master list (the retired one counts) takes slot 1, Sky.
    CHECK(fixture.require(QStringLiteral("colourName"))->property("text").toString() == QStringLiteral("Sky"));
    CHECK(fixture.shown(QStringLiteral("retireRow")) == nullptr);

    replace_text(fixture, QStringLiteral("Echo Delay"));
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.settle();
    fixture.m_Ui.advance(500ms);

    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.tile_label(4) == "Echo Delay");
    CHECK(fixture.shown(QStringLiteral("sheetPanel")) == nullptr);
    REQUIRE(added.count() == 1);
    CHECK(added.at(0).at(1).toInt() == 5);
}

TEST_CASE("settings.new_type: a taken name is refused in the sheet and nothing is placed", "[ui][settings_screen]")
{
    Fixture fixture;
    empty_position_five(fixture);
    const std::size_t before = fixture.saves();

    fixture.tap(QStringLiteral("miniTile4"));
    fixture.m_Ui.advance(500ms);
    fixture.tap(QStringLiteral("settingsPickerNewType"));
    fixture.m_Ui.advance(500ms);
    replace_text(fixture, QStringLiteral("mute delay"));
    fixture.tap(QStringLiteral("navTrailing"));
    fixture.m_Ui.settle();

    CHECK(fixture.saves() == before);
    CHECK(fixture.shown(QStringLiteral("settingsNewType")) != nullptr);
    CHECK(fixture.shows(QStringLiteral("There is already an error type called")));
    CHECK(fixture.model_label(4).isEmpty());
}

TEST_CASE("settings.board_empty: the shell's way in opens the picker for a position", "[ui][settings_screen]")
{
    Fixture fixture;
    empty_position_five(fixture);
    QMetaObject::invokeMethod(fixture.m_Screen, "addTypeAt", Q_ARG(int, 5));
    fixture.m_Ui.advance(500ms);
    REQUIRE(fixture.shown(QStringLiteral("sheetPanel")) != nullptr);
    CHECK(fixture.shows(QStringLiteral("For position 5 of 11.")));
    // VoiceOver reaches the new row, as a named button.
    QQuickItem* new_row = fixture.require(QStringLiteral("settingsPickerNewType"));
    CHECK(new_row->property("accessibleName").toString() == QStringLiteral("New error type for position 5"));
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(new_row);
    REQUIRE(iface != nullptr);
    CHECK(iface->role() == QAccessible::Button);
    CHECK(accessible_press(new_row));
    fixture.m_Ui.advance(500ms);
    CHECK(fixture.shown(QStringLiteral("settingsNewType")) != nullptr);
}

TEST_CASE("settings.board_layout: Cancel closes the picker and changes nothing", "[ui][settings_screen]")
{
    Fixture fixture;
    const std::size_t before = fixture.saves();
    fixture.tap(QStringLiteral("miniTile1"));
    fixture.tap(QStringLiteral("miniReplace"));
    fixture.m_Ui.advance(500ms);
    REQUIRE(fixture.shown(QStringLiteral("sheetPanel")) != nullptr);
    fixture.tap(QStringLiteral("navLeading"));
    fixture.m_Ui.advance(500ms);
    CHECK(fixture.shown(QStringLiteral("sheetPanel")) == nullptr);
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before);
    CHECK(fixture.tile_label(1) == "Sequence Out of Order");
}

TEST_CASE("settings.board_limit: a change the model refuses shows its words inline, on the list and in the picker",
          "[ui][settings_screen]")
{
    Fixture fixture;
    const std::size_t before = fixture.saves();

    // A first move is being saved. A second change is refused, in words, and shows above the board.
    fixture.tap(QStringLiteral("miniTile2"));
    fixture.tap(QStringLiteral("miniMoveLater"));
    CHECK(fixture.settings().saving());
    fixture.tap(QStringLiteral("miniMoveLater"));
    QQuickItem* message = fixture.shown(QStringLiteral("settingsMessage"));
    REQUIRE(message != nullptr);
    CHECK(fixture.shows(QStringLiteral("still being saved")));
    CHECK(fixture.saves() == before);

    // The first save finishes; the refusal stays until it is dismissed or another call succeeds.
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.tile_label(3) == "Wrong Track Played");
    CHECK(fixture.shown(QStringLiteral("settingsMessage")) != nullptr);
    fixture.tap(QStringLiteral("bannerClose"));
    CHECK(fixture.shown(QStringLiteral("settingsMessage")) == nullptr);

    // The same in the picker: the sheet stays open and says why.
    fixture.tap(QStringLiteral("miniMoveEarlier"));
    CHECK(fixture.settings().saving());
    fixture.tap(QStringLiteral("miniReplace"));
    fixture.m_Ui.advance(500ms);
    fixture.m_Ui.tap(fixture.require(QStringLiteral("settingsPickerType_%1").arg(MUTE_DELAY)));
    fixture.m_Ui.advance(500ms);
    CHECK(fixture.shown(QStringLiteral("sheetPanel")) != nullptr);
    CHECK(fixture.shown(QStringLiteral("settingsPickerMessage")) != nullptr);
    CHECK(fixture.shows(QStringLiteral("still being saved")));
    fixture.m_Ui.settle();
}

TEST_CASE("settings.board_limit: a write that fails shows the model's words", "[ui][settings_screen]")
{
    Fixture fixture;
    fixture.m_Models.store().fail_next_save(make_error(StorageErrorKind::DISK_FULL, "disk full"));
    fixture.tap(QStringLiteral("miniTile2"));
    fixture.tap(QStringLiteral("miniMoveLater"));
    fixture.m_Ui.settle();
    CHECK(fixture.tile_label(2) == "Wrong Track Played");
    CHECK(fixture.shown(QStringLiteral("settingsMessage")) != nullptr);
}

TEST_CASE("settings.export: the rows emit the requests and the result of an export is shown", "[ui][settings_screen]")
{
    Fixture fixture;
    QSignalSpy imports(fixture.m_Screen, SIGNAL(importRequested()));
    QSignalSpy exports(fixture.m_Screen, SIGNAL(exportRequested()));
    fixture.open_page(QStringLiteral("settingsImportExport"));
    CHECK(fixture.shown(QStringLiteral("settingsExportResult")) == nullptr);
    CHECK(fixture.shows(QStringLiteral("does not contain any logs")));
    // The note is not about one platform: no AirDrop, which Android has not got.
    CHECK(fixture.shows(QStringLiteral("through your device’s file picker")));
    CHECK_FALSE(fixture.shows(QStringLiteral("AirDrop")));

    fixture.tap(QStringLiteral("settingsExport"));
    CHECK(exports.count() == 1);
    CHECK(imports.count() == 0);
    // The screen asks and the shell answers: the model tells how it went.
    fixture.settings().exportResult(true, QStringLiteral("Exported to Files"),
                                    QStringLiteral("cs-bingo-board-setup.ejconfig"),
                                    QStringLiteral("cs-bingo-board-setup.ejconfig is ready to send."));
    fixture.m_Ui.layout();
    REQUIRE(fixture.shown(QStringLiteral("settingsExportResult")) != nullptr);
    CHECK(fixture.shows(QStringLiteral("Exported to Files")));
    CHECK(fixture.shows(QStringLiteral("cs-bingo-board-setup.ejconfig is ready to send.")));
    fixture.tap(QStringLiteral("bannerClose"));
    CHECK(fixture.shown(QStringLiteral("settingsExportResult")) == nullptr);

    fixture.settings().exportResult(false, QStringLiteral("Export failed"),
                                    QStringLiteral("cs-bingo-board-setup.ejconfig"),
                                    QStringLiteral("There was no room to write the file."));
    fixture.m_Ui.layout();
    CHECK(fixture.shown(QStringLiteral("settingsExportResult")) != nullptr);
    CHECK(fixture.shows(QStringLiteral("There was no room to write the file.")));
    CHECK(fixture.shows(QStringLiteral("Export failed")));

    // Asking again takes the old result away.
    fixture.tap(QStringLiteral("settingsExport"));
    CHECK(exports.count() == 2);
    CHECK(fixture.shown(QStringLiteral("settingsExportResult")) == nullptr);
}

TEST_CASE("settings.import_row: the row emits importRequested and nothing else", "[ui][settings_screen]")
{
    Fixture fixture;
    QSignalSpy imports(fixture.m_Screen, SIGNAL(importRequested()));
    QSignalSpy exports(fixture.m_Screen, SIGNAL(exportRequested()));
    fixture.open_page(QStringLiteral("settingsImportExport"));
    fixture.tap(QStringLiteral("settingsImport"));
    CHECK(imports.count() == 1);
    CHECK(exports.count() == 0);
    CHECK(fixture.depth() == 2);
}

TEST_CASE("settings.loading: the skeleton shows until the setup has been read, and offers nothing",
          "[ui][settings_screen]")
{
    Fixture fixture(false);
    REQUIRE(fixture.settings().loading());
    QQuickItem* skeleton = fixture.shown(QStringLiteral("settingsSkeleton"));
    REQUIRE(skeleton != nullptr);

    // Shapes only: twelve tiles and six bars, and no text of the setup, no row and no board.
    int tiles = 0;
    int bars = 0;
    for (const Node& node : fixture.m_Ui.tree(skeleton))
    {
        if (node.m_ObjectName == "settingsSkeletonTile")
        {
            ++tiles;
        }
        if (node.m_ObjectName == "settingsSkeletonBar")
        {
            ++bars;
        }
        CHECK_FALSE(node.m_Interactive);
    }
    CHECK(tiles == 12);
    CHECK(bars == 6);
    CHECK(fixture.shown(QStringLiteral("settingsFacilitator")) == nullptr);
    CHECK(fixture.shown(QStringLiteral("miniTile0")) == nullptr);
    CHECK_FALSE(fixture.shows(QStringLiteral("Sam Example")));
    expect_clean(check_everything(fixture.m_Ui, min_target));

    // The setup arrives: the skeleton goes, the board and the rows are there.
    fixture.m_Models.load_sample();
    fixture.m_Ui.settle();
    CHECK_FALSE(fixture.settings().loading());
    CHECK(fixture.shown(QStringLiteral("settingsSkeleton")) == nullptr);
    CHECK(fixture.shown(QStringLiteral("settingsFacilitator")) != nullptr);
    CHECK(fixture.shown(QStringLiteral("miniTile0")) != nullptr);
}

TEST_CASE("settings.voiceover: rows read their label and value, the switch reads its state, every control is reachable",
          "[ui][settings_screen][a11y]")
{
    Fixture fixture;
    const auto name_of = [&fixture](const QString& object_name)
    {
        for (const Node& node : fixture.m_Ui.accessible_tree())
        {
            if (node.m_ObjectName == object_name && node.m_Visible)
            {
                return node;
            }
        }
        return Node{};
    };

    const Node facilitator = name_of(QStringLiteral("settingsFacilitator"));
    CHECK(facilitator.m_Role == QAccessible::Button);
    CHECK(facilitator.m_Name == "Facilitator, Sam Example");
    CHECK(name_of(QStringLiteral("settingsErrorTypes")).m_Name == "Error types");
    CHECK(name_of(QStringLiteral("settingsTimeFormat")).m_Name == "Time format, 24-hour");

    // The totals switch is a row of the list.
    const Node totals = name_of(QStringLiteral("settingsTotalsRow"));
    CHECK(totals.m_Role == QAccessible::CheckBox);
    CHECK(totals.m_Name == "Include a totals line");
    QAccessibleInterface* iface =
        QAccessible::queryAccessibleInterface(fixture.require(QStringLiteral("settingsTotalsRow")));
    REQUIRE(iface != nullptr);
    CHECK_FALSE(iface->state().checked);

    // The press action of VoiceOver does what a tap does.
    CHECK(accessible_press(fixture.require(QStringLiteral("settingsTotalsRow"))));
    fixture.m_Ui.settle();
    CHECK(fixture.settings().include_totals());
    CHECK(iface->state().checked);

    // The board: every tile but Other is a button, and the buttons for a selected tile are there for VoiceOver.
    CHECK(accessible_press(fixture.require(QStringLiteral("miniTile4"))));
    fixture.m_Ui.layout();
    CHECK(name_of(QStringLiteral("miniMoveEarlier")).m_Role == QAccessible::Button);
    CHECK(name_of(QStringLiteral("miniMoveLater")).m_Role == QAccessible::Button);
    CHECK(name_of(QStringLiteral("miniReplace")).m_Role == QAccessible::Button);
    CHECK(accessible_press(fixture.require(QStringLiteral("miniMoveLater"))));
    fixture.m_Ui.settle();
    CHECK(fixture.tile_label(5) == "Track Skipped");

    // The pages: rows are buttons, a choice reads as checked.
    CHECK(accessible_press(fixture.require(QStringLiteral("settingsTimeFormat"))));
    fixture.m_Ui.advance(400ms);
    CHECK(name_of(QStringLiteral("settingsTime24")).m_Role == QAccessible::Button);
    QAccessibleInterface* choice =
        QAccessible::queryAccessibleInterface(fixture.require(QStringLiteral("settingsTime24")));
    REQUIRE(choice != nullptr);
    CHECK(choice->state().checked);
    CHECK(name_of(QStringLiteral("navLeading")).m_Name == "Back to Settings");
}

TEST_CASE("settings.a11y: names, targets, reading order and the text scales, in light and dark, with every page",
          "[ui][settings_screen][a11y]")
{
    Fixture fixture;
    for (const bool dark : {false, true})
    {
        fixture.m_Ui.set_dark(dark);
        INFO((dark ? "dark" : "light"));
        expect_clean(check_everything(fixture.m_Ui, min_target));

        // A tile selected: the panel with its three buttons.
        fixture.tap(QStringLiteral("miniTile4"));
        expect_clean(check_everything(fixture.m_Ui, min_target));
        fixture.tap(QStringLiteral("miniDone"));

        // A refusal on the list.
        fixture.tap(QStringLiteral("miniTile2"));
        fixture.tap(QStringLiteral("miniMoveLater"));
        fixture.tap(QStringLiteral("miniMoveLater"));
        expect_clean(check_everything(fixture.m_Ui, min_target));
        fixture.m_Ui.settle();
        fixture.tap(QStringLiteral("miniMoveEarlier"));
        fixture.m_Ui.settle();
        fixture.tap(QStringLiteral("miniDone"));
        if (QQuickItem* close = fixture.shown(QStringLiteral("bannerClose")))
        {
            fixture.m_Ui.tap(close);
        }

        for (const QString& row : {QStringLiteral("settingsFacilitator"), QStringLiteral("settingsTimeFormat"),
                                   QStringLiteral("settingsImportExport")})
        {
            INFO(row.toStdString());
            fixture.open_page(row);
            expect_clean(check_everything(fixture.m_Ui, min_target));
            fixture.go_back();
        }
        // The unit prefix is a page of its own, opened from the list.
        fixture.open_page(QStringLiteral("settingsUnitPrefixRow"));
        expect_clean(check_everything(fixture.m_Ui, min_target));
        fixture.go_back();
    }
}

TEST_CASE("settings.a11y: the picker sheet passes the checks on its own, in light and dark",
          "[ui][settings_screen][a11y]")
{
    for (const bool dark : {false, true})
    {
        SettingsRig models;
        models.load_sample();
        UiRig rig;
        rig.set_dark(dark);
        INFO((dark ? "dark" : "light"));
        QQuickItem* picker =
            rig.load(QStringLiteral("SettingsTypePicker"),
                     {{QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject*>(&models.m_Settings))},
                      {QStringLiteral("overrideInsets"), true},
                      {QStringLiteral("overrideTop"), 59},
                      {QStringLiteral("overrideBottom"), 34}});
        REQUIRE(picker != nullptr);
        INFO(rig.errors().toStdString());
        QMetaObject::invokeMethod(picker, "openFor", Q_ARG(int, 3), Q_ARG(QString, QString(MUTE_DELAY)));
        rig.advance(500ms);
        expect_clean(check_everything(rig, min_target));
        expect_clean(check_safe_area(rig, 59, 0));
    }
}

TEST_CASE("settings.a11y: the list clears the top inset and nothing is cut off", "[ui][settings_screen][a11y]")
{
    Fixture fixture;
    fixture.m_Screen->setProperty("overrideInsets", true);
    fixture.m_Screen->setProperty("overrideTop", 59);
    fixture.m_Screen->setProperty("overrideBottom", 34);
    fixture.m_Ui.layout();
    // The list scrolls, so at rest it starts below the inset, and so does every page that is pushed on it. (The stack
    // and the page are as big as the window, so the window-wide check of the rig is not used on them.)
    CHECK(fixture.m_Ui.geometry(fixture.require(QStringLiteral("settingsNavBar"))).top() >= 59);
    CHECK(fixture.m_Ui.geometry(fixture.require(QStringLiteral("miniGrid"))).top() > 59);
    for (const QString& row : {QStringLiteral("settingsFacilitator"), QStringLiteral("settingsTimeFormat")})
    {
        fixture.open_page(row);
        CHECK(fixture.m_Ui.geometry(fixture.require(QStringLiteral("settingsNavBar"))).top() >= 59);
        CHECK(fixture.m_Ui.geometry(fixture.require(QStringLiteral("navLeading"))).top() >= 59);
        fixture.go_back();
    }
}

TEST_CASE("settings.motion: with Reduce Motion the pages fade in 100 ms and the sheet does not slide",
          "[ui][settings_screen]")
{
    Fixture fixture;
    fixture.m_Ui.set_reduce_motion(true);
    fixture.open_page(QStringLiteral("settingsTimeFormat"));
    expect_clean(check_motion_reduced(fixture.m_Ui));
    fixture.go_back();
    fixture.tap(QStringLiteral("miniTile1"));
    fixture.tap(QStringLiteral("miniReplace"));
    fixture.m_Ui.advance(200ms);
    expect_clean(check_motion_reduced(fixture.m_Ui));
}

TEST_CASE("settings.about: the last row of the list opens the About page and Back returns to the list",
          "[ui][settings_screen][about]")
{
    Fixture fixture;
    fixture.reveal(QStringLiteral("settingsAboutRow"));
    QQuickItem* row = fixture.require(QStringLiteral("settingsAboutRow"));
    CHECK(row->property("text").toString() == "About CS Bingo Board");
    CHECK(row->property("showChevron").toBool());
    CHECK(fixture.m_Ui.geometry(row).height() == 44.0);
    // After the six rows of the setup, in a group of its own.
    CHECK(fixture.m_Ui.geometry(row).top() >
          fixture.m_Ui.geometry(fixture.require(QStringLiteral("settingsImportExport"))).bottom());

    fixture.tap(QStringLiteral("settingsAboutRow"));
    fixture.m_Ui.advance(400ms);
    CHECK(fixture.depth() == 2);
    CHECK(fixture.shown(QStringLiteral("aboutBuild")) != nullptr);
    CHECK(fixture.shown(QStringLiteral("settingsBoardHint")) == nullptr);
    fixture.go_back();
    CHECK(fixture.depth() == 1);
    CHECK(fixture.shown(QStringLiteral("settingsBoardHint")) != nullptr);
}

TEST_CASE("settings.about: without the model there is no About row", "[ui][settings_screen][about]")
{
    Fixture fixture(true, false);
    CHECK(fixture.shown(QStringLiteral("settingsAboutRow")) == nullptr);
    CHECK(fixture.shown(QStringLiteral("settingsImportExport")) != nullptr);
}

TEST_CASE("settings.about: the foot of the list is a plain caption with no timing line", "[ui][settings_screen][about]")
{
    Fixture fixture;
    fixture.m_Screen->setProperty("aboutText", QStringLiteral("0.1.0 · ios, storage: sqlite"));
    fixture.m_Ui.layout();
    fixture.reveal(QStringLiteral("settingsAboutFooter"));
    CHECK(fixture.text_of(QStringLiteral("settingsVersionCaption")) == "0.1.0 · ios, storage: sqlite");
    QQuickItem* footer = fixture.require(QStringLiteral("settingsAboutFooter"));
    for (int i = 0; i < 6; ++i)
    {
        fixture.m_Ui.tap(footer);
    }
    fixture.m_Ui.layout();
    CHECK(fixture.shown(QStringLiteral("settingsTiming")) == nullptr);
    CHECK(fixture.depth() == 1);
}

TEST_CASE("settings.about: the page says what this build is", "[ui][settings_screen][about]")
{
    Fixture fixture;
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    REQUIRE(fixture.depth() == 2);
    CHECK(fixture.value_of(QStringLiteral("aboutName")) == "CS Bingo Board");
    CHECK(fixture.value_of(QStringLiteral("aboutVersion")) == "0.1.0");
    CHECK(fixture.value_of(QStringLiteral("aboutBuildNumber")) == "57");
    CHECK(fixture.value_of(QStringLiteral("aboutCommit")) == "a1b2c3d");
    CHECK(fixture.value_of(QStringLiteral("aboutRef")) == "develop");
    CHECK(fixture.value_of(QStringLiteral("aboutBuildDate")) == "2026-11-09 03:21 UTC");
    CHECK(fixture.value_of(QStringLiteral("aboutPlatform")) == "ios");
    CHECK(fixture.value_of(QStringLiteral("aboutQt")) == "6.12.0");
    CHECK(fixture.value_of(QStringLiteral("aboutStorage")) == "sqlite");
    CHECK(fixture.value_of(QStringLiteral("aboutLicenceName")) == "GPL-3.0-or-later");
    CHECK(fixture.shown(QStringLiteral("aboutProblem")) == nullptr);
    // One row for each component of the notices.
    CHECK(fixture.m_Ui.find_items(QStringLiteral("aboutComponent")).size() == 4);
}

TEST_CASE(
    "settings.about: a build with no branch or tag leaves that row out and says unknown for what it does not know",
    "[ui][settings_screen][about]")
{
    Fixture fixture(true, false);
    viewmodel::AboutFacts facts = about_facts();
    facts.m_Ref.clear();
    facts.m_Commit = QStringLiteral("unknown");
    facts.m_BuildDate = QStringLiteral("unknown");
    viewmodel::AboutModel bare(facts, [] { return about_notices(); });
    fixture.m_Screen->setProperty("about", QVariant::fromValue(static_cast<QObject*>(&bare)));
    fixture.m_Ui.layout();
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    REQUIRE(fixture.depth() == 2);
    CHECK(fixture.shown(QStringLiteral("aboutRef")) == nullptr);
    CHECK(fixture.value_of(QStringLiteral("aboutCommit")) == "unknown");
    CHECK(fixture.value_of(QStringLiteral("aboutBuildDate")) == "unknown");
}

TEST_CASE("settings.about: notices that cannot be read are said so, and the facts of the build stay",
          "[ui][settings_screen][about]")
{
    Fixture fixture(true, false);
    viewmodel::AboutModel broken(about_facts(), [] { return QByteArray("not json"); });
    fixture.m_Screen->setProperty("about", QVariant::fromValue(static_cast<QObject*>(&broken)));
    fixture.m_Ui.layout();
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    REQUIRE(fixture.depth() == 2);
    CHECK(fixture.shown(QStringLiteral("aboutProblem")) != nullptr);
    CHECK(fixture.shows(QStringLiteral("could not be read")));
    CHECK(fixture.value_of(QStringLiteral("aboutVersion")) == "0.1.0");
    CHECK(fixture.m_Ui.find_items(QStringLiteral("aboutComponent")).empty());
}

TEST_CASE("settings.about: five quick taps on the Version row show the timing line, and five more hide it",
          "[ui][settings_screen][about]")
{
    Fixture fixture;
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    QQuickItem* version = fixture.require(QStringLiteral("aboutVersion"));
    // It is read as text, not as a control, and says nothing of the gesture.
    CHECK_FALSE(version->property("tappable").toBool());
    CHECK(fixture.shown(QStringLiteral("settingsTiming")) == nullptr);

    for (int i = 0; i < 4; ++i)
    {
        fixture.m_Ui.tap(version);
    }
    CHECK(fixture.shown(QStringLiteral("settingsTiming")) == nullptr);
    fixture.m_Ui.tap(version);
    fixture.m_Ui.advance(16ms);
    REQUIRE(fixture.shown(QStringLiteral("settingsTiming")) != nullptr);
    CHECK(fixture.text_of(QStringLiteral("settingsTiming")) == QString::fromLatin1(Fixture::TIMING_LINE));

    for (int i = 0; i < 5; ++i)
    {
        fixture.m_Ui.tap(version);
    }
    fixture.m_Ui.advance(16ms);
    CHECK(fixture.shown(QStringLiteral("settingsTiming")) == nullptr);
}

TEST_CASE("settings.about: the source row asks for the address of the repository and opens no page",
          "[ui][settings_screen][about]")
{
    Fixture fixture;
    QSignalSpy opened(fixture.m_Screen, SIGNAL(openUrlRequested(QString)));
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    fixture.reveal(QStringLiteral("aboutSourceUrl"));
    CHECK(fixture.text_of(QStringLiteral("aboutSourceUrl")) ==
          "https://github.com/iainchesworthlabs/icl-error-journaling");
    fixture.reveal(QStringLiteral("aboutSourceLink"));
    fixture.tap(QStringLiteral("aboutSourceLink"));
    REQUIRE(opened.count() == 1);
    CHECK(opened.at(0).at(0).toString() == "https://github.com/iainchesworthlabs/icl-error-journaling");
    CHECK(fixture.depth() == 2);
}

TEST_CASE("settings.about: Read the licence opens the full text of the licence of the app, as paragraphs",
          "[ui][settings_screen][about]")
{
    Fixture fixture;
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    fixture.reveal_and_open(QStringLiteral("aboutLicenceText"));
    REQUIRE(fixture.depth() == 3);
    CHECK(fixture.value_of(QStringLiteral("licenceExpression")) == "GPL-3.0-or-later");
    CHECK(fixture.value_of(QStringLiteral("licenceName")) == "CS Bingo Board");
    // The copyright line and the source address, then the text under the name of the licence: six paragraphs of three
    // joined lines.
    CHECK(fixture.count_shown(QStringLiteral("licenceHeading")) == 3);
    CHECK(fixture.shows(QStringLiteral("GPL-3.0-OR-LATER")));
    CHECK(fixture.count_shown(QStringLiteral("licenceParagraph")) == 1 + 1 + 6);
    CHECK(
        fixture.shows(QStringLiteral("1. Permission is granted to use, copy and share this sample text under the terms "
                                     "written here, as long as the notice stays with every copy")));
    fixture.go_back();
    CHECK(fixture.depth() == 2);
    fixture.go_back();
    CHECK(fixture.depth() == 1);
}

TEST_CASE("settings.about: a component row opens its page with the text of each licence it is offered under",
          "[ui][settings_screen][about]")
{
    Fixture fixture;
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    const auto rows = fixture.m_Ui.find_items(QStringLiteral("aboutComponent"));
    REQUIRE(rows.size() == 4);
    // The row reads as one button: name and version, then the licence.
    CHECK(rows[0]->property("title").toString() == "Sample Base 6.12.0");
    CHECK(rows[0]->property("licence").toString() == "Qt commercial licence OR LGPL-3.0-only OR GPL-3.0-only");

    fixture.reveal(QStringLiteral("aboutComponent"));
    fixture.m_Ui.tap(rows[0]);
    fixture.m_Ui.advance(400ms);
    REQUIRE(fixture.depth() == 3);
    CHECK(fixture.shows(QStringLiteral("Sample Base")));
    CHECK(fixture.value_of(QStringLiteral("licenceVersion")) == "6.12.0");
    CHECK(fixture.value_of(QStringLiteral("licenceExpression")) ==
          "Qt commercial licence OR LGPL-3.0-only OR GPL-3.0-only");
    CHECK(fixture.value_of(QStringLiteral("licenceLinking")) == "Static");
    CHECK(fixture.shown(QStringLiteral("licenceName")) == nullptr);
    CHECK(fixture.shows(QStringLiteral("Part of the sample set.")));
    CHECK(fixture.shows(QStringLiteral("Copyright (c) 2020 Sam Example")));
    CHECK(fixture.shows(QStringLiteral("Copyright (c) 2021 North Site Authors")));
    CHECK(fixture.shows(QStringLiteral("https://example.invalid/sample base.tar.xz")));
    // The two choices, each with its whole text.
    CHECK(fixture.shows(QStringLiteral("LGPL-3.0-ONLY")));
    CHECK(fixture.shows(QStringLiteral("GPL-3.0-ONLY")));
    CHECK(fixture.shows(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
    fixture.go_back();
    CHECK(fixture.depth() == 2);
}

TEST_CASE("settings.about: a dedication that needs no text and a component's own file have pages too",
          "[ui][settings_screen][about]")
{
    Fixture fixture;
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    const auto rows = fixture.m_Ui.find_items(QStringLiteral("aboutComponent"));
    REQUIRE(rows.size() == 4);

    fixture.reveal(QStringLiteral("aboutComponent"));
    fixture.m_Ui.tap(rows[2]);
    fixture.m_Ui.advance(400ms);
    REQUIRE(fixture.depth() == 3);
    CHECK(fixture.shows(QStringLiteral("Hash (in Sample Base)")));
    CHECK(fixture.shows(QStringLiteral("A public-domain dedication: it sets no condition.")));
    fixture.go_back();

    fixture.reveal(QStringLiteral("aboutComponent"));
    fixture.m_Ui.tap(rows[3]);
    fixture.m_Ui.advance(400ms);
    REQUIRE(fixture.depth() == 3);
    CHECK(fixture.value_of(QStringLiteral("licenceLinking")) == "Static and dynamic");
    CHECK(fixture.shows(QStringLiteral("LICENCE FILE")));
    CHECK(fixture.shows(QStringLiteral("Sample Library terms.")));
    // Nothing is made up for a component with no copyright and no source.
    CHECK(fixture.shown(QStringLiteral("licenceMissing")) == nullptr);
    fixture.go_back();
}

TEST_CASE("settings.about: every page passes the checks in light and dark", "[ui][settings_screen][about][a11y]")
{
    Fixture fixture;
    for (const bool dark : {false, true})
    {
        fixture.m_Ui.set_dark(dark);
        INFO((dark ? "dark" : "light"));
        fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
        expect_clean(check_everything(fixture.m_Ui, min_target));

        fixture.reveal(QStringLiteral("aboutLicenceText"));
        fixture.tap(QStringLiteral("aboutLicenceText"));
        fixture.m_Ui.advance(400ms);
        expect_clean(check_everything(fixture.m_Ui, min_target));
        fixture.go_back();

        const auto rows = fixture.m_Ui.find_items(QStringLiteral("aboutComponent"));
        REQUIRE(rows.size() == 4);
        fixture.reveal(QStringLiteral("aboutComponent"));
        fixture.m_Ui.tap(rows[0]);
        fixture.m_Ui.advance(400ms);
        expect_clean(check_everything(fixture.m_Ui, min_target));
        fixture.go_back();
        fixture.go_back();
    }
}

TEST_CASE("settings.about: rows are named, the version reads as text and the licence text is read by paragraph",
          "[ui][settings_screen][about][a11y]")
{
    Fixture fixture;
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    const auto named = [&fixture](const QString& object_name, std::size_t index = 0)
    {
        std::size_t seen = 0;
        for (const Node& node : fixture.m_Ui.accessible_tree())
        {
            if (node.m_ObjectName == object_name && node.m_Visible && seen++ == index)
            {
                return node;
            }
        }
        return Node{};
    };
    // A row of facts is read as one piece of text, "Version, 0.1.0", and is not a button.
    const Node version = named(QStringLiteral("aboutVersion"));
    CHECK(version.m_Name == "Version, 0.1.0");
    CHECK(version.m_Role == QAccessible::StaticText);
    // The rows that do something say what, and where to.
    const Node licence = named(QStringLiteral("aboutLicenceText"));
    CHECK(licence.m_Name == "Read the licence, GPL-3.0-or-later");
    CHECK(licence.m_Role == QAccessible::Button);
    const Node source = named(QStringLiteral("aboutSourceLink"));
    CHECK(source.m_Name == "Open the source code, https://github.com/iainchesworthlabs/icl-error-journaling");
    CHECK(source.m_Role == QAccessible::Button);
    const Node component = named(QStringLiteral("aboutComponent"), 1);
    CHECK(component.m_Name == "Pixel (in Sample Base) 1.2.3, MIT");
    CHECK(component.m_Role == QAccessible::Button);

    fixture.reveal_and_open(QStringLiteral("aboutLicenceText"));
    int paragraphs = 0;
    for (const Node& node : fixture.m_Ui.accessible_tree())
    {
        if (node.m_ObjectName == "licenceParagraph" && node.m_Visible)
        {
            ++paragraphs;
            CHECK(node.m_Name == node.m_Item->property("text").toString());
            CHECK(node.m_Role == QAccessible::StaticText);
        }
    }
    CHECK(paragraphs == 8);
}

TEST_CASE("settings.about: the licence text is whole at the largest text size and does not run off the screen",
          "[ui][settings_screen][about][a11y]")
{
    Fixture fixture;
    fixture.m_Ui.set_text_scale(1.3);
    fixture.reveal_and_open(QStringLiteral("settingsAboutRow"));
    fixture.reveal_and_open(QStringLiteral("aboutLicenceText"));
    const auto paragraphs = fixture.m_Ui.find_items(QStringLiteral("licenceParagraph"));
    REQUIRE(paragraphs.size() == 8);
    const double width = fixture.m_Ui.geometry(fixture.require(QStringLiteral("settingsFlickable"))).width();
    for (QQuickItem* paragraph : paragraphs)
    {
        CHECK(fixture.m_Ui.geometry(paragraph).right() <= width);
        // Wrapped to the screen, not cut: the text is as tall as it needs.
        CHECK(paragraph->property("contentHeight").toDouble() <= paragraph->height() + 1.0);
    }
    expect_clean(check_everything(fixture.m_Ui, min_target));
}

TEST_CASE("settings.list_rows: the unit prefix and the totals line are on the list, below the facilitator (F4)",
          "[ui][settings_screen]")
{
    Fixture fixture;
    QQuickItem* facilitator = fixture.require(QStringLiteral("settingsFacilitator"));
    QQuickItem* prefix = fixture.require(QStringLiteral("settingsUnitPrefixRow"));
    QQuickItem* totals = fixture.require(QStringLiteral("settingsTotalsRow"));
    CHECK(prefix->property("text").toString() == QStringLiteral("Unit prefix"));
    CHECK(prefix->property("value").toString() == QStringLiteral("UNIT"));
    CHECK(totals->property("text").toString() == QStringLiteral("Include a totals line"));
    CHECK(fixture.m_Ui.geometry(prefix).top() >= fixture.m_Ui.geometry(facilitator).bottom() - 0.5);
    CHECK(fixture.m_Ui.geometry(totals).top() >= fixture.m_Ui.geometry(prefix).bottom() - 0.5);

    // The switch is the setting's own: it saves and moves.
    CHECK_FALSE(fixture.settings().include_totals());
    const std::size_t before = fixture.saves();
    fixture.tap(QStringLiteral("settingsTotalsRow"));
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().include_totals());
    CHECK(totals->property("checked").toBool());

    // The prefix row opens the unit prefix page, with the value in the field.
    fixture.open_page(QStringLiteral("settingsUnitPrefixRow"));
    CHECK(fixture.depth() == 2);
    CHECK(fixture.require(QStringLiteral("formInput"))->property("text").toString() == "UNIT");
    // Back leads to the list it was opened from, and says so.
    CHECK(fixture.require(QStringLiteral("navLeadingLabel"))->property("text").toString() == "Settings");
}

TEST_CASE("settings.environments_row: the list has a Unit environments row with the count, which opens the editor "
          "(F21)",
          "[ui][settings_screen]")
{
    Fixture fixture;
    QSignalSpy environments(fixture.m_Screen, SIGNAL(unitEnvironmentsRequested()));
    QQuickItem* row = fixture.require(QStringLiteral("settingsUnitEnvironments"));
    CHECK(row->property("text").toString() == QStringLiteral("Unit environments"));
    CHECK(row->property("value").toString() == QStringLiteral("2"));
    CHECK(fixture.m_Ui.geometry(row).height() == 44.0);
    // Under Locations, which is the list it is most like.
    CHECK(fixture.m_Ui.geometry(row).top() >=
          fixture.m_Ui.geometry(fixture.require(QStringLiteral("settingsLocations"))).bottom() - 0.5);

    fixture.tap(QStringLiteral("settingsUnitEnvironments"));
    CHECK(environments.count() == 1);
    CHECK(fixture.depth() == 1);
}
