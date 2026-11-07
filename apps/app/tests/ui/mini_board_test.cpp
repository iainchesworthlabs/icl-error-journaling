// SPDX-License-Identifier: GPL-3.0-or-later
// MiniBoard (docs/plan/stage-4.md, 4.5): mini_board.drag, mini_board.swap and mini_board.buttons, against the real
// BoardLayoutModel over the sample configuration. A relay stands for SettingsModel: it moves the tile in the
// configuration and refreshes the model, as moveBoardTile does, or refuses.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort board_test_support.hpp first, so it is
// switched off.
// clang-format off
#include "storage_samples.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/board_layout_model.hpp"

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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;
using viewmodel::BoardLayoutModel;

namespace
{

constexpr int ROWS = 12;
constexpr int OTHER_ROW = 11;
constexpr double WIDTH = 370;

/// The layout model and the configuration it shows; `move` is what `SettingsModel::moveBoardTile` does to them.
class Layout
{
public:
    explicit Layout(Configuration configuration = sample_configuration()) : m_Configuration(std::move(configuration))
    {
        m_Model.refresh(m_Configuration);
    }

    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;

    BoardLayoutModel m_Model;
    Configuration m_Configuration;

    /// Takes the tile at `from` out and puts it at `to`, the tiles between shifting by one (positions 1 to 11).
    void move(int from, int to)
    {
        std::vector<std::optional<std::size_t>> by_position(11);
        for (std::size_t i = 0; i < m_Configuration.m_ErrorTypes.size(); ++i)
        {
            const ErrorType& type = m_Configuration.m_ErrorTypes[i];
            if (!type.m_Retired && type.m_BoardPosition)
            {
                by_position.at(static_cast<std::size_t>(*type.m_BoardPosition - 1)) = i;
            }
        }
        const std::optional<std::size_t> moving = by_position.at(static_cast<std::size_t>(from - 1));
        by_position.erase(by_position.begin() + (from - 1));
        by_position.insert(by_position.begin() + (to - 1), moving);
        for (std::size_t p = 0; p < by_position.size(); ++p)
        {
            if (by_position[p])
            {
                m_Configuration.m_ErrorTypes[*by_position[p]].m_BoardPosition = static_cast<int>(p) + 1;
            }
        }
        m_Model.refresh(m_Configuration);
    }

    [[nodiscard]] QString label(int row) const
    {
        return m_Model.data(m_Model.index(row), Qt::DisplayRole).toString();
    }
};

/// Stands for SettingsModel: moves the tile and returns an empty string, or refuses with a message.
class Relay : public QObject
{
    Q_OBJECT

public:
    explicit Relay(Layout& layout) : m_Layout(layout) {}

    Q_INVOKABLE QString moveBoardTile(int from, int to)
    {
        m_Calls.emplace_back(from, to);
        if (m_Refuse)
        {
            return QStringLiteral("That tile cannot move there.");
        }
        m_Layout.move(from, to);
        return {};
    }

    std::vector<std::pair<int, int>> m_Calls;
    bool m_Refuse = false;

private:
    Layout& m_Layout;
};

QQuickItem* load_board(UiRig& rig, Layout& layout)
{
    QQuickItem* board =
        rig.load(QStringLiteral("MiniBoard"),
                 {{QStringLiteral("model"), QVariant::fromValue(static_cast<QObject*>(&layout.m_Model))}});
    REQUIRE(board != nullptr);
    board->setX(16);
    board->setWidth(WIDTH);
    board->setHeight(board->implicitHeight());
    rig.layout();
    return board;
}

/// The board in a wrapper that connects moveRequested to the relay the way the settings screen connects it to
/// SettingsModel.
QQuickItem* load_wired(UiRig& rig, Layout& layout, Relay& relay)
{
    QQuickItem* wrapper =
        rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            id: wrapper
            property var layoutModel
            property var relay
            property var replaced: []
            MiniBoard {
                objectName: "board"
                x: 16
                width: 370
                model: wrapper.layoutModel
                onMoveRequested: (from, to) => wrapper.relay.moveBoardTile(from, to)
                onReplaceRequested: position => wrapper.replaced = wrapper.replaced.concat([position])
            }
        })"),
                        {{QStringLiteral("layoutModel"), QVariant::fromValue(static_cast<QObject*>(&layout.m_Model))},
                         {QStringLiteral("relay"), QVariant::fromValue(static_cast<QObject*>(&relay))}});
    REQUIRE(wrapper != nullptr);
    QQuickItem* board = rig.find_item(QStringLiteral("board"));
    REQUIRE(board != nullptr);
    board->setHeight(board->implicitHeight());
    rig.layout();
    return board;
}

