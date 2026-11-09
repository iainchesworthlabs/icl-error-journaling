// SPDX-License-Identifier: GPL-3.0-or-later
// The launch screen (docs/plan/stage-5.md, 5.8). The picture that iOS shows until the first frame is the skeleton of
// the Log screen, LaunchSkeleton.qml, drawn into the PNGs of the asset catalogue by tools/launch_image (its tests,
// launch.image_current and launch.image_compare, say that the PNGs are what the skeleton draws). The cases here say
// that the skeleton is what it claims to be and that it is where the real screen is.
//
// Cases: launch.skeleton, launch.matches_screen, launch.same_in_any_font.
//
// The real screen is the whole shell over the real Stage 3 services (the in-memory store, a fake clock), under the
// insets of an iPhone 17: 59 pt at the top, 34 pt at the bottom, as the visual capture has it, with the header filled
// in and two short template names. One thing in it depends on the font: the header card is at least 64 pt and as tall
// as its words when they are taller, and the font of a test machine is not the phone's. The skeleton's sizes do not
// depend on a font (launch.same_in_any_font), so the case measures how far the real card is from the skeleton's and
// requires everything under it to have moved by exactly that; on the phone's font the difference is within half a point
// and nothing has moved.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort them the other way, so it is switched off.
// clang-format off
#include "support/rig.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/settings_model.hpp"

#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAbstractItemModel>
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QModelIndex>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

constexpr auto MODULE = "ErrorJournalingApp";
constexpr std::size_t TILES = 12;
/// The insets of an iPhone 17 in portrait, which the skeleton takes from the theme.
constexpr int INSET_TOP = 59;
constexpr int INSET_BOTTOM = 34;
/// How far the skeleton may be from the real screen, in points (the plan: half a point).
constexpr double TOLERANCE = 0.5;

/// The part of the bridge the shell and the screens call, with the names of `QmlBridge`.
class FakeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double textScale READ text_scale WRITE set_text_scale NOTIFY textScaleChanged)
    Q_PROPERTY(bool reduceMotion READ reduce_motion CONSTANT)
    Q_PROPERTY(QString timingSummary READ timing_summary CONSTANT)

public:
    [[nodiscard]] double text_scale() const
    {
        return m_TextScale;
    }
    void set_text_scale(double scale)
    {
        m_TextScale = scale;
        emit textScaleChanged();
    }
    [[nodiscard]] bool reduce_motion() const
    {
        return false;
    }
    [[nodiscard]] QString timing_summary() const
    {
        return {};
    }

    Q_INVOKABLE void prepareHaptics() {}
    Q_INVOKABLE void setBoardVisible(bool /*visible*/) {}
    Q_INVOKABLE void setApplicationActive(bool /*active*/) {}
    Q_INVOKABLE void hapticImpact(double /*pressTimestamp*/ = 0) {}
    Q_INVOKABLE void hapticSuccess() {}

signals:
    void textScaleChanged();

private:
    double m_TextScale = 1.0;
};

/// The shell under an item that adds the safe-area insets of the phone, which the offscreen window does not have.
const char* const WRAPPER = R"(
import QtQuick
import ErrorJournalingApp

Item {
    id: wrap

    required property var session
    required property var undo
    required property var board
    required property var picker
    required property var timeline
    required property var history
    required property var day
    required property var settings
    required property var share
    required property var appRoot
    required property var bridge
    required property var fileChooser
    required property string aboutText

    SafeArea.additionalMargins.top: 59
    SafeArea.additionalMargins.bottom: 34

    AppShell {
        anchors.fill: parent
        session: wrap.session
        undo: wrap.undo
        board: wrap.board
        picker: wrap.picker
        timeline: wrap.timeline
        history: wrap.history
        day: wrap.day
        settings: wrap.settings
        share: wrap.share
        appRoot: wrap.appRoot
        bridge: wrap.bridge
        fileChooser: wrap.fileChooser
        aboutText: wrap.aboutText
    }
}
)";

/// The rectangles that the launch image has to get right.
struct Layout
{
    QRectF m_Header;
    QRectF m_Composer;
    QRectF m_LastBar;
    QRectF m_Board;
    QRectF m_TabBar;
    std::vector<QRectF> m_Tiles;
};

[[nodiscard]] QRectF scene_rect(const UiRig& rig, QQuickItem* item)
{
    REQUIRE(item != nullptr);
    return rig.geometry(item);
}

