// SPDX-License-Identifier: GPL-3.0-or-later
// TabBar (docs/plan/stage-4.md, 4.4): tab_bar.selection, tab_bar.geometry, the skeleton, contrast, Reduce Motion and
// the cross-cutting checks.
#include "container_support.hpp"
#include "rig/color_math.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAccessible>
#include <QByteArray>
#include <QFont>
#include <QImage>
#include <QQuickItem>
#include <QSignalSpy>

using namespace ej::ui;
using namespace ej::ui::containers;

namespace
{

const QStringList TABS = {QStringLiteral("tabLog"), QStringLiteral("tabReview"), QStringLiteral("tabHistory"),
                          QStringLiteral("tabSettings")};

QQuickItem* load_bar(UiRig& rig, double inset = 34, bool skeleton = false)
{
    QQuickItem* bar = rig.load(QStringLiteral("TabBar"),
                               {{QStringLiteral("bottomInset"), inset}, {QStringLiteral("skeleton"), skeleton}});
    INFO(rig.errors().toStdString());
    REQUIRE(bar != nullptr);
    return bar;
}

/// A tab, found through the item tree: the tabs are delegates of a Repeater, which `find` does not always reach.
QQuickItem* tab_item(UiRig& rig, const QString& name)
{
    QQuickItem* item = rig.find_item(name);
    REQUIRE(item != nullptr);
    return item;
}

/// The label of a tab: its `Text`.
QQuickItem* label_of(UiRig& rig, const QString& tab)
{
    for (const Node& node : rig.tree(tab_item(rig, tab)))
    {
        if (node.m_TypeName == QStringLiteral("QQuickText"))
        {
            return node.m_Item;
        }
    }
    return nullptr;
}

QFont font_of(QQuickItem* text)
{
    return text->property("font").value<QFont>();
}

} // namespace

TEST_CASE("tab_bar.geometry: 83 pt including the home indicator inset, with the tabs above the inset", "[ui][tab_bar]")
{
    UiRig rig;
    QQuickItem* root = load_bar(rig);
    QQuickItem* bar = rig.find(QStringLiteral("tabBar"));
    REQUIRE(bar != nullptr);

    SECTION("an iPhone 17: 34 pt")
    {
        CHECK(root->property("implicitHeight").toDouble() == 83);
        const QRectF rect = rig.geometry(bar);
        CHECK(rect.height() == 83);
        CHECK(rect.width() == 402);
        CHECK(rect.left() == 0);
        CHECK(rect.bottom() == 874);
        CHECK(rig.geometry(rig.find(QStringLiteral("tabBarBorder"))).height() == 1);

        double left = 0;
        for (const QString& name : TABS)
        {
            QQuickItem* tab = tab_item(rig, name);
            REQUIRE(tab != nullptr);
            const QRectF tab_rect = rig.geometry(tab);
            CHECK(tab_rect.left() == left);
            CHECK(tab_rect.width() == 402.0 / 4);
            CHECK(tab_rect.height() == 49);
            CHECK(tab_rect.top() == 874 - 83);
            // The tabs stop at the inset, which is where the home indicator starts.
            CHECK(tab_rect.bottom() == 874 - 34);
            left += tab_rect.width();
        }
        expect_clean(check_safe_area(rig, 0, 34));
    }

    SECTION("a phone with a taller inset gets a taller bar and the same tabs")
    {
        root->setProperty("bottomInset", 54);
        rig.layout();
        CHECK(rig.geometry(bar).height() == 103);
        CHECK(rig.geometry(bar).bottom() == 874);
        for (const QString& name : TABS)
        {
            CHECK(rig.geometry(tab_item(rig, name)).height() == 49);
            CHECK(rig.geometry(tab_item(rig, name)).bottom() == 874 - 54);
        }
        expect_clean(check_safe_area(rig, 0, 54));
    }

    SECTION("no inset: 49 pt")
    {
        root->setProperty("bottomInset", 0);
        rig.layout();
        CHECK(rig.geometry(bar).height() == 49);
    }

    SECTION("the labels are not cut at the cap, and stay above the inset")
    {
        for (const double scale : {1.0, 1.3, 3.1})
        {
            rig.set_text_scale(scale);
            for (const QString& name : TABS)
            {
                QQuickItem* label = label_of(rig, name);
                REQUIRE(label != nullptr);
                INFO("scale " << scale << " " << name.toStdString());
                CHECK_FALSE(label->property("truncated").toBool());
                CHECK(rig.geometry(label).bottom() <= 874 - 34 + 0.5);
                CHECK(rig.geometry(label).top() >= rig.geometry(bar).top());
            }
        }
        // At the cap of 1.3 a 10.5 pt label is 14 pt (13.65, rounded); a larger text size does not make it larger.
        rig.set_text_scale(1.3);
        const int at_cap = font_of(label_of(rig, QStringLiteral("tabLog"))).pixelSize();
        rig.set_text_scale(3.1);
        CHECK(font_of(label_of(rig, QStringLiteral("tabLog"))).pixelSize() == at_cap);
    }
}