QQuickItem* tile(UiRig& rig, int row)
{
    QQuickItem* item = rig.find_item(QStringLiteral("miniTile%1").arg(row));
    REQUIRE(item != nullptr);
    return item;
}

QString label_of(UiRig& rig, int row)
{
    return tile(rig, row)->findChild<QQuickItem*>(QStringLiteral("miniLabel"))->property("text").toString();
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

/// Waits out the hold of the finger that is down.
void wait_for_hold(UiRig& rig)
{
    QTest::qWait(ShortHold::HOLD_MS + 80);
    rig.advance(16ms);
}

/// A tap, and the layout that the positioners do before the next frame (the panel under the board appears and moves).
void tap_item(UiRig& rig, QQuickItem* item)
{
    rig.tap(item);
    rig.layout();
}

/// Presses a tile, waits for the hold, and drags to `to_row`, over a few updates; returns the finger, still down.
int hold_and_drag(UiRig& rig, int from_row, int to_row)
{
    const int finger = rig.press(tile(rig, from_row));
    wait_for_hold(rig);
    QQuickItem* target = tile(rig, to_row);
    const QPointF from = target->mapFromScene(tile(rig, from_row)->mapToScene(QPointF(10, 10)));
    const QPointF to(target->width() / 2, target->height() / 2);
    for (int step = 1; step <= 4; ++step)
    {
        rig.move(finger, target, from + (to - from) * (step / 4.0));
        rig.advance(16ms);
    }
    return finger;
}

double min_target(const Node& /*node*/)
{
    return 44.0;
}

} // namespace

TEST_CASE("mini_board.geometry: twelve small tiles in three columns, the size of the design as a share of the board",
          "[ui][mini_board]")
{
    Layout layout;
    UiRig rig;
    QQuickItem* board = load_board(rig, layout);
    REQUIRE(board != nullptr);

    const double gap = 6;
    const double width = (WIDTH - 2 * gap) / 3;
    for (int row = 0; row < ROWS; ++row)
    {
        const QRectF rect = rig.geometry(tile(rig, row));
        INFO("row " << row);
        CHECK(rect.width() == width);
        CHECK(rect.height() > 60);
        CHECK(rect.height() < 66);
        CHECK(rect.x() == 16 + (row % 3) * (width + gap));
        CHECK(rect.width() >= 44);
        CHECK(rect.height() >= 44);
    }
    CHECK(label_of(rig, 0) == QStringLiteral("Wrong Response"));
    CHECK(label_of(rig, 1) == QStringLiteral("Sequence Out of Order"));
    CHECK(label_of(rig, OTHER_ROW) == QStringLiteral("Other"));
    // Nothing is selected, so there is no panel.
    CHECK_FALSE(rig.find_item(QStringLiteral("miniPanel"))->isVisible());
    CHECK(board->property("selectedPosition").toInt() == 0);
}

TEST_CASE("mini_board.swap: a tap selects a tile and Replace type asks for a type for that position",
          "[ui][mini_board]")
{
    Layout layout;
    UiRig rig;
    QQuickItem* board = load_board(rig, layout);
    QSignalSpy replace(board, SIGNAL(replaceRequested(int)));
    QSignalSpy moved(board, SIGNAL(moveRequested(int, int)));

    tap_item(rig, tile(rig, 4));
    CHECK(board->property("selectedPosition").toInt() == 5);
    QQuickItem* panel = rig.find_item(QStringLiteral("miniPanel"));
    REQUIRE(panel->isVisible());
    CHECK(rig.find_item(QStringLiteral("miniSelectedName"))->property("text").toString() == layout.label(4));
    CHECK(rig.find_item(QStringLiteral("miniSelectedPlace"))->property("text").toString() ==
          QStringLiteral("Position 5 of 11"));
    // Selected is shown by more than colour: a tick, and the tile is lifted and larger.
    QQuickItem* face = tile(rig, 4)->findChild<QQuickItem*>(QStringLiteral("miniFace4"));
    CHECK(tile(rig, 4)->findChild<QQuickItem*>(QStringLiteral("miniTick"))->isVisible());
    CHECK(face->scale() > 1.0);
    CHECK(face->y() < 0.0);
    CHECK_FALSE(tile(rig, 5)->findChild<QQuickItem*>(QStringLiteral("miniTick"))->isVisible());
    CHECK(face->parentItem() == tile(rig, 4));

    // Another tile takes the selection.
    tap_item(rig, tile(rig, 8));
    CHECK(board->property("selectedPosition").toInt() == 9);
    CHECK_FALSE(tile(rig, 4)->findChild<QQuickItem*>(QStringLiteral("miniTick"))->isVisible());

    tap_item(rig, rig.find_item(QStringLiteral("miniReplace")));
    REQUIRE(replace.count() == 1);
    CHECK(replace.at(0).at(0).toInt() == 9);
    CHECK(moved.count() == 0);

    // Done puts the board back as it was.
    tap_item(rig, rig.find_item(QStringLiteral("miniDone")));
    CHECK(board->property("selectedPosition").toInt() == 0);
    CHECK_FALSE(panel->isVisible());
}

