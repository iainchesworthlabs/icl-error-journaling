// SPDX-License-Identifier: GPL-3.0-or-later
// BoardGrid (docs/plan/stage-4.md, 4.5): grid.geometry and grid.model_updates, against the real BoardModel over an open
// log with the sample configuration, so the roles the grid binds to are the ones the model has. The signals of the
// tiles through the grid, the skeleton, the accessibility checks and the scene size are here too.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort board_test_support.hpp first, so it is
// switched off.
// clang-format off
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_sources.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/board_model.hpp"

#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QColor>
#include <QMetaObject>
#include <QQuickItem>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QString>
#include <QVariant>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;
using viewmodel::BoardModel;
using viewmodel::LogChange;

namespace
{

constexpr int ROWS = 12;
constexpr int OTHER_ROW = 11;
constexpr double MARGIN = 16;
constexpr double GRID_WIDTH = 402 - 2 * MARGIN;
constexpr double BOARD_HEIGHT = 440;
// An animator of 120 ms has written its end value back a few frames after its time is up (it starts at the first frame
// after the call, on the render thread).
constexpr std::chrono::milliseconds ANIMATOR_SETTLE = 192ms;

/// An open log, the sample configuration and the board over them.
struct BoardRig
{
    explicit BoardRig(Configuration configuration = sample_configuration()) : m_Board(m_Config, m_Open)
    {
        m_Config.set(std::move(configuration));
        m_Open.set(m_Rig.document.get());
    }

    BoardRig(const BoardRig&) = delete;
    BoardRig& operator=(const BoardRig&) = delete;

    DocumentRig m_Rig;
    FakeConfigurationSource m_Config;
    FakeOpenLogSource m_Open;
    BoardModel m_Board;
    std::uint64_t m_NextEntry = 1;

    [[nodiscard]] QVariant value(int row, const char* role) const
    {
        const int id = m_Board.roleNames().key(role);
        return m_Board.data(m_Board.index(row), id);
    }

    /// Records one error of the type on `row` and lets the commit finish: what a tap does to the model.
    void commit_tap(int row)
    {
        const ErrorTypeId type_id = type_of(row);
        Entry entry;
        entry.m_Id = sample_id<EntryId>(m_NextEntry++);
        entry.m_Captured = Timestamp{sample_time(10), "Australia/Melbourne"};
        entry.m_Body = ErrorEntry{type_id, std::nullopt, value(row, "label").toString().toStdString()};
        int calls = 0;
        REQUIRE(m_Rig.document->add_entry(std::move(entry), LogChange::Origin::USER, m_Rig.count(calls)));
        m_Rig.gateway().run_all();
        REQUIRE(calls == 1);
    }

    /// The master-list type that is on `row` (position `row + 1`).
    [[nodiscard]] ErrorTypeId type_of(int row) const
    {
        for (const ErrorType& type : m_Config.configuration().m_ErrorTypes)
        {
            if (!type.m_Retired && type.m_BoardPosition == row + 1)
            {
                return type.m_Id;
            }
        }
        FAIL("no type on row " << row);
        return {};
    }
};

QQuickItem* load_grid(UiRig& rig, BoardRig& board, bool skeleton = false)
{
    QQuickItem* grid = rig.load(QStringLiteral("BoardGrid"),
                                {{QStringLiteral("model"), QVariant::fromValue(static_cast<QObject*>(&board.m_Board))},
                                 {QStringLiteral("skeleton"), skeleton}});
    REQUIRE(grid != nullptr);
    // The width the Log screen gives it; the rig sizes a loaded item to the window.
    grid->setX(MARGIN);
    grid->setWidth(GRID_WIDTH);
    grid->setHeight(grid->implicitHeight());
    rig.layout();
    return grid;
}

QQuickItem* tile_at(UiRig& rig, int row)
{
    QQuickItem* tile = find_item(rig, QStringLiteral("tile%1").arg(row));
    REQUIRE(tile != nullptr);
    return tile;
}

QObject* theme_group(UiRig& rig, const char* name)
{
    auto* group = rig.theme()->property(name).value<QObject*>();
    REQUIRE(group != nullptr);
    return group;
}

QColor theme_tile_colour(UiRig& rig, int slot)
{
    QColor result;
    REQUIRE(QMetaObject::invokeMethod(rig.theme(), "tileColour", Q_RETURN_ARG(QColor, result), Q_ARG(int, slot)));
    return result;
}

double tile_minimum(const Node& /*node*/)
{
    return 88.0;
}

} // namespace

