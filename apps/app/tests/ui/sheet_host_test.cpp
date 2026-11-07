// SPDX-License-Identifier: GPL-3.0-or-later
// SheetHost (docs/plan/stage-4.md, 4.4): the layer that holds the screen and the sheets, and keeps the screen out of
// reach while a sheet is open.
#include "container_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <chrono>

using namespace ej::ui;
using namespace ej::ui::containers;
using namespace std::chrono_literals;

namespace
{

// A screen with a tab bar, and over it a sheet and an alert that opens over the sheet.
constexpr auto HOST = R"(
    import QtQuick
    import ErrorJournalingApp
    SheetHost {
        id: host
        objectName: "host"
        Rectangle {
            anchors.fill: parent
            color: Theme.colour.background
        }
        Rectangle {
            objectName: "button"
            property int taps: 0
            x: 20; y: 100; width: 120; height: 54
            color: Theme.colour.primary
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Log another error"
            TapHandler { onTapped: parent.taps += 1 }
        }
        TabBar {
            objectName: "tabs"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            bottomInset: 34
        }
        sheets: [
            Sheet {
                id: sheet
                objectName: "sheet"
                accessibleName: "Log another error"
                detent: Sheet.Medium
                // A tap on the screen above it would dismiss it; this test taps the screen to see that it does not respond.
                dismissible: false
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
            },
            Alert {
                id: alert
                objectName: "alert"
                title: "Delete this log?"
                confirmText: "Delete log"
                cancelText: "Cancel"
                destructive: true
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
            }
        ]
    })";

} // namespace

TEST_CASE("sheet_host: the screen works until a sheet is open, and again when the last one closes", "[ui][sheet_host]")
{
    UiRig rig;
    QQuickItem* host = rig.load_source(QByteArray(HOST));
    INFO(rig.errors().toStdString());
    REQUIRE(host != nullptr);
    QQuickItem* button = rig.find(QStringLiteral("button"));
    QQuickItem* tabs = rig.find(QStringLiteral("tabs"));
    QQuickItem* sheet = rig.find(QStringLiteral("sheet"));
    QQuickItem* alert = rig.find(QStringLiteral("alert"));
    REQUIRE(button != nullptr);
    REQUIRE(tabs != nullptr);
    REQUIRE(sheet != nullptr);
    REQUIRE(alert != nullptr);

    CHECK_FALSE(host->property("modalOpen").toBool());
    rig.tap(button);
    CHECK(button->property("taps").toInt() == 1);
    CHECK(button->isEnabled());

    QMetaObject::invokeMethod(sheet, "open");
    rig.advance(500ms);
    CHECK(host->property("modalOpen").toBool());
    // Nothing of the screen takes a touch, a key or focus.
    CHECK_FALSE(button->isEnabled());
    CHECK_FALSE(tabs->isEnabled());
    rig.tap(button);
    CHECK(button->property("taps").toInt() == 1);
    rig.tap(rig.find_item(QStringLiteral("tabSettings")));
    CHECK(tabs->property("currentIndex").toInt() == 0);

    SECTION("the sheet covers the tab bar too")
    {
        // The medium sheet's top is at the middle of the window; its panel is over the tab bar's area.
        const QRectF panel = rig.geometry(rig.find(QStringLiteral("sheetPanel")));
        const QRectF bar = rig.geometry(rig.find(QStringLiteral("tabBar")));
        CHECK(panel.bottom() >= bar.bottom());
        CHECK(panel.top() < bar.top());
    }

    SECTION("an alert over the sheet: the layer stays modal until both are closed")
    {
        QMetaObject::invokeMethod(alert, "open");
        rig.advance(500ms);
        CHECK(host->property("modalOpen").toBool());
        rig.tap(rig.find(QStringLiteral("alertCancel")));
        rig.advance(600ms);
        CHECK_FALSE(alert->property("isOpen").toBool());
        CHECK(sheet->property("isOpen").toBool());
        CHECK(host->property("modalOpen").toBool());
        CHECK_FALSE(button->isEnabled());

        QMetaObject::invokeMethod(sheet, "close");
        rig.advance(600ms);
        CHECK_FALSE(host->property("modalOpen").toBool());
        CHECK(button->isEnabled());
        CHECK(tabs->isEnabled());
        rig.tap(button);
        CHECK(button->property("taps").toInt() == 2);
        rig.tap(rig.find_item(QStringLiteral("tabSettings")));
        CHECK(tabs->property("currentIndex").toInt() == 3);
    }

    SECTION("closing the sheet makes the screen work again")
    {
        QMetaObject::invokeMethod(sheet, "close");
        rig.advance(600ms);
        CHECK_FALSE(host->property("modalOpen").toBool());
        rig.tap(button);
        CHECK(button->property("taps").toInt() == 2);
    }

    SECTION("Tab stays in the sheet, because the screen cannot take focus")
    {
        for (int i = 0; i < 6; ++i)
        {
            rig.key(Qt::Key_Tab);
            QQuickItem* focus = rig.window().activeFocusItem();
            REQUIRE(focus != nullptr);
            CHECK_FALSE(button->hasActiveFocus());
            bool inside = false;
            for (QQuickItem* item = focus; item != nullptr; item = item->parentItem())
            {
                inside = inside || item == rig.find(QStringLiteral("sheetPanel"));
            }
            CHECK(inside);
        }
    }
}

TEST_CASE("sheet_host: with no sheet open it is transparent to the checks of the screen under it", "[ui][sheet_host]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArray(HOST)) != nullptr);
    expect_clean(check_everything(rig));
    expect_clean(check_safe_area(rig, 0, 34));
}

TEST_CASE("motion.reduce: the host itself has nothing that moves", "[ui][sheet_host][motion]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArray(HOST)) != nullptr);
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));

    // The sheets in it are as quick as they say: a presentation is over within the time of a fade.
    QQuickItem* sheet = rig.find(QStringLiteral("sheet"));
    QMetaObject::invokeMethod(sheet, "open");
    rig.advance(250ms);
    CHECK(sheet->property("isOpen").toBool());
    expect_clean(check_motion_reduced(rig));
}