TEST_CASE("mini_board.swap: Other is fixed, an empty position can be filled", "[ui][mini_board]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.at(5).m_BoardPosition = std::nullopt;
    Layout layout(std::move(configuration));
    UiRig rig;
    QQuickItem* board = load_board(rig, layout);
    QSignalSpy replace(board, SIGNAL(replaceRequested(int)));
    QSignalSpy moved(board, SIGNAL(moveRequested(int, int)));

    // Other is shown and named, and neither selected nor moved.
    tap_item(rig, tile(rig, OTHER_ROW));
    CHECK(board->property("selectedPosition").toInt() == 0);
    CHECK_FALSE(rig.find_item(QStringLiteral("miniPanel"))->isVisible());

    // The empty position (6) is selected, named as empty, cannot move, and can be given a type.
    tap_item(rig, tile(rig, 5));
    CHECK(board->property("selectedPosition").toInt() == 6);
    CHECK(rig.find_item(QStringLiteral("miniSelectedName"))->property("text").toString() ==
          QStringLiteral("Empty tile"));
    QQuickItem* earlier = rig.find_item(QStringLiteral("miniMoveEarlier"));
    QQuickItem* later = rig.find_item(QStringLiteral("miniMoveLater"));
    CHECK_FALSE(earlier->isEnabled());
    CHECK_FALSE(later->isEnabled());
    tap_item(rig, earlier);
    tap_item(rig, later);
    CHECK(moved.count() == 0);
    tap_item(rig, rig.find_item(QStringLiteral("miniReplace")));
    REQUIRE(replace.count() == 1);
    CHECK(replace.at(0).at(0).toInt() == 6);
}

TEST_CASE("mini_board.buttons: Move earlier and Move later do what the drag does, and the selection follows the tile",
          "[ui][mini_board]")
{
    Layout layout;
    Relay relay(layout);
    UiRig rig;
    QQuickItem* board = load_wired(rig, layout, relay);
    const QString third = layout.label(2);
    const QString fourth = layout.label(3);

    tap_item(rig, tile(rig, 2));
    REQUIRE(board->property("selectedPosition").toInt() == 3);

    tap_item(rig, rig.find_item(QStringLiteral("miniMoveLater")));
    REQUIRE(relay.m_Calls.size() == 1);
    CHECK(relay.m_Calls.back() == std::make_pair(3, 4));
    CHECK(label_of(rig, 3) == third);
    CHECK(label_of(rig, 2) == fourth);
    CHECK(board->property("selectedPosition").toInt() == 4);
    CHECK(rig.find_item(QStringLiteral("miniSelectedName"))->property("text").toString() == third);
    CHECK(rig.find_item(QStringLiteral("miniSelectedPlace"))->property("text").toString() ==
          QStringLiteral("Position 4 of 11"));

    tap_item(rig, rig.find_item(QStringLiteral("miniMoveEarlier")));
    REQUIRE(relay.m_Calls.size() == 2);
    CHECK(relay.m_Calls.back() == std::make_pair(4, 3));
    CHECK(label_of(rig, 2) == third);
    CHECK(board->property("selectedPosition").toInt() == 3);

    // VoiceOver reaches the same buttons and they do the same.
    REQUIRE(accessible_press(rig.find_item(QStringLiteral("miniMoveLater"))));
    REQUIRE(relay.m_Calls.size() == 3);
    CHECK(relay.m_Calls.back() == std::make_pair(3, 4));
    CHECK(board->property("selectedPosition").toInt() == 4);
    REQUIRE(accessible_press(tile(rig, 7)));
    CHECK(board->property("selectedPosition").toInt() == 8);

    // A refusal moves nothing and the selection stays where it is.
    relay.m_Refuse = true;
    const QString eighth = layout.label(7);
    tap_item(rig, rig.find_item(QStringLiteral("miniMoveEarlier")));
    REQUIRE(relay.m_Calls.size() == 4);
    CHECK(relay.m_Calls.back() == std::make_pair(8, 7));
    CHECK(layout.label(7) == eighth);
    CHECK(label_of(rig, 7) == eighth);
    CHECK(board->property("selectedPosition").toInt() == 8);
}

