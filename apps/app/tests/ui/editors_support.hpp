// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
#include "settings_rig.hpp"

#include <QAccessible>
#include <QByteArray>
#include <QMetaObject>
#include <QObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

/// What the tests of the editors, the first-launch screens and the import screens of package 4.13 share: the real
/// `SettingsModel` of Stage 3 on its fakes and the rig that loads a screen over it, with a few lookups.
namespace ej::ui::editors
{

/// The text of `config/sample-config.json`.
[[nodiscard]] inline std::string sample_text()
{
    std::ifstream in(EJ_SAMPLE_CONFIG, std::ios::binary);
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// A model that has loaded the sample configuration (or, for `FIRST_LAUNCH`, found no saved file), and the rig. The
/// model's saves wait in the manual gateway until `settle()` runs them, so a test can look between "asked" and "saved".
struct World
{
    enum class Start
    {
        SAMPLE,
        FIRST_LAUNCH,
        /// A saved file the model cannot use: it starts setup again and says why (`loadProblem`).
        DAMAGED_FILE
    };

    explicit World(Start start = Start::SAMPLE)
    {
        if (start == Start::SAMPLE)
        {
            m_Rig.load_sample();
        }
        else if (start == Start::DAMAGED_FILE)
        {
            m_Rig.load_text(std::string("{ \"schema_version\": "));
        }
        else
        {
            m_Rig.load_text(std::nullopt);
        }
        m_Ui.attach(m_Rig.m_Gateway);
    }

    /// A model that has loaded `configuration`.
    explicit World(const Configuration& configuration)
    {
        m_Rig.load_configuration(configuration);
        m_Ui.attach(m_Rig.m_Gateway);
    }

    [[nodiscard]] viewmodel::SettingsModel& model()
    {
        return m_Rig.m_Settings;
    }

    [[nodiscard]] QVariant settings()
    {
        return QVariant::fromValue(static_cast<QObject*>(&m_Rig.m_Settings));
    }

    /// Loads the screen `type` with the model as its `settings`, and any other properties.
    QQuickItem* load(const QString& type, QVariantMap properties = {})
    {
        properties.insert(QStringLiteral("settings"), settings());
        QQuickItem* item = m_Ui.load(type, properties);
        INFO(m_Ui.errors().toStdString());
        REQUIRE(item != nullptr);
        return item;
    }

    /// Runs the saves that wait, and the events they leave.
    void settle()
    {
        m_Ui.settle();
    }

    /// Plays animation time one frame at a time, so that the animators of a sheet or a page see it go by.
    void play(int ms)
    {
        constexpr int FRAME = 16;
        for (int done = 0; done < ms; done += FRAME)
        {
            m_Ui.advance(std::chrono::milliseconds(FRAME));
        }
    }

    test::SettingsRig m_Rig;
    UiRig m_Ui;
};

/// The first item at or under `from` whose objectName is `name`, found by the visual tree (a delegate of a Repeater is
/// not under its QObject parent, which is where `UiRig::find` looks).
[[nodiscard]] inline QQuickItem* find_item(QQuickItem* from, const QString& name)
{
    if (from == nullptr)
    {
        return nullptr;
    }
    if (from->objectName() == name)
    {
        return from;
    }
    for (QQuickItem* child : from->childItems())
    {
        if (QQuickItem* found = find_item(child, name))
        {
            return found;
        }
    }
    return nullptr;
}

/// What VoiceOver does when it adjusts a control (a swipe up or down): the "increase" or "decrease" action of the
/// item's accessible interface. False when the item has no such action.
[[nodiscard]] inline bool accessible_adjust(QQuickItem* item, bool increase)
{
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item);
    if (iface == nullptr)
    {
        return false;
    }
    QAccessibleActionInterface* actions = iface->actionInterface();
    const QString action =
        increase ? QAccessibleActionInterface::increaseAction() : QAccessibleActionInterface::decreaseAction();
    if (actions == nullptr || !actions->actionNames().contains(action))
    {
        return false;
    }
    actions->doAction(action);
    return true;
}

/// `found`, which has to be there: the test fails here and goes no further when it is not, so the caller may use it.
[[nodiscard]] inline QQuickItem* require(QQuickItem* found)
{
    REQUIRE(found != nullptr);
    return found;
}

/// The first item under the loaded one whose objectName is `name`, found by the visual tree (a delegate of a Repeater
/// is not under its QObject parent). The test fails when there is none.
[[nodiscard]] inline QQuickItem* item(World& world, const QString& name)
{
    INFO(name.toStdString());
    return require(find_item(world.m_Ui.root(), name));
}

/// The first item at or under `from` with the objectName; the test fails when there is none.
[[nodiscard]] inline QQuickItem* child(QQuickItem* from, const QString& name)
{
    INFO(name.toStdString());
    return require(find_item(from, name));
}

/// Every item under `from` whose objectName is `name`, in visual order.
inline void collect(QQuickItem* from, const QString& name, std::vector<QQuickItem*>& out)
{
    if (from == nullptr)
    {
        return;
    }
    if (from->objectName() == name)
    {
        out.push_back(from);
    }
    for (QQuickItem* child : from->childItems())
    {
        collect(child, name, out);
    }
}

[[nodiscard]] inline std::vector<QQuickItem*> items(World& world, const QString& name, QQuickItem* from = nullptr)
{
    std::vector<QQuickItem*> found;
    collect(from != nullptr ? from : world.m_Ui.root(), name, found);
    return found;
}

/// What a screen reader says for the item.
[[nodiscard]] inline QString name_of(QQuickItem* item)
{
    QAccessibleInterface* iface = item != nullptr ? QAccessible::queryAccessibleInterface(item) : nullptr;
    return iface != nullptr ? iface->text(QAccessible::Name) : QString();
}

/// The names of the labels (`editorLabel`) of the rows under `from`, in order: the text of each row and its detail.
[[nodiscard]] inline QStringList row_names(World& world, const QString& row_name, QQuickItem* from = nullptr)
{
    QStringList names;
    for (QQuickItem* row : items(world, row_name, from))
    {
        names.push_back(row->property("text").toString());
    }
    return names;
}

/// The row whose text is `text`, or null.
[[nodiscard]] inline QQuickItem* row_with(World& world, const QString& row_name, const QString& text,
                                          QQuickItem* from = nullptr)
{
    for (QQuickItem* row : items(world, row_name, from))
    {
        if (row->property("text").toString() == text)
        {
            return row;
        }
    }
    return nullptr;
}

/// The label button of a row (the part that opens it).
[[nodiscard]] inline QQuickItem* label_of(QQuickItem* row)
{
    return find_item(row, QStringLiteral("editorLabel"));
}

/// Replaces what is in the text field of the `FormField` named `field_name` by `text`, as the keyboard does.
inline void enter_text(World& world, const QString& field_name, const QString& text)
{
    QQuickItem* field = item(world, field_name);
    REQUIRE(field != nullptr);
    QQuickItem* input = find_item(field, QStringLiteral("formInput"));
    REQUIRE(input != nullptr);
    input->forceActiveFocus();
    QMetaObject::invokeMethod(input, "selectAll");
    if (text.isEmpty())
    {
        world.m_Ui.key(Qt::Key_Delete);
    }
    else
    {
        world.m_Ui.type(text);
    }
}

/// The text of the `FormField` named `field_name`.
[[nodiscard]] inline QString field_text(World& world, const QString& field_name)
{
    QQuickItem* input = find_item(item(world, field_name), QStringLiteral("formInput"));
    return input != nullptr ? input->property("text").toString() : QString();
}

/// The message shown under the `FormField` named `field_name`; empty when it has none.
[[nodiscard]] inline QString field_error(World& world, const QString& field_name)
{
    return item(world, field_name)->property("errorText").toString();
}

/// Taps the first shown item with the objectName (a name two pages can share, such as `cancelButton`, is the one in
/// view), then lets the saves run.
inline void tap(World& world, const QString& name)
{
    QQuickItem* target = nullptr;
    for (QQuickItem* candidate : items(world, name))
    {
        if (candidate->isVisible())
        {
            target = candidate;
            break;
        }
    }
    INFO(name.toStdString());
    REQUIRE(target != nullptr);
    world.m_Ui.tap(target);
    world.m_Ui.settle();
    world.m_Ui.layout();
}

/// True when some shown `Text` under `from` has exactly this text.
[[nodiscard]] inline bool shows_text(World& world, const QString& text, QQuickItem* from = nullptr)
{
    for (const Node& node : world.m_Ui.tree(from))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) && node.m_Visible &&
            node.m_Item->property("text").toString() == text)
        {
            return true;
        }
    }
    return false;
}

} // namespace ej::ui::editors
