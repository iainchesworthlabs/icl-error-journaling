// SPDX-License-Identifier: GPL-3.0-or-later
// Banner (docs/plan/stage-4.md, 4.4): banner.tones (contrast for each tone in both schemes), its signals, its
// accessibility and the cross-cutting checks.
#include "container_support.hpp"
#include "rig/color_math.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAccessible>
#include <QQuickItem>
#include <QSignalSpy>

using namespace ej::ui;
using namespace ej::ui::containers;

namespace
{

enum Tone
{
    TONE_INFORMATION = 0,
    TONE_WARNING = 1,
    TONE_ERROR = 2
};

const QString WRITE_FAILED_TITLE = QStringLiteral("Mute Delay wasn't saved");
const QString WRITE_FAILED_MESSAGE = QStringLiteral("Nothing was added to the log. Tap Mute Delay again to log it.");
const QString RECOVERY_TITLE = QStringLiteral("Your log was repaired");
const QString RECOVERY_MESSAGE = QStringLiteral(
    "The app closed unexpectedly. It recovered 1 entry from its journal: 10:20 T| Mute Delay. Nothing was lost.");

QQuickItem* load_banner(UiRig& rig, int tone, const QString& title = WRITE_FAILED_TITLE,
                        const QString& message = WRITE_FAILED_MESSAGE, const QString& action = QString())
{
    QQuickItem* banner = rig.load(QStringLiteral("Banner"), {{QStringLiteral("title"), title},
                                                             {QStringLiteral("message"), message},
                                                             {QStringLiteral("tone"), tone},
                                                             {QStringLiteral("actionText"), action}});
    INFO(rig.errors().toStdString());
    REQUIRE(banner != nullptr);
    return banner;
}

QColor property_colour(QQuickItem* item, const char* name)
{
    return item->property(name).value<QColor>();
}

} // namespace

TEST_CASE("banner.tones: every pair a tone uses passes its contrast ratio, in light and dark", "[ui][banner][contrast]")
{
    UiRig rig;
    QQuickItem* banner =
        load_banner(rig, TONE_INFORMATION, RECOVERY_TITLE, RECOVERY_MESSAGE, QStringLiteral("Check the log in Review"));
    const int tone = GENERATE(TONE_INFORMATION, TONE_WARNING, TONE_ERROR);
    const bool dark = GENERATE(false, true);
    INFO("tone " << tone << ", dark " << dark);
    rig.set_dark(dark);
    banner->setProperty("tone", tone);
    rig.layout();

    const QColor fill = property_colour(banner, "fillColour");
    const QColor accent = property_colour(banner, "accentColour");
    // The text is the primary text colour on the fill, the action is the link colour, and the icon and the outline are
    // the accent colour (a non-text indicator).
    CHECK(contrast_ratio(theme_colour(rig, "textPrimary"), fill) >= 4.5);
    CHECK(contrast_ratio(theme_colour(rig, "primaryText"), fill) >= 4.5);
    CHECK(contrast_ratio(accent, fill) >= 3.0);
    // The close glyph.
    CHECK(contrast_ratio(theme_colour(rig, "textSecondary"), fill) >= 3.0);

    // The fill and the accent are the design's tokens for the tone.
    const char* fill_token = tone == TONE_WARNING ? "bannerWarning"
                             : tone == TONE_ERROR ? "bannerError"
                                                  : "primaryTint";
    const char* accent_token = tone == TONE_WARNING ? "bannerWarningBorder"
                               : tone == TONE_ERROR ? "bannerErrorBorder"
                                                    : "primaryText";
    CHECK(fill == theme_colour(rig, fill_token));
    CHECK(accent == theme_colour(rig, accent_token));
}

TEST_CASE("banner.tones: a tone is an icon and an outline as well as a fill", "[ui][banner]")
{
    UiRig rig;
    QQuickItem* banner = load_banner(rig, TONE_INFORMATION);
    QQuickItem* card = rig.find(QStringLiteral("bannerCard"));
    REQUIRE(card != nullptr);

    // The icon of information differs in shape from that of a warning.
    const QString information_icon = banner->property("iconPath").toString();
    banner->setProperty("tone", TONE_WARNING);
    const QString warning_icon = banner->property("iconPath").toString();
    banner->setProperty("tone", TONE_ERROR);
    CHECK(information_icon != warning_icon);
    CHECK(banner->property("iconPath").toString() == warning_icon);

    // Every tone has the outline of 1.5 pt, and the radius of the toast.
    for (const int tone : {TONE_INFORMATION, TONE_WARNING, TONE_ERROR})
    {
        banner->setProperty("tone", tone);
        rig.layout();
        CHECK(card->property("border").value<QObject*>()->property("width").toDouble() == 1.5);
        CHECK(card->property("radius").toDouble() == 14);
    }
}