TEST_CASE("tab_bar.selection: a tap changes the current tab once and emits once", "[ui][tab_bar]")
{
    UiRig rig;
    QQuickItem* root = load_bar(rig);
    QSignalSpy selected(root, SIGNAL(tabSelected(int)));
    QSignalSpy reselected(root, SIGNAL(tabReselected(int)));
    CHECK(root->property("currentIndex").toInt() == 0);

    rig.tap(tab_item(rig, QStringLiteral("tabReview")));
    CHECK(root->property("currentIndex").toInt() == 1);
    REQUIRE(selected.count() == 1);
    CHECK(selected.first().first().toInt() == 1);
    CHECK(reselected.count() == 0);

    SECTION("the tab that is current reports a reselection and nothing else")
    {
        rig.tap(tab_item(rig, QStringLiteral("tabReview")));
        CHECK(root->property("currentIndex").toInt() == 1);
        CHECK(selected.count() == 1);
        CHECK(reselected.count() == 1);
        CHECK(reselected.first().first().toInt() == 1);
    }

    SECTION("each of the four can be chosen")
    {
        rig.tap(tab_item(rig, QStringLiteral("tabSettings")));
        CHECK(root->property("currentIndex").toInt() == 3);
        rig.tap(tab_item(rig, QStringLiteral("tabHistory")));
        CHECK(root->property("currentIndex").toInt() == 2);
        rig.tap(tab_item(rig, QStringLiteral("tabLog")));
        CHECK(root->property("currentIndex").toInt() == 0);
        CHECK(selected.count() == 4);
    }

    SECTION("a screen reader's activation does the same as a tap")
    {
        QQuickItem* history = tab_item(rig, QStringLiteral("tabHistory"));
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(history);
        REQUIRE(iface != nullptr);
        auto* action = iface->actionInterface();
        REQUIRE(action != nullptr);
        action->doAction(QAccessibleActionInterface::pressAction());
        CHECK(root->property("currentIndex").toInt() == 2);
        CHECK(selected.count() == 2);
    }

    SECTION("a drag that ends on a tab does not choose it")
    {
        rig.drag(tab_item(rig, QStringLiteral("tabSettings")), QPointF(50, 20), QPointF(50, -300));
        CHECK(root->property("currentIndex").toInt() == 1);
    }
}

TEST_CASE("tab_bar.selection: the selected cue is not colour only", "[ui][tab_bar]")
{
    UiRig rig;
    QQuickItem* root = load_bar(rig);
    rig.tap(tab_item(rig, QStringLiteral("tabHistory")));
    REQUIRE(root->property("currentIndex").toInt() == 2);

    // 1. A heavier label.
    const QFont selected = font_of(label_of(rig, QStringLiteral("tabHistory")));
    const QFont other = font_of(label_of(rig, QStringLiteral("tabLog")));
    CHECK(selected.weight() > other.weight());

    // 2. No indicator bar: the mock-ups mark the selected tab by colour and weight only (owner decision).
    CHECK(rig.find(QStringLiteral("tabSelectedBar")) == nullptr);
    // The icon is 8 pt under the 1 pt top border, and the label 3 pt under the icon.
    QQuickItem* icon = rig.find(QStringLiteral("tabIcon"), tab_item(rig, QStringLiteral("tabHistory")));
    REQUIRE(icon != nullptr);
    const QRectF history = rig.geometry(tab_item(rig, QStringLiteral("tabHistory")));
    CHECK(rig.geometry(icon).top() == history.top() + 9);
    CHECK(rig.geometry(label_of(rig, QStringLiteral("tabHistory"))).top() == rig.geometry(icon).bottom() + 3);

    // 3. The accessibility state, which is what a screen reader says.
    for (const QString& name : TABS)
    {
        QQuickItem* tab = tab_item(rig, name);
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(tab);
        REQUIRE(iface != nullptr);
        CHECK(iface->state().selected == (name == QStringLiteral("tabHistory")));
        CHECK(iface->role() == QAccessible::PageTab);
    }
}