/// The skeleton of the launch screen, alone in the window.
Layout skeleton_layout(UiRig& rig, QQuickItem* skeleton)
{
    Layout layout;
    layout.m_Header = scene_rect(rig, rig.find_item(QStringLiteral("launchHeaderCard"), skeleton));
    layout.m_Composer = scene_rect(rig, rig.find_item(QStringLiteral("launchComposer"), skeleton));
    layout.m_LastBar = scene_rect(rig, rig.find_item(QStringLiteral("launchLastBar"), skeleton));
    layout.m_Board = scene_rect(rig, rig.find_item(QStringLiteral("launchBoard"), skeleton));
    layout.m_TabBar = scene_rect(rig, rig.find_item(QStringLiteral("launchTabBar"), skeleton));
    for (QQuickItem* tile : rig.find_items(QStringLiteral("tileBackground"), skeleton))
    {
        layout.m_Tiles.push_back(scene_rect(rig, tile));
    }
    return layout;
}

[[nodiscard]] bool near(const QRectF& a, const QRectF& b, double shift = 0)
{
    return std::abs(a.x() - b.x()) <= TOLERANCE && std::abs(a.y() + shift - b.y()) <= TOLERANCE &&
           std::abs(a.width() - b.width()) <= TOLERANCE && std::abs(a.height() - b.height()) <= TOLERANCE;
}

[[nodiscard]] std::string text(const QRectF& rect)
{
    return QStringLiteral("(%1, %2, %3 x %4)")
        .arg(rect.x())
        .arg(rect.y())
        .arg(rect.width())
        .arg(rect.height())
        .toStdString();
}

/// Asserts that two rectangles agree within the tolerance, saying what each one is when they do not.
void require_near(const char* what, const QRectF& shape, const QRectF& real, double shift = 0)
{
    INFO(what << ": the skeleton has " << text(shape) << " (moved down by " << shift << "), the screen has "
              << text(real));
    CHECK(near(shape, real, shift));
}

/// The application as the phone has it, with the board loaded and on screen.
struct Phone
{
    Phone() : services(RigStore::MEMORY)
    {
        services.import_sample_configuration();
        ui.on_advance([this](std::chrono::milliseconds by) { services.advance(by); });
        ui.on_settle([this] { services.settle(); });
        QQmlComponent component(&ui.engine(), MODULE, "UiStubFileChooser");
        chooser.reset(component.create());
        REQUIRE(chooser != nullptr);
        fill_in_the_setup();

        auto& root = services.root();
        const auto object = [](QObject& value) { return QVariant::fromValue<QObject*>(&value); };
        const QVariantMap properties{{QStringLiteral("session"), object(*root.session())},
                                     {QStringLiteral("undo"), object(*root.undo())},
                                     {QStringLiteral("board"), object(*root.board())},
                                     {QStringLiteral("picker"), object(*root.picker())},
                                     {QStringLiteral("timeline"), object(*root.timeline())},
                                     {QStringLiteral("history"), object(*root.history())},
                                     {QStringLiteral("day"), object(*root.day())},
                                     {QStringLiteral("settings"), object(*root.settings())},
                                     {QStringLiteral("share"), object(*root.share())},
                                     {QStringLiteral("appRoot"), object(root)},
                                     {QStringLiteral("bridge"), object(bridge)},
                                     {QStringLiteral("fileChooser"), object(*chooser)},
                                     {QStringLiteral("aboutText"), QStringLiteral("0.1.0")}};
        REQUIRE(ui.load_source(QByteArray(WRAPPER), properties) != nullptr);
        INFO(ui.errors().toStdString());
        ui.settle();
        ui.advance(50ms);
        ui.layout();
    }

    Phone(const Phone&) = delete;
    Phone& operator=(const Phone&) = delete;

    /// The state the design draws: all five fields of the header filled in, so that the card has no strip of missing
    /// fields under it, and two short template names, so that the chips share a row as they do in the design (the font
    /// of a test machine is not the phone's, and may not have narrow glyphs).
    void fill_in_the_setup()
    {
        auto& session = services.session();
        REQUIRE(session.correctHeader(QStringLiteral("unit_number"), QStringLiteral("27")));
        auto& locations = *services.root().settings()->locations();
        const QHash<int, QByteArray> names = locations.roleNames();
        QString location;
        for (int row = 0; row < locations.rowCount(); ++row)
        {
            const QModelIndex index = locations.index(row, 0);
            QString name;
            QString id;
            for (auto it = names.begin(); it != names.end(); ++it)
            {
                name = it.value() == "name" ? locations.data(index, it.key()).toString() : name;
                id = it.value() == "locationId" ? locations.data(index, it.key()).toString() : id;
            }
            if (name == QStringLiteral("North Site"))
            {
                location = id;
            }
        }
        REQUIRE_FALSE(location.isEmpty());
        REQUIRE(session.correctHeader(QStringLiteral("location"), location));
        REQUIRE(session.correctHeader(QStringLiteral("software_version"), QStringLiteral("1.4.2")));
        auto& settings = *services.root().settings();
        const auto rename = [&](const char* id, const char* name)
        {
            const QString error = settings.renameTemplate(QString::fromLatin1(id), QString::fromLatin1(name));
            INFO(error.toStdString());
            REQUIRE(error.isEmpty());
            services.settle();
        };
        rename("00000000-0000-4000-8000-0000000000c1", "Song");
        rename("00000000-0000-4000-8000-0000000000c2", "Guest");
        services.settle();
    }

