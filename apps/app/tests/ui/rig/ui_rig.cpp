// SPDX-License-Identifier: GPL-3.0-or-later
#include "rig/ui_rig.hpp"

#include "rig/manual_animation_driver.hpp"
#include "rig/stage3_hooks.hpp"

#include <QAccessible>
#include <QCoreApplication>
#include <QImage>
#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QtGlobal>
#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace ej::ui
{

namespace
{

constexpr int FRAME_MS = 16;
constexpr auto MODULE = "ErrorJournalingApp";

bool inherits_class(const QObject* object, const char* name)
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

} // namespace

bool is_pointer_handler(const QObject* object)
{
    return inherits_class(object, "QQuickPointerHandler");
}

struct UiRig::Impl
{
    // The animation clock first: it must be installed before any animation starts and outlive them all.
    ManualAnimationDriver m_Driver;
    QQmlEngine m_Engine;
    std::unique_ptr<QQuickWindow> m_Window = std::make_unique<QQuickWindow>();
    QQuickItem* m_Root = nullptr;
    QString m_Errors;
    test::TestClock* m_Clock = nullptr;
    test::ManualStorageGateway* m_Gateway = nullptr;
    std::function<void()> m_Settle;
    std::function<void(std::chrono::milliseconds)> m_Advance;
    QPointingDevice* m_Device = nullptr;
    std::map<int, QPoint> m_Down; ///< finger id to its last position in the window
    int m_NextFinger = 1;
};

UiRig::UiRig(QSize window_size) : m_Impl(std::make_unique<Impl>())
{
    m_Impl->m_Window->resize(window_size);
    m_Impl->m_Window->setColor(Qt::white);
    m_Impl->m_Window->show();
    m_Impl->m_Device = QTest::createTouchDevice();
}

UiRig::~UiRig()
{
    // The item was made by the engine's component; delete it before the window and the engine go.
    delete m_Impl->m_Root;
    m_Impl->m_Root = nullptr;
}

QQmlEngine& UiRig::engine()
{
    return m_Impl->m_Engine;
}

QQuickWindow& UiRig::window()
{
    return *m_Impl->m_Window;
}

QQuickItem* UiRig::root() const
{
    return m_Impl->m_Root;
}

const QString& UiRig::errors() const
{
    return m_Impl->m_Errors;
}

QQuickItem* UiRig::content() const
{
    return m_Impl->m_Window->contentItem();
}

QObject* UiRig::theme()
{
    return m_Impl->m_Engine.singletonInstance<QObject*>(MODULE, "Theme");
}

void UiRig::set_dark(bool dark)
{
    QObject* t = theme();
    t->setProperty("colourScheme",
                   dark ? static_cast<int>(Qt::ColorScheme::Dark) : static_cast<int>(Qt::ColorScheme::Light));
    layout();
}

void UiRig::set_text_scale(double scale)
{
    theme()->setProperty("textScale", scale);
    layout();
}

void UiRig::set_reduce_motion(bool reduce)
{
    theme()->setProperty("reduceMotion", reduce);
    layout();
}

QQuickItem* UiRig::adopt(QObject* created)
{
    auto* item = qobject_cast<QQuickItem*>(created);
    if (item == nullptr)
    {
        m_Impl->m_Errors += QStringLiteral("the root is not an Item\n");
        delete created;
        return nullptr;
    }
    delete m_Impl->m_Root;
    m_Impl->m_Root = item;
    item->setParentItem(m_Impl->m_Window->contentItem());
    item->setSize(QSizeF(m_Impl->m_Window->size()));
    layout();
    return item;
}

QQuickItem* UiRig::load(const QString& type, const QVariantMap& properties)
{
    m_Impl->m_Errors.clear();
    QQmlComponent component(&m_Impl->m_Engine, MODULE, type);
    if (component.isError())
    {
        for (const auto& error : component.errors())
        {
            m_Impl->m_Errors += error.toString() + QLatin1Char('\n');
        }
        return nullptr;
    }
    QObject* created = component.createWithInitialProperties(properties);
    if (created == nullptr)
    {
        for (const auto& error : component.errors())
        {
            m_Impl->m_Errors += error.toString() + QLatin1Char('\n');
        }
        return nullptr;
    }
    return adopt(created);
}

QQuickItem* UiRig::load_source(const QByteArray& source, const QVariantMap& properties)
{
    m_Impl->m_Errors.clear();
    QQmlComponent component(&m_Impl->m_Engine);
    component.setData(source, QUrl(QStringLiteral("qrc:/ej-ui-rig/inline.qml")));
    if (component.isError())
    {
        for (const auto& error : component.errors())
        {
            m_Impl->m_Errors += error.toString() + QLatin1Char('\n');
        }
        return nullptr;
    }
    QObject* created = component.createWithInitialProperties(properties);
    if (created == nullptr)
    {
        return nullptr;
    }
    return adopt(created);
}

// ---- touch and keys ----

int UiRig::press(QQuickItem* item, std::optional<QPointF> at)
{
    const QPointF local = at.value_or(QPointF(item->width() / 2, item->height() / 2));
    const QPoint point = item->mapToScene(local).toPoint();
    const int id = m_Impl->m_NextFinger++;
    auto sequence = QTest::touchEvent(m_Impl->m_Window.get(), m_Impl->m_Device, false);
    for (const auto& down : m_Impl->m_Down)
    {
        sequence.stationary(down.first);
    }
    sequence.press(id, point, m_Impl->m_Window.get()).commit();
    m_Impl->m_Down[id] = point;
    QCoreApplication::processEvents();
    return id;
}

void UiRig::move(int finger, QQuickItem* item, QPointF to)
{
    const QPoint point = item->mapToScene(to).toPoint();
    auto sequence = QTest::touchEvent(m_Impl->m_Window.get(), m_Impl->m_Device, false);
    for (const auto& down : m_Impl->m_Down)
    {
        if (down.first != finger)
        {
            sequence.stationary(down.first);
        }
    }
    sequence.move(finger, point, m_Impl->m_Window.get()).commit();
    m_Impl->m_Down[finger] = point;
    QCoreApplication::processEvents();
}

void UiRig::release(int finger)
{
    const auto found = m_Impl->m_Down.find(finger);
    if (found == m_Impl->m_Down.end())
    {
        return;
    }
    const QPoint point = found->second;
    m_Impl->m_Down.erase(found);
    auto sequence = QTest::touchEvent(m_Impl->m_Window.get(), m_Impl->m_Device, false);
    for (const auto& down : m_Impl->m_Down)
    {
        sequence.stationary(down.first);
    }
    sequence.release(finger, point, m_Impl->m_Window.get()).commit();
    QCoreApplication::processEvents();
}

void UiRig::tap(QQuickItem* item, std::optional<QPointF> at)
{
    release(press(item, at));
}

void UiRig::drag(QQuickItem* item, QPointF from, QPointF to, int steps)
{
    const int finger = press(item, from);
    for (int step = 1; step <= steps; ++step)
    {
        const qreal t = static_cast<qreal>(step) / steps;
        move(finger, item, from + (to - from) * t);
    }
    release(finger);
}

void UiRig::type(const QString& text)
{
    for (const QChar character : text)
    {
        QTest::sendKeyEvent(QTest::Click, m_Impl->m_Window.get(), Qt::Key_unknown, QString(character), Qt::NoModifier);
    }
    QCoreApplication::processEvents();
}

void UiRig::key(Qt::Key key)
{
    QTest::keyClick(m_Impl->m_Window.get(), key);
    QCoreApplication::processEvents();
}

// ---- time ----

void UiRig::advance(std::chrono::milliseconds by)
{
    auto left = by.count();
    while (left > 0)
    {
        const auto step = std::min<long long>(left, FRAME_MS);
        m_Impl->m_Driver.step(step);
        if (m_Impl->m_Advance)
        {
            m_Impl->m_Advance(std::chrono::milliseconds(step));
        }
        else if (m_Impl->m_Clock != nullptr)
        {
            advance_clock(*m_Impl->m_Clock, std::chrono::milliseconds(step));
        }
        QCoreApplication::processEvents();
        // A frame for each step: the animators and the compressed touch events are handled when a frame is rendered.
        layout();
        left -= step;
    }
}

void UiRig::wait_real(std::chrono::milliseconds by)
{
    QTest::qWait(static_cast<int>(by.count()));
    layout();
}

void UiRig::settle()
{
    if (m_Impl->m_Gateway != nullptr)
    {
        run_gateway(*m_Impl->m_Gateway);
    }
    if (m_Impl->m_Settle)
    {
        m_Impl->m_Settle();
    }
    QCoreApplication::processEvents();
}

void UiRig::on_advance(std::function<void(std::chrono::milliseconds)> hook)
{
    m_Impl->m_Advance = std::move(hook);
}

void UiRig::on_settle(std::function<void()> hook)
{
    m_Impl->m_Settle = std::move(hook);
}

void UiRig::attach(test::TestClock& clock)
{
    m_Impl->m_Clock = &clock;
}

void UiRig::attach(test::ManualStorageGateway& gateway)
{
    m_Impl->m_Gateway = &gateway;
}

// ---- looking at the result ----

Node UiRig::describe(QQuickItem* item, int depth) const
{
    Node node;
    node.m_Item = item;
    node.m_Depth = depth;
    node.m_TypeName = QString::fromLatin1(item->metaObject()->className());
    node.m_ObjectName = item->objectName();
    node.m_Rect = QRectF(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
    node.m_Enabled = item->isEnabled();
    node.m_Visible = item->isVisible() && item->opacity() > 0 && item->width() > 0 && item->height() > 0;

    // What takes a press: a mouse area, a text input, a control, or an item with a pointer handler. (Not every item
    // that accepts mouse buttons does: a Text takes them for its links, and a Flickable for its drags.)
    bool takes_press = inherits_class(item, "QQuickMouseArea") || inherits_class(item, "QQuickTextInput") ||
                       inherits_class(item, "QQuickTextEdit") || inherits_class(item, "QQuickControl");
    for (const QObject* child : item->children())
    {
        if (is_pointer_handler(child) && child->property("enabled").toBool())
        {
            takes_press = true;
        }
    }
    // A StackView is a Control but a container: its pages are the controls, and it is named (Accessible.name) like any
    // region, not pressed.
    node.m_Interactive = takes_press && !inherits_class(item, "QQuickStackView");

    if (QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item))
    {
        node.m_Role = static_cast<int>(iface->role());
        node.m_Name = iface->text(QAccessible::Name);
        node.m_Description = iface->text(QAccessible::Description);
        node.m_Value = iface->text(QAccessible::Value);
    }
    return node;
}

void UiRig::walk(QQuickItem* item, int depth, std::vector<Node>& out) const
{
    out.push_back(describe(item, depth));
    for (QQuickItem* child : item->childItems())
    {
        walk(child, depth + 1, out);
    }
}

std::vector<Node> UiRig::tree(QQuickItem* from)
{
    std::vector<Node> out;
    QQuickItem* start = from != nullptr ? from : m_Impl->m_Root;
    if (start == nullptr)
    {
        return out;
    }
    walk(start, 0, out);

    // Which items the accessibility tree reaches.
    const std::vector<Node> reached = accessible_tree(start);
    std::set<const QQuickItem*> in_tree;
    for (const Node& node : reached)
    {
        in_tree.insert(node.m_Item);
    }
    for (Node& node : out)
    {
        node.m_InAccessibleTree = in_tree.contains(node.m_Item);
    }
    return out;
}

std::vector<Node> UiRig::accessible_tree(QQuickItem* from)
{
    std::vector<Node> out;
    QQuickItem* start = from != nullptr ? from : m_Impl->m_Root;
    if (start == nullptr)
    {
        return out;
    }
    // Depth first through the interface tree, which is the order a screen reader reads.
    std::function<void(QAccessibleInterface*, int)> visit = [&](QAccessibleInterface* iface, int depth)
    {
        if (iface == nullptr)
        {
            return;
        }
        if (auto* item = qobject_cast<QQuickItem*>(iface->object()))
        {
            if (item == start || start->isAncestorOf(item))
            {
                Node node = describe(item, depth);
                node.m_InAccessibleTree = true;
                out.push_back(node);
            }
        }
        for (int i = 0; i < iface->childCount(); ++i)
        {
            visit(iface->child(i), depth + 1);
        }
    };
    visit(QAccessible::queryAccessibleInterface(m_Impl->m_Window.get()), 0);
    return out;
}

QRectF UiRig::geometry(QQuickItem* item) const
{
    return QRectF(item->mapToScene(QPointF(0, 0)), QSizeF(item->width(), item->height()));
}

QQuickItem* UiRig::find(const QString& name, QQuickItem* from) const
{
    QQuickItem* start = from != nullptr ? from : m_Impl->m_Root;
    if (start == nullptr)
    {
        return nullptr;
    }
    if (start->objectName() == name)
    {
        return start;
    }
    return start->findChild<QQuickItem*>(name);
}

QQuickItem* UiRig::find_item(const QString& name, QQuickItem* from) const
{
    QQuickItem* start = from != nullptr ? from : m_Impl->m_Root;
    if (start == nullptr)
    {
        return nullptr;
    }
    if (start->objectName() == name)
    {
        return start;
    }
    for (QQuickItem* child : start->childItems())
    {
        if (QQuickItem* found = find_item(name, child))
        {
            return found;
        }
    }
    return nullptr;
}

std::vector<QQuickItem*> UiRig::find_items(const QString& name, QQuickItem* from) const
{
    std::vector<QQuickItem*> found;
    QQuickItem* start = from != nullptr ? from : m_Impl->m_Root;
    std::function<void(QQuickItem*)> visit = [&](QQuickItem* item)
    {
        if (item->objectName() == name)
        {
            found.push_back(item);
        }
        for (QQuickItem* child : item->childItems())
        {
            visit(child);
        }
    };
    if (start != nullptr)
    {
        visit(start);
    }
    return found;
}

HideBase::HideBase(QQuickItem* base) : m_Base(base), m_WasVisible(base != nullptr && base->isVisible())
{
    if (m_Base != nullptr)
    {
        m_Base->setVisible(false);
    }
}

HideBase::~HideBase()
{
    if (m_Base != nullptr)
    {
        m_Base->setVisible(m_WasVisible);
    }
}

void UiRig::layout()
{
    // Rendering one frame runs polish and sync, which settles anchors, layouts and implicit sizes.
    (void)m_Impl->m_Window->grabWindow();
}

QImage UiRig::grab()
{
    return m_Impl->m_Window->grabWindow();
}

} // namespace ej::ui