TEST_CASE("banner: the geometry follows the design", "[ui][banner]")
{
    UiRig rig;
    load_banner(rig, TONE_ERROR);
    QQuickItem* card = rig.find(QStringLiteral("bannerCard"));
    REQUIRE(card != nullptr);

    SECTION("a write-failed banner: padding, close button of 44 pt, text beside the icon")
    {
        const QRectF rect = rig.geometry(card);
        CHECK(rect.left() == 0);
        CHECK(rect.width() == 402);
        CHECK(rect.height() >= 44);

        const QRectF close = rig.geometry(rig.find(QStringLiteral("bannerClose")));
        CHECK(close.width() == 44);
        CHECK(close.height() == 44);
        CHECK(close.right() == rect.right());
        CHECK(close.top() == rect.top());

        const QRectF title = rig.geometry(rig.find(QStringLiteral("bannerTitle")));
        const QRectF message = rig.geometry(rig.find(QStringLiteral("bannerMessage")));
        CHECK(title.left() == rect.left() + 14 + 24 + 12); // padding, icon, gap
        CHECK(title.top() == rect.top() + 12);
        CHECK(message.top() >= title.bottom());
        CHECK(title.right() <= close.left());
        CHECK(message.right() <= close.left());
    }

    SECTION("with no message and no close button it is a single line")
    {
        QQuickItem* banner = rig.root();
        banner->setProperty("message", QString());
        banner->setProperty("closable", false);
        rig.layout();
        CHECK_FALSE(rig.find(QStringLiteral("bannerMessage"))->isVisible());
        CHECK_FALSE(rig.find(QStringLiteral("bannerClose"))->isVisible());
        CHECK(rig.geometry(card).height() < 70);
        CHECK(rig.geometry(rig.find(QStringLiteral("bannerTitle"))).right() > 402 - 14 - 1);
    }

    SECTION("its height is its implicit height, so a screen can anchor it and let it grow")
    {
        CHECK(rig.root()->property("implicitHeight").toDouble() == rig.geometry(card).height());
    }
}

TEST_CASE("banner: the action and the close button emit once each and do not hide the banner", "[ui][banner]")
{
    UiRig rig;
    QQuickItem* banner =
        load_banner(rig, TONE_WARNING, RECOVERY_TITLE, RECOVERY_MESSAGE, QStringLiteral("Check the log in Review"));
    QSignalSpy action(banner, SIGNAL(actionTriggered()));
    QSignalSpy close(banner, SIGNAL(closeRequested()));

    rig.tap(rig.find(QStringLiteral("bannerAction")));
    CHECK(action.count() == 1);
    CHECK(close.count() == 0);

    rig.tap(rig.find(QStringLiteral("bannerClose")));
    CHECK(close.count() == 1);
    CHECK(action.count() == 1);
    CHECK(banner->isVisible()); // the owner decides

    SECTION("a screen reader's activation")
    {
        for (const char* name : {"bannerAction", "bannerClose"})
        {
            QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(rig.find(QString::fromLatin1(name)));
            REQUIRE(iface != nullptr);
            iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        }
        CHECK(action.count() == 2);
        CHECK(close.count() == 2);
    }

    SECTION("with no action text there is no action, and with closable off there is no close button")
    {
        banner->setProperty("actionText", QString());
        banner->setProperty("closable", false);
        rig.layout();
        const std::vector<Node> reached = rig.accessible_tree();
        CHECK(find_shown(reached, QStringLiteral("bannerAction")) == nullptr);
        CHECK(find_shown(reached, QStringLiteral("bannerClose")) == nullptr);
        rig.tap(rig.find(QStringLiteral("bannerClose")));
        CHECK(close.count() == 1);
    }
}