TEST_CASE("tab_bar: a screen reader reads a tab list of four named tabs, in order", "[ui][tab_bar]")
{
    UiRig rig;
    load_bar(rig);
    const std::vector<Node> nodes = rig.accessible_tree();
    const Node* list = find_node(nodes, QStringLiteral("tabBar"));
    REQUIRE(list != nullptr);
    CHECK(list->m_Role == QAccessible::PageTabList);
    CHECK(list->m_Name == QStringLiteral("Tabs"));

    const QStringList names = {QStringLiteral("Log"), QStringLiteral("Review"), QStringLiteral("History"),
                               QStringLiteral("Settings")};
    for (int i = 0; i < 4; ++i)
    {
        const Node* tab = find_node(nodes, TABS[i]);
        REQUIRE(tab != nullptr);
        CHECK(tab->m_Role == QAccessible::PageTab);
        CHECK(tab->m_Name == names[i]);
        CHECK(tab->m_Description == QStringLiteral("%1 of 4").arg(i + 1));
        CHECK(tab->m_InAccessibleTree);
    }
}

TEST_CASE("tab_bar: passes the cross-cutting checks in light and dark, whichever tab is current", "[ui][tab_bar]")
{
    UiRig rig;
    QQuickItem* root = load_bar(rig);
    const bool dark = GENERATE(false, true);
    const int current = GENERATE(0, 1, 2, 3);
    INFO("dark " << dark << ", current " << current);
    rig.set_dark(dark);
    root->setProperty("currentIndex", current);
    rig.layout();
    expect_clean(check_everything(rig));
    expect_clean(check_safe_area(rig, 0, 34));
}

TEST_CASE("tab_bar.contrast: the selected tab is 3 to 1 and the others 4.5 to 1 on the bar, in both schemes",
          "[ui][tab_bar][contrast]")
{
    UiRig rig;
    load_bar(rig);
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        const QColor bar = theme_colour(rig, "surface");
        INFO("dark " << dark);
        CHECK(contrast_ratio(theme_colour(rig, "primaryText"), bar) >= 3.0);
        CHECK(contrast_ratio(theme_colour(rig, "textSecondary"), bar) >= 4.5);
        CHECK(contrast_ratio(theme_colour(rig, "tabBarBorder"), bar) >= 1.0);
    }
}

TEST_CASE("tab_bar.skeleton: neutral shapes with no text, no input and nothing for a screen reader", "[ui][tab_bar]")
{
    UiRig rig;
    QQuickItem* root = load_bar(rig, 34, true);
    QSignalSpy selected(root, SIGNAL(tabSelected(int)));

    int shapes = 0;
    for (const Node& node : rig.tree())
    {
        if (node.m_TypeName == QStringLiteral("QQuickText"))
        {
            CHECK_FALSE(node.m_Visible);
        }
        CHECK_FALSE((node.m_Interactive && node.m_Visible));
        if ((node.m_ObjectName == QStringLiteral("tabSkeletonIcon") ||
             node.m_ObjectName == QStringLiteral("tabSkeletonLabel")) &&
            node.m_Visible)
        {
            ++shapes;
        }
    }
    CHECK(shapes == 8);

    // The bar has its size, so the first frame does not move when the real one replaces it.
    CHECK(rig.geometry(rig.find(QStringLiteral("tabBar"))).height() == 83);

    rig.tap(tab_item(rig, QStringLiteral("tabSettings")));
    CHECK(selected.count() == 0);
    CHECK(root->property("currentIndex").toInt() == 0);
    for (const Node& node : rig.accessible_tree())
    {
        CHECK(node.m_ObjectName != QStringLiteral("tabSettings"));
    }

    // The real bar and the skeleton have the same geometry.
    const QRectF skeleton_tab = rig.geometry(tab_item(rig, QStringLiteral("tabHistory")));
    root->setProperty("skeleton", false);
    rig.layout();
    CHECK(rig.geometry(tab_item(rig, QStringLiteral("tabHistory"))) == skeleton_tab);
}