    [[nodiscard]] QQuickItem* need(const QString& name)
    {
        QQuickItem* item = ui.find_item(name);
        INFO(name.toStdString());
        REQUIRE(item != nullptr);
        return item;
    }

    [[nodiscard]] Layout layout()
    {
        Layout real;
        real.m_Header = scene_rect(ui, need(QStringLiteral("logHeaderCard")));
        real.m_Composer = scene_rect(ui, need(QStringLiteral("logComposer")));
        real.m_LastBar = scene_rect(ui, need(QStringLiteral("logLastBar")));
        real.m_Board = scene_rect(ui, need(QStringLiteral("logBoard")));
        real.m_TabBar = scene_rect(ui, need(QStringLiteral("shellTabBar")));
        QQuickItem* screen = need(QStringLiteral("logScreen"));
        for (QQuickItem* tile : ui.find_items(QStringLiteral("tileBackground"), screen))
        {
            real.m_Tiles.push_back(scene_rect(ui, tile));
        }
        return real;
    }

    Rig services;
    FakeBridge bridge;
    UiRig ui;
    std::unique_ptr<QObject> chooser;
};

} // namespace

TEST_CASE("launch.skeleton: the launch screen is shapes only, in the colours of the theme, and no one can reach it",
          "[ui][launch]")
{
    const bool dark = GENERATE(false, true);
    CAPTURE(dark);
    UiRig rig;
    rig.set_dark(dark);
    QQuickItem* skeleton = rig.load(QStringLiteral("LaunchSkeleton"));
    INFO(rig.errors().toStdString());
    REQUIRE(skeleton != nullptr);
    rig.layout();

    // The frame of the design, filled by the skeleton, and the 12 tiles of the board.
    CHECK(rig.geometry(skeleton) == QRectF(0, 0, 402, 874));
    const Layout layout = skeleton_layout(rig, skeleton);
    REQUIRE(layout.m_Tiles.size() == TILES);

    // No text is drawn, no one can tap anything, and a screen reader finds nothing: it is a picture.
    for (const Node& node : rig.tree(skeleton))
    {
        INFO(node.m_ObjectName.toStdString());
        if (node.m_TypeName == QStringLiteral("QQuickText"))
        {
            CHECK_FALSE(node.m_Visible);
        }
        const bool takes_touch = node.m_Visible && node.m_Enabled && node.m_Interactive;
        const bool is_read = node.m_Visible && node.m_InAccessibleTree;
        CHECK_FALSE(takes_touch);
        CHECK_FALSE(is_read);
    }

    // The background is the theme's, light or dark, in every pixel the shapes leave: the corners of the frame, and the
    // gap between two tiles.
    const QColor background =
        rig.find_item(QStringLiteral("launchBackground"), skeleton)->property("color").value<QColor>();
    CHECK(background == QColor(dark ? "#0E141A" : "#F2F4F7"));
    const QImage picture = rig.grab();
    const double scale = static_cast<double>(picture.width()) / 402.0;
    const auto pixel = [&](double x, double y)
    { return QColor(picture.pixel(static_cast<int>(x * scale), static_cast<int>(y * scale))); };
    CHECK(pixel(1, 1) == background);
    const QRectF first = layout.m_Tiles.at(0);
    const QRectF second = layout.m_Tiles.at(1);
    CHECK(pixel((first.right() + second.left()) / 2, first.center().y()) == background);
    // A tile is the neutral swatch, whatever the type: all twelve are one colour.
    const QColor swatch = pixel(first.center().x(), first.center().y());
    CHECK(swatch != background);
    for (const QRectF& tile : layout.m_Tiles)
    {
        CHECK(pixel(tile.center().x(), tile.center().y()) == swatch);
    }
}

