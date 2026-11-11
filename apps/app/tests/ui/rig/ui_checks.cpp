// SPDX-License-Identifier: GPL-3.0-or-later
#include "rig/ui_checks.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>

namespace ej::ui
{

namespace
{

constexpr double EPSILON = 0.5;
constexpr double DEFAULT_TARGET = 44.0;
constexpr int REDUCED_MOTION_MS = 100;
/// `Text.ElideNone`, which is Qt::ElideNone (3): ElideLeft, ElideRight and ElideMiddle are 0, 1 and 2.
constexpr int TEXT_ELIDE_NONE = 3;

QString label(const Node& node)
{
    const QString name = node.m_ObjectName.isEmpty() ? QStringLiteral("(no objectName)") : node.m_ObjectName;
    return QStringLiteral("%1 %2 at (%3, %4) %5 x %6")
        .arg(node.m_TypeName, name)
        .arg(node.m_Rect.x())
        .arg(node.m_Rect.y())
        .arg(node.m_Rect.width())
        .arg(node.m_Rect.height());
}

bool is_class(const QObject* object, const char* name)
{
    for (const QMetaObject* meta = object->metaObject(); meta != nullptr; meta = meta->superClass())
    {
        if (qstrcmp(meta->className(), name) == 0)
        {
            return true;
        }
    }
    return false;
}

bool is_text(const Node& node)
{
    return is_class(node.m_Item, "QQuickText");
}

bool related(const Node& a, const Node& b)
{
    return a.m_Item->isAncestorOf(b.m_Item) || b.m_Item->isAncestorOf(a.m_Item);
}

/// The hit rectangle: the item, widened by the largest margin of its enabled pointer handlers.
QRectF hit_rect(const Node& node)
{
    qreal margin = 0;
    for (const QObject* child : node.m_Item->children())
    {
        if (is_pointer_handler(child) && child->property("enabled").toBool())
        {
            margin = std::max(margin, child->property("margin").toReal());
        }
    }
    return node.m_Rect.adjusted(-margin, -margin, margin, margin);
}

} // namespace

Violations check_names(UiRig& rig)
{
    Violations out;
    const std::vector<Node> nodes = rig.tree();
    std::map<std::pair<const QQuickItem*, QString>, std::vector<const Node*>> by_parent_and_name;
    for (const Node& node : nodes)
    {
        if (!node.m_Interactive || !node.m_Visible)
        {
            continue;
        }
        if (!node.m_InAccessibleTree)
        {
            out.push_back(QStringLiteral("not reached by a screen reader: ") + label(node));
            continue;
        }
        if (node.m_Role == QAccessible::NoRole)
        {
            out.push_back(QStringLiteral("no Accessible.role: ") + label(node));
        }
        if (node.m_Name.trimmed().isEmpty())
        {
            out.push_back(QStringLiteral("no Accessible.name: ") + label(node));
        }
        else
        {
            by_parent_and_name[{node.m_Item->parentItem(), node.m_Name}].push_back(&node);
        }
    }
    for (const auto& [key, group] : by_parent_and_name)
    {
        for (std::size_t i = 0; i < group.size(); ++i)
        {
            for (std::size_t j = i + 1; j < group.size(); ++j)
            {
                if (group[i]->m_Value == group[j]->m_Value)
                {
                    out.push_back(QStringLiteral("two controls named \"%1\" with the same value: %2 and %3")
                                      .arg(key.second, label(*group[i]), label(*group[j])));
                }
            }
        }
    }
    return out;
}

Violations check_targets(UiRig& rig, const std::function<double(const Node&)>& minimum)
{
    Violations out;
    for (const Node& node : rig.tree())
    {
        if (!node.m_Interactive || !node.m_Visible)
        {
            continue;
        }
        const double needed = minimum ? minimum(node) : DEFAULT_TARGET;
        const QRectF hit = hit_rect(node);
        if (hit.width() + EPSILON < needed || hit.height() + EPSILON < needed)
        {
            out.push_back(QStringLiteral("target under %1 pt: %2 (hit area %3 x %4)")
                              .arg(needed)
                              .arg(label(node))
                              .arg(hit.width())
                              .arg(hit.height()));
        }
    }
    return out;
}

Violations check_reading_order(UiRig& rig)
{
    Violations out;
    std::vector<Node> read;
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_Role != QAccessible::NoRole && node.m_Visible)
        {
            read.push_back(node);
        }
    }
    for (std::size_t i = 0; i < read.size(); ++i)
    {
        for (std::size_t j = i + 1; j < read.size(); ++j)
        {
            const Node& first = read[i];
            const Node& later = read[j];
            if (related(first, later))
            {
                continue;
            }
            const double overlap = std::min(first.m_Rect.bottom(), later.m_Rect.bottom()) -
                                   std::max(first.m_Rect.top(), later.m_Rect.top());
            const double smaller = std::min(first.m_Rect.height(), later.m_Rect.height());
            const bool same_row = overlap > smaller / 2;
            if (!same_row && later.m_Rect.bottom() <= first.m_Rect.top() + EPSILON)
            {
                out.push_back(
                    QStringLiteral("read after, but above: %1 is read before %2").arg(label(first), label(later)));
            }
            else if (same_row && later.m_Rect.left() + EPSILON < first.m_Rect.left())
            {
                out.push_back(QStringLiteral("read after, but to the left: %1 is read before %2")
                                  .arg(label(first), label(later)));
            }
        }
    }
    return out;
}