TEST_CASE("motion.reduce: the tab bar has nothing that moves", "[ui][tab_bar][motion]")
{
    UiRig rig;
    load_bar(rig);
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
    rig.tap(tab_item(rig, QStringLiteral("tabReview")));
    expect_clean(check_motion_reduced(rig));
}

TEST_CASE("tab_bar.geometry: the inset comes from the SafeFrame of the shell, as on a phone", "[ui][tab_bar]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            SafeFrame {
                id: safe
                objectName: "safe"
                anchors.fill: parent
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
            }
            TabBar {
                objectName: "tabs"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                bottomInset: safe.bottomInset
            }
        })")) != nullptr);
    QQuickItem* tabs = rig.find(QStringLiteral("tabs"));
    REQUIRE(tabs != nullptr);
    CHECK(rig.geometry(tabs).height() == 83);
    CHECK(rig.geometry(tabs).bottom() == 874);

    // A phone with 20 pt more at the bottom.
    rig.find(QStringLiteral("safe"))->setProperty("overrideBottom", 54);
    rig.layout();
    CHECK(rig.geometry(tabs).height() == 103);
    CHECK(rig.geometry(tabs).bottom() == 874);
    expect_clean(check_safe_area(rig, 59, 54));
    expect_clean(check_everything(rig));
}

TEST_CASE("tab_bar.stacking: a page built after the bar draws nothing over it once the bar is painted again",
          "[ui][tab_bar]")
{
    // The shell builds the tabs after Log when they are first chosen, so their nodes join the scene graph after the
    // bar's. The software renderer (these tests, the visual capture) then paints the glyphs of the rows that lie wholly
    // under the bar, such as the handles and arrows of Error types, over the bar and outside the clip of their list,
    // until the bar is painted again; `repaint_over` does that, and the capture calls it before each picture. The
    // phone's renderer does not do it.
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp

        Item {
            Loader {
                objectName: "page"
                width: parent.width
                height: 874 - 83
                active: false

                sourceComponent: Flickable {
                    clip: true
                    contentWidth: width
                    contentHeight: 40 * 56
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        width: parent.width

                        Repeater {
                            model: 40

                            delegate: Item {
                                required property int index

                                width: parent.width
                                height: 56

                                EditorGlyph {
                                    x: 40
                                    anchors.verticalCenter: parent.verticalCenter
                                    kind: "handle"
                                    colour: "#FF0000"
                                }

                                EditorGlyph {
                                    x: 300
                                    anchors.verticalCenter: parent.verticalCenter
                                    kind: "up"
                                    colour: "#FF0000"
                                }

                                Icon {
                                    x: 340
                                    anchors.verticalCenter: parent.verticalCenter
                                    kind: "chevronRight"
                                    colour: "#FF0000"
                                }
                            }
                        }
                    }
                }
            }

            TabBar {
                objectName: "tabs"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                bottomInset: 34
            }
        })")) != nullptr);
    INFO(rig.errors().toStdString());
    rig.settle();
    rig.advance(std::chrono::milliseconds(100));
    rig.find(QStringLiteral("page"))->setProperty("active", true);
    rig.settle();
    rig.advance(std::chrono::milliseconds(100));

    rig.repaint_over(rig.find(QStringLiteral("tabs")));
    const QImage image = rig.grab();
    const double scale = static_cast<double>(image.width()) / 402.0;
    const int top = static_cast<int>((874 - 83) * scale);
    const auto red_pixels = [&image](int from, int to)
    {
        int count = 0;
        for (int y = from; y < to; ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QRgb pixel = image.pixel(x, y);
                count += qRed(pixel) > 200 && qGreen(pixel) < 120 && qBlue(pixel) < 120 ? 1 : 0;
            }
        }
        return count;
    };

    // The rows above the bar do draw their glyphs (the case would pass for nothing otherwise), and none show in the
    // bar.
    CHECK(red_pixels(0, top) > 0);
    CHECK(red_pixels(top, image.height()) == 0);
}