TEST_CASE(
    "launch.matches_screen: the tiles, the cards and the tab bar are where the real Log screen has them, within half "
    "a point",
    "[ui][launch]")
{
    const bool dark = GENERATE(false, true);
    CAPTURE(dark);

    // The skeleton, on its own.
    UiRig skeleton_rig;
    skeleton_rig.set_dark(dark);
    QQuickItem* skeleton = skeleton_rig.load(QStringLiteral("LaunchSkeleton"));
    INFO(skeleton_rig.errors().toStdString());
    REQUIRE(skeleton != nullptr);
    skeleton_rig.layout();
    const Layout shapes = skeleton_layout(skeleton_rig, skeleton);
    REQUIRE(shapes.m_Tiles.size() == TILES);

    // The real screen: the shell over the sample setup, with the open log loaded and the board on screen.
    Phone phone;
    phone.ui.set_dark(dark);
    REQUIRE(phone.services.root().session()->state() != viewmodel::LogSession::State::LOADING);
    const Layout real = phone.layout();
    REQUIRE(real.m_Tiles.size() == TILES);

    // The frame is the design's, so the numbers below mean what the plan says they mean.
    CHECK(shapes.m_Header.top() == INSET_TOP);
    CHECK(real.m_Header.top() == INSET_TOP);
    CHECK(real.m_TabBar.bottom() == 874.0);
    CHECK(real.m_TabBar.height() == 49.0 + INSET_BOTTOM);

    // The card's own height is the one thing a font changes; everything under it moves with it.
    const double shift = real.m_Header.height() - shapes.m_Header.height();
    INFO("the real header card is " << shift << " pt taller than the skeleton's");
    CHECK(std::abs(shift) <= 8.0);
    require_near("header card, top and sides",
                 QRectF(shapes.m_Header.topLeft(), QSizeF(shapes.m_Header.width(), real.m_Header.height())),
                 real.m_Header);
    require_near("note composer", shapes.m_Composer, real.m_Composer, shift);
    require_near("Last bar", shapes.m_LastBar, real.m_LastBar, shift);
    require_near("board", shapes.m_Board, real.m_Board, shift);
    require_near("tab bar", shapes.m_TabBar, real.m_TabBar);
    for (std::size_t i = 0; i < TILES; ++i)
    {
        require_near(("tile " + std::to_string(i)).c_str(), shapes.m_Tiles.at(i), real.m_Tiles.at(i), shift);
    }
}

TEST_CASE("launch.same_in_any_font: the skeleton does not change with the font of the machine that draws it",
          "[ui][launch]")
{
    // The launch image is made on a desktop and checked in CI on others, and must come out the same.
    const auto draw = [](const QFont& font)
    {
        const QFont before = QGuiApplication::font();
        QGuiApplication::setFont(font);
        UiRig rig;
        QQuickItem* skeleton = rig.load(QStringLiteral("LaunchSkeleton"));
        INFO(rig.errors().toStdString());
        REQUIRE(skeleton != nullptr);
        rig.layout();
        QImage picture = rig.grab();
        const Layout layout = skeleton_layout(rig, skeleton);
        QGuiApplication::setFont(before);
        return std::pair{picture, layout};
    };

    // The installed families with the shortest and the tallest line at the size of the card's text, and the widest
    // glyphs: the most that this machine's fonts can differ. (A machine with one font has nothing to compare, and the
    // case is trivial.)
    QFont low;
    QFont high;
    QFont wide;
    qreal lowest = 1e9;
    qreal highest = 0;
    qreal widest = 0;
    for (const QString& family : QFontDatabase::families())
    {
        QFont font(family);
        font.setPixelSize(13);
        const QFontMetricsF metrics(font);
        if (metrics.height() < lowest)
        {
            lowest = metrics.height();
            low = font;
        }
        if (metrics.height() > highest)
        {
            highest = metrics.height();
            high = font;
        }
        if (metrics.horizontalAdvance(QStringLiteral("0123456789")) > widest)
        {
            widest = metrics.horizontalAdvance(QStringLiteral("0123456789"));
            wide = font;
        }
    }
    INFO("lines " << lowest << " to " << highest << " px at 13 px");

    const auto [sans_picture, sans_layout] = draw(low);
    for (const QFont& other : {high, wide})
    {
        const auto [picture, layout] = draw(other);
        CAPTURE(other.family().toStdString());
        CHECK(near(layout.m_Header, sans_layout.m_Header));
        CHECK(near(layout.m_Board, sans_layout.m_Board));
        CHECK(near(layout.m_TabBar, sans_layout.m_TabBar));
        CHECK(picture == sans_picture);
    }
}

#include "launch_skeleton_test.moc"