TEST_CASE("grid.geometry: 118 by 104 pt tiles, 8 pt gaps, 440 pt tall, in every text size, with no scrolling",
          "[ui][grid]")
{
    BoardRig board;
    UiRig rig;
    QQuickItem* grid = load_grid(rig, board);

    CHECK(grid->property("tileWidth").toDouble() == 118);
    CHECK(grid->property("boardHeight").toDouble() == BOARD_HEIGHT);
    CHECK(grid->implicitHeight() == BOARD_HEIGHT);

    for (const double scale : {1.0, 1.3, 3.1})
    {
        for (const bool dark : {false, true})
        {
            rig.set_text_scale(scale);
            rig.set_dark(dark);
            rig.layout();
            INFO("text scale " << scale << (dark ? ", dark" : ", light"));
            for (int row = 0; row < ROWS; ++row)
            {
                const QRectF rect = rig.geometry(tile_at(rig, row));
                const int column = row % 3;
                const int line = row / 3;
                INFO("row " << row);
                CHECK(rect.width() == 118);
                CHECK(rect.height() == 104);
                CHECK(rect.x() == MARGIN + column * (118 + 8));
                CHECK(rect.y() == line * (104 + 8));
            }
            const QRectF last = rig.geometry(tile_at(rig, OTHER_ROW));
            CHECK(last.bottom() == BOARD_HEIGHT);
            CHECK(last.right() == MARGIN + GRID_WIDTH);
        }
    }

    // It never scrolls: there is nothing to flick, and the model has 12 rows for good.
    const std::vector<Node> nodes = rig.tree();
    CHECK(std::ranges::none_of(nodes,
                               [](const Node& node) { return node.m_TypeName.contains(QStringLiteral("Flickable")); }));
    CHECK(std::ranges::none_of(nodes,
                               [](const Node& node) { return node.m_TypeName.contains(QStringLiteral("ListView")); }));
    CHECK(board.m_Board.rowCount() == ROWS);
}

TEST_CASE("grid.binding: each tile shows its row of the model", "[ui][grid]")
{
    BoardRig board;
    UiRig rig;
    load_grid(rig, board);
    QObject* colour = theme_group(rig, "colour");

    for (int row = 0; row < ROWS; ++row)
    {
        QQuickItem* tile = tile_at(rig, row);
        INFO("row " << row);
        CHECK(tile->property("label").toString() == board.value(row, "label").toString());
        CHECK(tile->property("accessibleName").toString() == board.value(row, "accessibleName").toString());
        CHECK(tile->property("colourSlot").toInt() == board.value(row, "colourSlot").toInt());
        CHECK(tile->property("isOther").toBool() == (row == OTHER_ROW));
        CHECK_FALSE(tile->property("hasCount").toBool());
        if (row != OTHER_ROW)
        {
            QQuickItem* background = tile->findChild<QQuickItem*>(QStringLiteral("tileBackground"));
            REQUIRE(background != nullptr);
            CHECK(background->property("color").value<QColor>() ==
                  theme_tile_colour(rig, board.value(row, "colourSlot").toInt()));
        }
    }
    CHECK(tile_at(rig, 0)->property("label").toString() == QStringLiteral("Wrong Response"));
    CHECK(tile_at(rig, OTHER_ROW)->property("label").toString() == QStringLiteral("Other"));
    (void)colour;
}