/// Whether the item is in a row that scrolls sideways, such as the template chips at a large text size: what lies past the
/// edge of the window is reached by the scroll, and is not lost.
static bool in_sideways_scroller(const QQuickItem* item)
{
    constexpr int HORIZONTAL_FLICK = 1; // Flickable.HorizontalFlick
    for (const QQuickItem* up = item->parentItem(); up != nullptr; up = up->parentItem())
    {
        if (up->inherits("QQuickFlickable") && up->property("flickableDirection").toInt() == HORIZONTAL_FLICK &&
            up->property("contentWidth").toReal() > up->width() + EPSILON)
        {
            return true;
        }
    }
    return false;
}

Violations check_text_scale(UiRig& rig)
{
    Violations out;
    const double window_width = rig.window().width();
    for (const Node& node : rig.tree())
    {
        if (!is_text(node) || !node.m_Visible)
        {
            continue;
        }
        const QQuickItem* item = node.m_Item;
        const bool elides = item->property("elide").toInt() != TEXT_ELIDE_NONE;
        const bool wraps = item->property("wrapMode").toInt() != 0; // Text.NoWrap is 0
        if (!elides && item->property("truncated").toBool())
        {
            out.push_back(QStringLiteral("text is cut off: ") + label(node));
        }
        if (item->property("contentHeight").toReal() > item->height() + EPSILON)
        {
            out.push_back(QStringLiteral("text is taller than its box: ") + label(node));
        }
        if (!wraps && !elides && item->property("contentWidth").toReal() > item->width() + EPSILON)
        {
            out.push_back(QStringLiteral("text is wider than its box: ") + label(node));
        }
        if ((node.m_Rect.left() < -EPSILON || node.m_Rect.right() > window_width + EPSILON) && !in_sideways_scroller(item))
        {
            out.push_back(QStringLiteral("text leaves the window sideways: ") + label(node));
        }
    }
    return out;
}

Violations check_safe_area(UiRig& rig, double top, double bottom)
{
    Violations out;
    const double height = rig.window().height();
    const double width = rig.window().width();
    for (const Node& node : rig.tree())
    {
        if (!node.m_Visible || !(node.m_Interactive || is_text(node)))
        {
            continue;
        }
        if (node.m_Rect.top() + EPSILON < top)
        {
            out.push_back(QStringLiteral("enters the top inset of %1 pt: ").arg(top) + label(node));
        }
        if (node.m_Rect.bottom() > height - bottom + EPSILON)
        {
            out.push_back(QStringLiteral("enters the bottom inset of %1 pt: ").arg(bottom) + label(node));
        }
        if (node.m_Rect.left() < -EPSILON || node.m_Rect.right() > width + EPSILON)
        {
            out.push_back(QStringLiteral("is clipped by the window: ") + label(node));
        }
    }
    return out;
}

Violations check_motion_reduced(UiRig& rig)
{
    Violations out;
    if (rig.root() == nullptr)
    {
        return out;
    }
    QObjectList all = rig.root()->findChildren<QObject*>();
    all.prepend(rig.root());
    for (const QObject* object : all)
    {
        const QByteArray name = object->metaObject()->className();
        if (!name.contains("Animat") && !name.contains("Transition"))
        {
            continue;
        }
        const QVariant duration = object->property("duration");
        if (duration.isValid() && duration.toInt() > REDUCED_MOTION_MS)
        {
            out.push_back(QStringLiteral("%1 \"%2\" lasts %3 ms with Reduce Motion on")
                              .arg(QString::fromLatin1(name), object->objectName())
                              .arg(duration.toInt()));
        }
        const bool slides =
            name.contains("XAnimator") || name.contains("YAnimator") ||
            (name.contains("NumberAnimation") && (object->property("property").toString() == QStringLiteral("x") ||
                                                  object->property("property").toString() == QStringLiteral("y")));
        const QVariant from = object->property("from");
        const QVariant to = object->property("to");
        if (slides && from.isValid() && to.isValid() && !std::isnan(from.toReal()) && !std::isnan(to.toReal()) &&
            !qFuzzyCompare(from.toReal() + 1.0, to.toReal() + 1.0))
        {
            out.push_back(QStringLiteral("%1 \"%2\" slides with Reduce Motion on")
                              .arg(QString::fromLatin1(name), object->objectName()));
        }
    }
    return out;
}

