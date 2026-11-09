// SPDX-License-Identifier: GPL-3.0-or-later
// Keyboard (the one reader of the on-screen keyboard): keyboard.coverage, keyboard.dismiss, keyboard.is_text_input.
// The rectangles are the ones that Qt's iOS plugin reports under `Qt.ExpandedClientAreaHint`: in the coordinates of the
// window, which is the whole screen, docked at its bottom edge (qiosinputcontext.mm, `updateKeyboardState`).
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QVariant>

using namespace ej::ui;

namespace
{

/// The height of the keyboard of an iPhone 17 with the letters, as the screenshot of the phone shows it.
constexpr double KEYBOARD = 301.0;
/// The tab bar: 49 pt and the 34 pt of the home indicator.
constexpr double TAB_BAR = 83.0;

constexpr auto ITEMS = R"(
    import QtQuick
    import ErrorJournalingApp
    Item {
        Item {
            id: full
            objectName: "full"
            width: parent.width
            height: parent.height
            readonly property real covered: Keyboard.coverage(full)
        }
        Item {
            id: above
            objectName: "aboveTabBar"
            width: parent.width
            height: parent.height - 83
            readonly property real covered: Keyboard.coverage(above)
        }
    })";

double covered(UiRig& rig, const char* name)
{
    QQuickItem* item = rig.find(QString::fromLatin1(name));
    REQUIRE(item != nullptr);
    return item->property("covered").toDouble();
}

} // namespace

TEST_CASE("keyboard.coverage: how far the keyboard reaches into an item, wherever the item ends in the window",
          "[ui][keyboard]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArray(ITEMS)) != nullptr);
    const double window_height = rig.window().height();
    REQUIRE(window_height == 874.0);

    SECTION("no keyboard covers nothing")
    {
        CHECK(covered(rig, "full") == 0.0);
        CHECK(covered(rig, "aboveTabBar") == 0.0);
    }

    SECTION("a docked keyboard covers its height of an item that ends at the bottom of the window")
    {
        rig.show_keyboard(KEYBOARD);
        CHECK(covered(rig, "full") == KEYBOARD);
        // An item that ends above the tab bar is covered by the keyboard less the tab bar: that part is not in it.
        CHECK(covered(rig, "aboveTabBar") == KEYBOARD - TAB_BAR);
    }

    SECTION(
        "the start of the transition has the final size, below the window, and the layout does not wait for the end")
    {
        rig.show_keyboard_starting(KEYBOARD);
        CHECK(covered(rig, "full") == KEYBOARD);
        CHECK(covered(rig, "aboveTabBar") == KEYBOARD - TAB_BAR);
        rig.show_keyboard(KEYBOARD);
        CHECK(covered(rig, "full") == KEYBOARD);
    }

    SECTION("it follows a keyboard that changes height, as the number pad and the letters differ")
    {
        rig.show_keyboard(KEYBOARD);
        rig.show_keyboard(KEYBOARD + 45.0);
        CHECK(covered(rig, "full") == KEYBOARD + 45.0);
        rig.show_keyboard(KEYBOARD);
        CHECK(covered(rig, "full") == KEYBOARD);
    }

    SECTION("a keyboard that is not docked to the bottom of the window covers nothing")
    {
        rig.show_keyboard(KEYBOARD);
        rig.keyboard()->setProperty("overrideRectangle", QRectF(20, 120, 300, 260));
        CHECK(covered(rig, "full") == 0.0);
    }

    SECTION("a keyboard that was put away covers nothing")
    {
        rig.show_keyboard(KEYBOARD);
        rig.hide_keyboard();
        CHECK(covered(rig, "full") == 0.0);
        CHECK(covered(rig, "aboveTabBar") == 0.0);
    }
}

TEST_CASE("keyboard.is_text_input: a field that takes text is one, and nothing else is", "[ui][keyboard]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            TextInput { id: input }
            Rectangle { id: plain }
            readonly property bool inputIs: Keyboard.isTextInput(input)
            readonly property bool plainIs: Keyboard.isTextInput(plain)
            readonly property bool nullIs: Keyboard.isTextInput(null)
        })")) != nullptr);
    QQuickItem* root = rig.root();
    CHECK(root->property("inputIs").toBool());
    CHECK_FALSE(root->property("plainIs").toBool());
    CHECK_FALSE(root->property("nullIs").toBool());
}

TEST_CASE("keyboard.dismiss: the field loses the focus and the keyboard goes, unless the field is on the page that is "
          "coming in",
          "[ui][keyboard]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            id: root
            property Item keep: null
            function dismiss() { Keyboard.dismiss(root, keep) }
            Item {
                id: page
                objectName: "page"
                TextInput { objectName: "digits"; inputMethodHints: Qt.ImhDigitsOnly }
            }
            Item { objectName: "elsewhere"; activeFocusOnTab: true }
        })")) != nullptr);
    QQuickItem* root = rig.root();
    QQuickItem* digits = rig.find(QStringLiteral("digits"));
    QQuickItem* page = rig.find(QStringLiteral("page"));
    QQuickItem* elsewhere = rig.find(QStringLiteral("elsewhere"));
    REQUIRE(digits != nullptr);
    REQUIRE(elsewhere != nullptr);

    SECTION("a digits-only field is put away with the keyboard")
    {
        digits->forceActiveFocus();
        rig.show_keyboard(KEYBOARD);
        REQUIRE(rig.focused_input() == digits);
        QMetaObject::invokeMethod(root, "dismiss");
        CHECK(rig.focused_input() == nullptr);
        CHECK_FALSE(digits->hasActiveFocus());
        CHECK(rig.keyboard_hides() == 1);
    }

    SECTION("a keyboard that is up while no text field has the focus is put away too")
    {
        elsewhere->forceActiveFocus();
        rig.show_keyboard(KEYBOARD);
        QMetaObject::invokeMethod(root, "dismiss");
        CHECK(rig.keyboard_hides() == 1);
        CHECK(elsewhere->hasActiveFocus());
    }

    SECTION("with no keyboard and no field there is nothing to do")
    {
        elsewhere->forceActiveFocus();
        QMetaObject::invokeMethod(root, "dismiss");
        CHECK(rig.keyboard_hides() == 0);
    }

    SECTION("a field on the page that is coming in keeps the focus")
    {
        digits->forceActiveFocus();
        rig.show_keyboard(KEYBOARD);
        root->setProperty("keep", QVariant::fromValue(page));
        QMetaObject::invokeMethod(root, "dismiss");
        CHECK(rig.focused_input() == digits);
        CHECK(rig.keyboard_hides() == 0);
    }
}
