// SPDX-License-Identifier: GPL-3.0-or-later
// SettingsScreen (docs/plan/stage-4.md, 4.12): settings.rows, settings.facilitator, settings.prefix,
// settings.time_format, settings.totals, settings.board_layout, settings.board_limit, settings.export,
// settings.import_row, settings.loading, settings.voiceover and the cross-cutting checks, against the real
// SettingsModel and BoardModel over the fakes of Stage 3 and the sample configuration.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort the includes otherwise, so it is switched
// off.
// clang-format off
#include "settings_rig.hpp"
#include "support/fake_sources.hpp"
#include "viewmodel/board_model.hpp"

#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QGuiApplication>
#include <QObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QStyleHints>
#include <QTest>
#include <QVariant>
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
    explicit Fixture(bool loaded = true)
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
        m_Screen =
            m_Ui.load(QStringLiteral("SettingsScreen"),
                      {{QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject*>(&m_Models.m_Settings))}});
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

    /// A tap, and the layout that follows it.
    void tap(const QString& name)
    {
        m_Ui.tap(require(name));
        m_Ui.layout();
    }

    /// Taps the row, and waits for the page to arrive.
    void open_page(const QString& row)
    {
        tap(row);
        advance_frames(m_Ui, 400ms);
    }

    /// Taps the Back of the page that is on top, and waits.
    void go_back()
    {
        tap(QStringLiteral("navLeading"));
        advance_frames(m_Ui, 400ms);
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
        QQuickItem* tile = find_item(m_Ui, QStringLiteral("miniTile%1").arg(row));
        REQUIRE(tile != nullptr);
        return tile->findChild<QQuickItem*>(QStringLiteral("miniLabel"))->property("text").toString();
    }

    [[nodiscard]] QString model_label(int row)
    {
        QAbstractItemModel* layout = m_Models.m_Settings.property("boardLayout").value<QAbstractItemModel*>();
        return layout->data(layout->index(row, 0), Qt::DisplayRole).toString();
    }

    [[nodiscard]] std::size_t saves()
    {
        return m_Models.store().saves().size();
    }

    SettingsRig m_Models;
    UiRig m_Ui;
    QQuickItem* m_Screen = nullptr;
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
    QTest::qWait(ShortHold::HOLD_MS + 80);
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

TEST_CASE("settings.rows: the rows show the values and the counts of the model, and open what they name",
          "[ui][settings_screen]")
{
    Fixture fixture;
    QSignalSpy types(fixture.m_Screen, SIGNAL(errorTypesRequested()));
    QSignalSpy templates(fixture.m_Screen, SIGNAL(templatesRequested()));
    QSignalSpy locations(fixture.m_Screen, SIGNAL(locationsRequested()));

    CHECK(fixture.shows(QStringLiteral("Settings")));
    CHECK(fixture.require(QStringLiteral("settingsErrorTypes"))->property("value").toString() == "11");
    CHECK(fixture.require(QStringLiteral("settingsTemplates"))->property("value").toString() == "2");
    CHECK(fixture.require(QStringLiteral("settingsLocations"))->property("value").toString() == "2");
    CHECK(fixture.require(QStringLiteral("settingsFacilitator"))->property("value").toString() == "Sam Example");
    CHECK(fixture.require(QStringLiteral("settingsTimeFormat"))->property("value").toString() == "24-hour");
    CHECK(fixture.require(QStringLiteral("settingsUnitPrefix"))->property("value").toString() == "UNIT");
    CHECK(fixture.require(QStringLiteral("settingsImportExport"))->property("value").toString().isEmpty());

    // The rows of the three lists leave the editors to the shell.
    fixture.tap(QStringLiteral("settingsErrorTypes"));
    fixture.tap(QStringLiteral("settingsTemplates"));
    fixture.tap(QStringLiteral("settingsLocations"));
    CHECK(types.count() == 1);
    CHECK(templates.count() == 1);
    CHECK(locations.count() == 1);
    CHECK(fixture.depth() == 1);

    // The counts follow the lists: a new error type makes 12.
    CHECK(fixture.settings().addErrorType(QStringLiteral("Echo Delay")).isEmpty());
    fixture.m_Ui.settle();
    CHECK(fixture.require(QStringLiteral("settingsErrorTypes"))->property("value").toString() == "12");
    CHECK(fixture.settings().addLocation(QStringLiteral("East Depot")).isEmpty());
    fixture.m_Ui.settle();
    CHECK(fixture.require(QStringLiteral("settingsLocations"))->property("value").toString() == "3");
}