TEST_CASE("mini_board.buttons: the first and the last position cannot move past the ends", "[ui][mini_board]")
{
    Layout layout;
    Relay relay(layout);
    UiRig rig;
    QQuickItem* board = load_wired(rig, layout, relay);

    tap_item(rig, tile(rig, 0));
    REQUIRE(board->property("selectedPosition").toInt() == 1);
    CHECK_FALSE(rig.find_item(QStringLiteral("miniMoveEarlier"))->isEnabled());
    CHECK(rig.find_item(QStringLiteral("miniMoveLater"))->isEnabled());
    tap_item(rig, rig.find_item(QStringLiteral("miniMoveEarlier")));
    (void)accessible_press(rig.find_item(QStringLiteral("miniMoveEarlier")));
    CHECK(relay.m_Calls.empty());

    tap_item(rig, tile(rig, 10));
    REQUIRE(board->property("selectedPosition").toInt() == 11);
    CHECK(rig.find_item(QStringLiteral("miniMoveEarlier"))->isEnabled());
    CHECK_FALSE(rig.find_item(QStringLiteral("miniMoveLater"))->isEnabled());
    tap_item(rig, rig.find_item(QStringLiteral("miniMoveLater")));
    CHECK(relay.m_Calls.empty());
}

TEST_CASE("mini_board.drag: hold, then drag from position 3 to 7 asks for the move once, and the tile is at 7 only "
          "after the model says so",
          "[ui][mini_board]")
{
    const ShortHold short_hold;
    Layout layout;
    UiRig rig;
    QQuickItem* board = load_board(rig, layout);
    QSignalSpy moved(board, SIGNAL(moveRequested(int, int)));
    const QString dragged = layout.label(2);
    const QString at_seven = layout.label(6);
    QQuickItem* proxy = rig.find_item(QStringLiteral("miniDragProxy"));
    REQUIRE(proxy != nullptr);
    CHECK_FALSE(board->property("dragging").toBool());

    const int finger = hold_and_drag(rig, 2, 6);
    // While the finger is down the held tile is lifted, under the finger, and nothing has moved.
    CHECK(board->property("dragging").toBool());
    CHECK(proxy->isVisible());
    CHECK(moved.count() == 0);
    CHECK(label_of(rig, 6) == at_seven);
    CHECK(label_of(rig, 2) == dragged);
    const QRectF held = rig.geometry(proxy);
    const QRectF over = rig.geometry(tile(rig, 6));
    // Under the finger, to the whole point the touch is rounded to.
    CHECK(std::abs(held.center().x() - over.center().x()) <= 2.0);
    CHECK(std::abs(held.center().y() - over.center().y()) <= 2.0);

    rig.release(finger);
    REQUIRE(moved.count() == 1);
    CHECK(moved.at(0).at(0).toInt() == 3);
    CHECK(moved.at(0).at(1).toInt() == 7);
    CHECK_FALSE(board->property("dragging").toBool());
    CHECK_FALSE(proxy->isVisible());
    // Nobody has answered yet, so the board is as it was: the tile is not at 7.
    CHECK(label_of(rig, 6) == at_seven);
    CHECK(label_of(rig, 2) == dragged);

    // The model moves it, and now it is.
    layout.move(3, 7);
    rig.layout();
    CHECK(label_of(rig, 6) == dragged);
    CHECK(label_of(rig, 2) != dragged);
    CHECK(moved.count() == 1);
    // The release did not also select the tile it ended on.
    CHECK(board->property("selectedPosition").toInt() == 0);
}

TEST_CASE("mini_board.drag: a refusal leaves the tile where it was", "[ui][mini_board]")
{
    const ShortHold short_hold;
    Layout layout;
    Relay relay(layout);
    relay.m_Refuse = true;
    UiRig rig;
    QQuickItem* board = load_wired(rig, layout, relay);
    const QString dragged = layout.label(2);
    const QString at_seven = layout.label(6);

    const int finger = hold_and_drag(rig, 2, 6);
    rig.release(finger);
    REQUIRE(relay.m_Calls.size() == 1);
    CHECK(relay.m_Calls.back() == std::make_pair(3, 7));
    CHECK_FALSE(board->property("dragging").toBool());
    CHECK_FALSE(rig.find_item(QStringLiteral("miniDragProxy"))->isVisible());
    CHECK(label_of(rig, 2) == dragged);
    CHECK(label_of(rig, 6) == at_seven);
    CHECK(tile(rig, 2)->findChild<QQuickItem*>(QStringLiteral("miniFace2"))->opacity() == 1.0);
}