Violations check_everything(UiRig& rig, const std::function<double(const Node&)>& minimum)
{
    Violations out = check_names(rig);
    const auto add = [&out](const Violations& more, const QString& when)
    {
        for (const QString& line : more)
        {
            out.push_back(when + line);
        }
    };
    add(check_targets(rig, minimum), {});
    add(check_reading_order(rig), {});
    for (const double scale : {1.0, 1.3, 3.1})
    {
        rig.set_text_scale(scale);
        add(check_text_scale(rig), QStringLiteral("at text scale %1: ").arg(scale));
    }
    rig.set_text_scale(1.0);
    return out;
}

QString describe(const Violations& violations)
{
    QString text = QStringLiteral("%1 fault(s)").arg(violations.size());
    for (const QString& line : violations)
    {
        text += QStringLiteral("\n  ") + line;
    }
    return text;
}

void expect_clean(const Violations& violations)
{
    INFO(describe(violations).toStdString());
    CHECK(violations.empty());
}

std::vector<double> time_each(int count, const std::function<void()>& press_once, const std::function<void()>& after)
{
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        press_once();
        const auto end = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        if (after)
        {
            after();
        }
    }
    std::sort(times.begin(), times.end());
    return times;
}

double percentile(const std::vector<double>& sorted, double fraction)
{
    if (sorted.empty())
    {
        return 0;
    }
    const auto index = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size()))) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

double perf_budget_factor()
{
    bool parsed = false;
    const double factor = qEnvironmentVariable("EJ_PERF_BUDGET_FACTOR").toDouble(&parsed);
    return std::max(1.0, parsed ? factor : static_cast<double>(EJ_PERF_BUDGET_FACTOR_DEFAULT));
}

SceneSize measure_scene(UiRig& rig, QQuickItem* from)
{
    SceneSize size;
    std::set<const QQuickItem*> delegates;
    for (const Node& node : rig.tree(from))
    {
        ++size.m_Items;
        QQuickItem* item = node.m_Item;
        // A Repeater makes its delegates as items of its parent; a view makes them in its content item. Ask each for
        // the ones it has made, however the delegate is declared.
        const bool repeater = is_class(item, "QQuickRepeater");
        const bool view = is_class(item, "QQuickItemView");
        if (repeater || view)
        {
            const int count = item->property("count").toInt();
            for (int i = 0; i < count; ++i)
            {
                QQuickItem* delegate = nullptr;
                const bool asked = QMetaObject::invokeMethod(item, repeater ? "itemAt" : "itemAtIndex",
                                                             Q_RETURN_ARG(QQuickItem*, delegate), Q_ARG(int, i));
                if (asked && delegate != nullptr)
                {
                    delegates.insert(delegate);
                }
            }
        }
        // A delegate of a model that declares them as required properties, such as a Tumbler or a path view.
        if (node.m_Item->property("index").isValid() && node.m_Item->property("model").isValid())
        {
            delegates.insert(node.m_Item);
        }
    }
    size.m_Delegates = static_cast<int>(delegates.size());
    return size;
}

std::vector<Geometry> snapshot_geometry(UiRig& rig, QQuickItem* from)
{
    std::vector<Geometry> out;
    std::map<QString, int> seen;
    for (const Node& node : rig.tree(from))
    {
        if (!node.m_Visible)
        {
            continue;
        }
        // The key is the path of class names and sibling positions from the root, which does not change when a label
        // does.
        QString key;
        for (const QQuickItem* item = node.m_Item; item != nullptr && item != (from != nullptr ? from : rig.root());
             item = item->parentItem())
        {
            const auto siblings =
                item->parentItem() != nullptr ? item->parentItem()->childItems() : QList<QQuickItem*>();
            key.prepend(QStringLiteral("/%1:%2")
                            .arg(QString::fromLatin1(item->metaObject()->className()))
                            .arg(siblings.indexOf(item)));
        }
        out.push_back({key, node.m_Rect});
    }
    return out;
}

std::vector<QString> moved_items(const std::vector<Geometry>& before, const std::vector<Geometry>& after)
{
    std::map<QString, QRectF> first;
    std::map<QString, QRectF> second;
    for (const Geometry& entry : before)
    {
        first[entry.m_Key] = entry.m_Rect;
    }
    for (const Geometry& entry : after)
    {
        second[entry.m_Key] = entry.m_Rect;
    }
    std::vector<QString> moved;
    for (const auto& [key, rect] : first)
    {
        const auto found = second.find(key);
        if (found == second.end() || found->second != rect)
        {
            moved.push_back(key);
        }
    }
    for (const auto& [key, rect] : second)
    {
        if (!first.contains(key))
        {
            moved.push_back(key);
        }
    }
    return moved;
}

} // namespace ej::ui