TEST_CASE("settings.rows: the pages the screen has itself are pushed, and Back returns to the list",
          "[ui][settings_screen]")
{
    Fixture fixture;
    for (const QString& row : {QStringLiteral("settingsFacilitator"), QStringLiteral("settingsUnitPrefix"),
                               QStringLiteral("settingsTimeFormat"), QStringLiteral("settingsImportExport")})
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
    advance_frames(fixture.m_Ui, 400ms);
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
    advance_frames(fixture.m_Ui, 400ms);
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
    fixture.open_page(QStringLiteral("settingsUnitPrefix"));
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
    advance_frames(fixture.m_Ui, 400ms);
    fixture.m_Ui.settle();
    CHECK(fixture.depth() == 1);
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().unit_prefix() == "CELL");
    CHECK(fixture.require(QStringLiteral("settingsUnitPrefix"))->property("value").toString() == "CELL");
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
    QQuickItem* row = fixture.require(QStringLiteral("settingsTotals"));
    CHECK_FALSE(row->property("checked").toBool());
    CHECK_FALSE(fixture.settings().include_totals());

    const std::size_t before = fixture.saves();
    fixture.tap(QStringLiteral("settingsTotals"));
    CHECK_FALSE(row->property("checked").toBool());
    fixture.m_Ui.settle();
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.settings().include_totals());
    CHECK(row->property("checked").toBool());
    // The knob is on the other side, so the state is not shown by colour alone.
    QQuickItem* knob = find_item(row, QStringLiteral("settingsSwitchKnob"));
    QQuickItem* track = find_item(row, QStringLiteral("settingsSwitchTrack"));
    REQUIRE(knob != nullptr);
    CHECK(knob->x() + knob->width() / 2 > track->width() / 2);

    fixture.tap(QStringLiteral("settingsTotals"));
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
    CHECK(fixture.tile_label(OTHER_ROW) == "Other");
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

TEST_CASE("settings.board_layout: Replace type opens the picker, a choice calls assignBoardTile and the picker closes",
          "[ui][settings_screen]")
{
    Fixture fixture;
    // A type that is not on the board.
    REQUIRE(fixture.settings().addErrorType(QStringLiteral("Echo Delay")).isEmpty());
    fixture.m_Ui.settle();
    const std::size_t before = fixture.saves();

    fixture.tap(QStringLiteral("miniTile0"));
    fixture.tap(QStringLiteral("miniReplace"));
    advance_frames(fixture.m_Ui, 500ms);
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
    advance_frames(fixture.m_Ui, 500ms);
    CHECK(fixture.saves() == before + 1);
    CHECK(fixture.tile_label(0) == "Mute Delay");
    CHECK(fixture.tile_label(7) == "Wrong Response");
    CHECK(fixture.shown(QStringLiteral("sheetPanel")) == nullptr);

    // A type that is not on the board takes the tile and sends the tile's type off it.
    fixture.tap(QStringLiteral("miniReplace"));
    advance_frames(fixture.m_Ui, 500ms);
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
    advance_frames(fixture.m_Ui, 500ms);
    CHECK(fixture.saves() == before + 2);
    CHECK(fixture.tile_label(0) == "Echo Delay");
    CHECK(fixture.shown(QStringLiteral("sheetPanel")) == nullptr);
}

TEST_CASE("settings.board_layout: Cancel closes the picker and changes nothing", "[ui][settings_screen]")
{
    Fixture fixture;
    const std::size_t before = fixture.saves();
    fixture.tap(QStringLiteral("miniTile1"));
    fixture.tap(QStringLiteral("miniReplace"));
    advance_frames(fixture.m_Ui, 500ms);
    REQUIRE(fixture.shown(QStringLiteral("sheetPanel")) != nullptr);
    fixture.tap(QStringLiteral("navLeading"));
    advance_frames(fixture.m_Ui, 500ms);
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
    advance_frames(fixture.m_Ui, 500ms);
    fixture.m_Ui.tap(fixture.require(QStringLiteral("settingsPickerType_%1").arg(MUTE_DELAY)));
    advance_frames(fixture.m_Ui, 500ms);
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

    fixture.tap(QStringLiteral("settingsExport"));
    CHECK(exports.count() == 1);
    CHECK(imports.count() == 0);
    // The screen asks and the shell answers: the model tells how it went.
    fixture.settings().exportFinished(true, QStringLiteral("The settings were exported."));
    fixture.m_Ui.layout();
    REQUIRE(fixture.shown(QStringLiteral("settingsExportResult")) != nullptr);
    CHECK(fixture.shows(QStringLiteral("The settings were exported.")));
    fixture.tap(QStringLiteral("bannerClose"));
    CHECK(fixture.shown(QStringLiteral("settingsExportResult")) == nullptr);

    fixture.settings().exportFinished(false, QStringLiteral("There was no room to write the file."));
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
    CHECK(name_of(QStringLiteral("settingsErrorTypes")).m_Name == "Error types, 11");
    CHECK(name_of(QStringLiteral("settingsTimeFormat")).m_Name == "Time format, 24-hour");

    const Node totals = name_of(QStringLiteral("settingsTotals"));
    CHECK(totals.m_Role == QAccessible::CheckBox);
    CHECK(totals.m_Name == "Include a totals line");
    QAccessibleInterface* iface =
        QAccessible::queryAccessibleInterface(fixture.require(QStringLiteral("settingsTotals")));
    REQUIRE(iface != nullptr);
    CHECK_FALSE(iface->state().checked);

    // The press action of VoiceOver does what a tap does.
    CHECK(accessible_press(fixture.require(QStringLiteral("settingsTotals"))));
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
    advance_frames(fixture.m_Ui, 400ms);
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

        for (const QString& row : {QStringLiteral("settingsFacilitator"), QStringLiteral("settingsUnitPrefix"),
                                   QStringLiteral("settingsTimeFormat"), QStringLiteral("settingsImportExport")})
        {
            INFO(row.toStdString());
            fixture.open_page(row);
            expect_clean(check_everything(fixture.m_Ui, min_target));
            fixture.go_back();
        }
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
        advance_frames(rig, 500ms);
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
    advance_frames(fixture.m_Ui, 200ms);
    expect_clean(check_motion_reduced(fixture.m_Ui));
}