TEST_CASE("mini_board.drag: the selection follows a selected tile that is dragged", "[ui][mini_board]")
{
    const ShortHold short_hold;
    Layout layout;
    Relay relay(layout);
    UiRig rig;
    QQuickItem* board = load_wired(rig, layout, relay);

    tap_item(rig, tile(rig, 2));
    REQUIRE(board->property("selectedPosition").toInt() == 3);
    const int finger = hold_and_drag(rig, 2, 5);
    rig.release(finger);
    REQUIRE(relay.m_Calls.size() == 1);
    CHECK(relay.m_Calls.back() == std::make_pair(3, 6));
    CHECK(board->property("selectedPosition").toInt() == 6);
}

TEST_CASE(
    "mini_board.drag: a press that moves before the hold is a scroll, a short press is a tap, Other is not dragged",
    "[ui][mini_board]")
{
    const ShortHold short_hold;
    Layout layout;
    UiRig rig;
    QQuickItem* board = load_board(rig, layout);
    QSignalSpy moved(board, SIGNAL(moveRequested(int, int)));

    // Moves at once: no drag, even when the hold time has gone by.
    int finger = rig.press(tile(rig, 2));
    rig.move(finger, tile(rig, 2), QPointF(10, 120));
    rig.move(finger, tile(rig, 2), QPointF(10, 200));
    wait_for_hold(rig);
    CHECK_FALSE(board->property("dragging").toBool());
    rig.release(finger);
    CHECK(moved.count() == 0);

    // A short press and release is a tap and selects.
    tap_item(rig, tile(rig, 3));
    CHECK(board->property("selectedPosition").toInt() == 4);
    CHECK_FALSE(board->property("dragging").toBool());

    // Other cannot be picked up.
    finger = rig.press(tile(rig, OTHER_ROW));
    wait_for_hold(rig);
    CHECK_FALSE(board->property("dragging").toBool());
    rig.release(finger);
    CHECK(moved.count() == 0);

    // Dropped on Other, or off the board, it is not a move.
    finger = hold_and_drag(rig, 2, OTHER_ROW);
    rig.release(finger);
    CHECK(moved.count() == 0);
    finger = rig.press(tile(rig, 1));
    wait_for_hold(rig);
    REQUIRE(board->property("dragging").toBool());
    rig.move(finger, tile(rig, 1), QPointF(-600, -600));
    rig.advance(16ms);
    rig.release(finger);
    CHECK(moved.count() == 0);
    CHECK_FALSE(board->property("dragging").toBool());

    // A hold that ends on its own tile is not a move either.
    finger = rig.press(tile(rig, 1));
    wait_for_hold(rig);
    rig.release(finger);
    CHECK(moved.count() == 0);
}

TEST_CASE("mini_board.a11y: names, targets, reading order and the text scales, in light and dark",
          "[ui][mini_board][a11y]")
{
    Layout layout;
    UiRig rig;
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        QQuickItem* board = load_board(rig, layout);
        INFO((dark ? "dark" : "light"));
        expect_clean(check_everything(rig, min_target));

        // With a tile selected, and with an empty position selected.
        board->setProperty("selectedPosition", 5);
        rig.layout();
        expect_clean(check_everything(rig, min_target));
        board->setProperty("selectedPosition", 0);
    }

    // Every tile has a button role and a name that says what it is; Other is read but is not a button.
    QQuickItem* board = load_board(rig, layout);
    board->setProperty("selectedPosition", 2);
    rig.layout();
    int buttons = 0;
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_ObjectName.startsWith(QStringLiteral("miniTile")) && node.m_InAccessibleTree)
        {
            if (node.m_ObjectName == QStringLiteral("miniTile%1").arg(OTHER_ROW))
            {
                CHECK(node.m_Role == QAccessible::StaticText);
            }
            else
            {
                CHECK(node.m_Role == QAccessible::Button);
                ++buttons;
            }
            CHECK_FALSE(node.m_Name.isEmpty());
        }
    }
    CHECK(buttons == 11);
}

TEST_CASE("mini_board.a11y: an empty position is named, selectable and a button", "[ui][mini_board][a11y]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.at(3).m_BoardPosition = std::nullopt;
    Layout layout(std::move(configuration));
    UiRig rig;
    QQuickItem* board = load_board(rig, layout);
    board->setProperty("selectedPosition", 4);
    rig.layout();
    expect_clean(check_everything(rig, min_target));
    bool found = false;
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_ObjectName == QStringLiteral("miniTile3"))
        {
            found = true;
            CHECK(node.m_Name == QStringLiteral("Empty tile, position 4"));
        }
    }
    CHECK(found);
}

#include "mini_board_test.moc"