// T-I5-3 (docs/plan/stage-14.md): five tabs on the narrowest window.
TEST_CASE("shell.tab_label_cap: at the largest text scale the five labels do not truncate at 375 pt", "[ui][tab_bar]")
{
    UiRig rig;
    QQuickItem* root = load_bar(rig);
    root->setProperty("interactionsEnabled", true);
    rig.set_window_size(QSize(375, 812));
    rig.layout();
    const QStringList tabs = {QStringLiteral("tabLog"), QStringLiteral("tabInteractions"), QStringLiteral("tabReview"),
                              QStringLiteral("tabHistory"), QStringLiteral("tabSettings")};
    QQuickItem* bar = rig.find(QStringLiteral("tabBar"));
    REQUIRE(bar != nullptr);

    double at_default_pt = 0;
    double at_cap_pt = 0;
    for (const double scale : {0.82, 1.0, 1.3, 2.0, 3.1})
    {
        rig.set_text_scale(scale);
        rig.layout();
        for (const QString& name : tabs)
        {
            QQuickItem* tab = tab_item(rig, name);
            QQuickItem* label = label_of(rig, name);
            REQUIRE(label != nullptr);
            INFO("scale " << scale << " " << name.toStdString());
            CHECK(rig.geometry(tab).width() == 375.0 / 5);
            // One line, none of it cut, and inside its tab.
            CHECK_FALSE(label->property("truncated").toBool());
            CHECK(label->property("lineCount").toInt() == 1);
            CHECK(rig.geometry(label).bottom() <= 812 - 34 + 0.5);
        }
        if (scale == 1.0)
        {
            at_default_pt = font_of(label_of(rig, QStringLiteral("tabInteractions"))).pointSizeF();
        }
        if (scale == 1.3)
        {
            at_cap_pt = font_of(label_of(rig, QStringLiteral("tabInteractions"))).pointSizeF();
        }
    }
    // The label never goes below its size at the default text size (the cap is a floor of the fit, not a second scale).
    // Whether the longest label fits its fifth of 375 pt in the phone's own font is for a device: the font of the test
    // is not it.
    rig.set_text_scale(3.1);
    rig.layout();
    const QQuickItem* longest = label_of(rig, QStringLiteral("tabInteractions"));
    CHECK(font_of(const_cast<QQuickItem*>(longest)).pointSizeF() >= at_default_pt);
    // ... and it is never larger than at the cap of the tab labels (1.3), whatever the text size.
    CHECK(font_of(const_cast<QQuickItem*>(longest)).pointSizeF() <= at_cap_pt + 0.01);
}

TEST_CASE("shell.five_tabs: four tabs keep the labels of v1 and the fifth is Interactions", "[ui][tab_bar]")
{
    UiRig rig;
    QQuickItem* root = load_bar(rig);
    CHECK(label_of(rig, QStringLiteral("tabLog"))->property("text").toString() == QStringLiteral("Log"));
    CHECK(rig.find_item(QStringLiteral("tabInteractions")) == nullptr);
    root->setProperty("interactionsEnabled", true);
    rig.layout();
    CHECK(label_of(rig, QStringLiteral("tabLog"))->property("text").toString() == QStringLiteral("Errors"));
    CHECK(label_of(rig, QStringLiteral("tabInteractions"))->property("text").toString() ==
          QStringLiteral("Interactions"));
    // A tap on the fifth tab selects its place in the bar (the second).
    QSignalSpy selected(root, SIGNAL(tabSelected(int)));
    rig.tap(tab_item(rig, QStringLiteral("tabInteractions")));
    REQUIRE(selected.count() == 1);
    CHECK(selected.at(0).at(0).toInt() == 1);
}