TEST_CASE("grid.signals: a touch on a tile says which one, at the press, and Other says so", "[ui][grid]")
{
    BoardRig board;
    UiRig rig;
    QQuickItem* grid = load_grid(rig, board);
    QSignalSpy pressed(grid, SIGNAL(tilePressed(int, QString, bool)));
    QSignalSpy tapped(grid, SIGNAL(tileTapped(int, QString, bool)));
    REQUIRE(pressed.isValid());
    REQUIRE(tapped.isValid());

    const int finger = rig.press(tile_at(rig, 7));
    REQUIRE(pressed.count() == 1);
    CHECK(tapped.count() == 0);
    CHECK(pressed.at(0).at(0).toInt() == 7);
    CHECK(pressed.at(0).at(1).toString() == board.value(7, "typeId").toString());
    CHECK(pressed.at(0).at(2).toBool() == false);
    rig.release(finger);
    REQUIRE(tapped.count() == 1);
    CHECK(tapped.at(0).at(0).toInt() == 7);

    rig.tap(tile_at(rig, OTHER_ROW));
    REQUIRE(pressed.count() == 2);
    REQUIRE(tapped.count() == 2);
    CHECK(pressed.at(1).at(0).toInt() == OTHER_ROW);
    CHECK(pressed.at(1).at(1).toString().isEmpty());
    CHECK(pressed.at(1).at(2).toBool() == true);
    CHECK(tapped.at(1).at(2).toBool() == true);

    // VoiceOver reaches the tile and sends the same pair.
    REQUIRE(accessible_press(tile_at(rig, 2)));
    CHECK(pressed.count() == 3);
    CHECK(tapped.count() == 3);
    CHECK(pressed.at(2).at(0).toInt() == 2);
}

TEST_CASE("grid.signals: an empty position is inert", "[ui][grid]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.at(4).m_BoardPosition = std::nullopt;
    BoardRig board(std::move(configuration));
    UiRig rig;
    QQuickItem* grid = load_grid(rig, board);
    REQUIRE(board.value(4, "isEmpty").toBool());

    QSignalSpy pressed(grid, SIGNAL(tilePressed(int, QString, bool)));
    QSignalSpy tapped(grid, SIGNAL(tileTapped(int, QString, bool)));
    rig.tap(tile_at(rig, 4));
    CHECK(pressed.count() == 0);
    CHECK(tapped.count() == 0);

    // It is drawn as an empty slot and VoiceOver does not read it as a button.
    CHECK(tile_at(rig, 4)->findChild<QQuickItem*>(QStringLiteral("tileEmptySlot"))->isVisible());
    const std::vector<Node> nodes = rig.accessible_tree();
    const auto empty = std::ranges::find_if(
        nodes, [](const Node& node)
        { return node.m_Name.startsWith(QStringLiteral("Empty tile")) && node.m_InAccessibleTree; });
    CHECK(empty == nodes.end());

    // The other tiles are not affected, and the check passes with a hole in the board.
    rig.tap(tile_at(rig, 5));
    CHECK(pressed.count() == 1);
    expect_clean(check_everything(rig, tile_minimum));
}

TEST_CASE("grid.model_updates: a change in one row updates one tile, and a press running on another is not interrupted",
          "[ui][grid]")
{
    BoardRig board;
    UiRig rig;
    load_grid(rig, board);
    QSignalSpy changed(&board.m_Board, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&board.m_Board, &QAbstractItemModel::modelReset);

    // A finger is down on row 0 and its press animation is running.
    QQuickItem* held = tile_at(rig, 0);
    auto* pressIn = held->findChild<QObject*>(QStringLiteral("tilePressIn"));
    REQUIRE(pressIn != nullptr);
    const int finger = rig.press(held);
    rig.advance(48ms);
    REQUIRE(pressIn->property("running").toBool());

    // The commit of an entry for row 3 arrives meanwhile.
    board.commit_tap(3);
    rig.settle();
    REQUIRE(changed.count() >= 1);
    CHECK(reset.count() == 0);
    CHECK(changed.count() == 1);
    CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 3);
    CHECK(changed.at(0).at(1).value<QModelIndex>().row() == 3);

    // Only row 3 changed: its badge and name, and nothing else.
    CHECK(tile_at(rig, 3)->property("hasCount").toBool());
    CHECK(tile_at(rig, 3)->property("count").toInt() == 1);
    CHECK(tile_at(rig, 3)->findChild<QQuickItem*>(QStringLiteral("tileBadge"))->isVisible());
    CHECK(tile_at(rig, 3)->findChild<QQuickItem*>(QStringLiteral("tileBadgeText"))->property("text").toString() ==
          QStringLiteral("1"));
    CHECK(tile_at(rig, 3)->property("accessibleName").toString().endsWith(QStringLiteral(", 1 today")));
    for (int row = 0; row < ROWS; ++row)
    {
        if (row != 3)
        {
            INFO("row " << row);
            CHECK_FALSE(tile_at(rig, row)->property("hasCount").toBool());
            CHECK_FALSE(tile_at(rig, row)->findChild<QQuickItem*>(QStringLiteral("tileBadge"))->isVisible());
        }
    }

    // The press on row 0 is still running, and finishes as it would have.
    CHECK(pressIn->property("running").toBool());
    advance_frames(rig, ANIMATOR_SETTLE);
    CHECK(held->findChild<QQuickItem*>(QStringLiteral("tileFace"))->scale() == 0.96);
    rig.release(finger);
    advance_frames(rig, ANIMATOR_SETTLE);
    CHECK(held->findChild<QQuickItem*>(QStringLiteral("tileFace"))->scale() == 1.0);

    // The tile objects are the same ones: nothing was created again.
    CHECK(tile_at(rig, 0) == held);

    // The next commit is one change of one row as well.
    changed.clear();
    board.commit_tap(7);
    rig.settle();
    CHECK(changed.count() == 1);
    CHECK(tile_at(rig, 7)->property("count").toInt() == 1);
}