TEST_CASE("banner: to a screen reader it is an alert named by its title and described by its message", "[ui][banner]")
{
    UiRig rig;
    load_banner(rig, TONE_ERROR, WRITE_FAILED_TITLE, WRITE_FAILED_MESSAGE, QStringLiteral("Try again"));
    const std::vector<Node> nodes = rig.accessible_tree();
    const Node* card = find_node(nodes, QStringLiteral("bannerCard"));
    REQUIRE(card != nullptr);
    CHECK(card->m_Role == QAccessible::AlertMessage);
    CHECK(card->m_Name == WRITE_FAILED_TITLE);
    CHECK(card->m_Description == WRITE_FAILED_MESSAGE);

    const Node* close = find_node(nodes, QStringLiteral("bannerClose"));
    REQUIRE(close != nullptr);
    CHECK(close->m_Role == QAccessible::Button);
    CHECK(close->m_Name == QStringLiteral("Dismiss"));
    const Node* action = find_node(nodes, QStringLiteral("bannerAction"));
    REQUIRE(action != nullptr);
    CHECK(action->m_Name == QStringLiteral("Try again"));

    // The words of the title and the message are in the card's name and description, not read a second time.
    CHECK(find_node(nodes, QStringLiteral("bannerTitle")) == nullptr);
    CHECK(find_node(nodes, QStringLiteral("bannerMessage")) == nullptr);
}

TEST_CASE("banner: passes the cross-cutting checks for each tone, in light and dark", "[ui][banner]")
{
    UiRig rig;
    QQuickItem* banner =
        load_banner(rig, TONE_INFORMATION, RECOVERY_TITLE, RECOVERY_MESSAGE, QStringLiteral("Check the log in Review"));
    const int tone = GENERATE(TONE_INFORMATION, TONE_WARNING, TONE_ERROR);
    const bool dark = GENERATE(false, true);
    INFO("tone " << tone << ", dark " << dark);
    rig.set_dark(dark);
    banner->setProperty("tone", tone);
    rig.layout();
    expect_clean(check_everything(rig));
}

TEST_CASE(
    "banner: the text wraps at the largest text size, and scrolls inside the banner when the screen has no more room",
    "[ui][banner]")
{
    UiRig rig;
    QQuickItem* banner =
        load_banner(rig, TONE_WARNING, RECOVERY_TITLE, RECOVERY_MESSAGE, QStringLiteral("Check the log in Review"));
    const double normal = banner->property("implicitHeight").toDouble();
    rig.set_text_scale(3.1);
    const double large = banner->property("implicitHeight").toDouble();
    CHECK(large > 2 * normal);
    expect_clean(check_text_scale(rig));

    SECTION("a screen that can spare 400 pt gets a banner of at most that, and the close button stays")
    {
        banner->setProperty("maximumHeight", 400);
        rig.layout();
        CHECK(large > 400); // so the limit bites
        const QRectF card = rig.geometry(rig.find(QStringLiteral("bannerCard")));
        CHECK(card.height() == 400);
        CHECK(banner->property("implicitHeight").toDouble() == 400);
        const QRectF close = rig.geometry(rig.find(QStringLiteral("bannerClose")));
        CHECK(close.top() == card.top());
        CHECK(close.width() == 44);
        // The text is clipped to the card, and the card scrolls.
        QQuickItem* flick = nullptr;
        for (const Node& node : rig.tree())
        {
            if (node.m_TypeName == QStringLiteral("QQuickFlickable"))
            {
                flick = node.m_Item;
            }
        }
        REQUIRE(flick != nullptr);
        CHECK(flick->property("interactive").toBool());
        CHECK(rig.geometry(flick).bottom() <= card.bottom());
        expect_clean(check_text_scale(rig));
    }

    SECTION("a banner that fits does not scroll")
    {
        rig.set_text_scale(1.0);
        banner->setProperty("maximumHeight", 400);
        rig.layout();
        for (const Node& node : rig.tree())
        {
            if (node.m_TypeName == QStringLiteral("QQuickFlickable"))
            {
                CHECK_FALSE(node.m_Item->property("interactive").toBool());
            }
        }
    }
}

TEST_CASE("motion.reduce: the banner has nothing that moves", "[ui][banner][motion]")
{
    UiRig rig;
    load_banner(rig, TONE_ERROR);
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}