TEST_CASE("grid.skeleton: twelve neutral shapes with no text, count or type colour", "[ui][grid]")
{
    BoardRig board;
    board.commit_tap(3);
    UiRig rig;
    load_grid(rig, board, true);

    QObject* colour = theme_group(rig, "colour");
    for (int row = 0; row < ROWS; ++row)
    {
        QQuickItem* tile = tile_at(rig, row);
        INFO("row " << row);
        CHECK(tile->width() == 118);
        CHECK(tile->height() == 104);
        CHECK_FALSE(tile->findChild<QQuickItem*>(QStringLiteral("tileLabel"))->isVisible());
        CHECK_FALSE(tile->findChild<QQuickItem*>(QStringLiteral("tileBadge"))->isVisible());
        QQuickItem* background = tile->findChild<QQuickItem*>(QStringLiteral("tileBackground"));
        CHECK(background->isVisible());
        CHECK(background->property("color").value<QColor>() == colour->property("swatch").value<QColor>());
    }
    // Nothing for a screen reader to read, and nothing takes a touch.
    for (const Node& node : rig.accessible_tree())
    {
        CHECK_FALSE(node.m_Name.contains(QStringLiteral("Wrong Response")));
    }
    QSignalSpy pressed(rig.root(), SIGNAL(tilePressed(int, QString, bool)));
    rig.tap(tile_at(rig, 1));
    CHECK(pressed.count() == 0);
}

TEST_CASE("grid.a11y: names, 88 pt targets, reading order and the text scales, in light and dark", "[ui][grid][a11y]")
{
    BoardRig board;
    board.commit_tap(1);
    board.commit_tap(1);
    board.commit_tap(8);
    UiRig rig;
    load_grid(rig, board);
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        INFO((dark ? "dark" : "light"));
        expect_clean(check_everything(rig, tile_minimum));
    }

    // The tiles are read by row, left to right, and each has its own name.
    const QRegularExpression tile_name(QStringLiteral("^tile\\d+$"));
    std::vector<QString> names;
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_ObjectName.contains(tile_name) && node.m_InAccessibleTree && node.m_Role != 0)
        {
            names.push_back(node.m_Name);
        }
    }
    REQUIRE(names.size() == ROWS);
    CHECK(names.front() == QStringLiteral("Wrong Response"));
    CHECK(names[1].endsWith(QStringLiteral(", 2 today")));
    CHECK(names.back() == QStringLiteral("Other"));
}

TEST_CASE("perf.scene: the board is twelve delegates and a bounded number of items", "[ui][perf][grid]")
{
    BoardRig board;
    UiRig rig;
    load_grid(rig, board);
    const SceneSize size = measure_scene(rig);
    INFO("items " << size.m_Items << ", delegates " << size.m_Delegates);
    CHECK(size.m_Delegates == ROWS);
    // About a dozen items to a tile; a Loader or a nested Repeater for each tile would double it.
    CHECK(size.m_Items <= ROWS * 14 + 4);
}
