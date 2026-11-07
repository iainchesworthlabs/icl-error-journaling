// SPDX-License-Identifier: GPL-3.0-or-later
// NoteComposer (docs/plan/stage-4.md, 4.6): composer.draft_binding, composer.add, composer.chips, composer.undo_text
// and composer.dictation.
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QCoreApplication>
#include <QInputMethodEvent>
#include <QQuickItem>
#include <QSignalSpy>
#include <QStandardItemModel>

using namespace ej::ui;
using namespace ej::ui::input;

namespace
{

// The host plays the part of the Log screen: it holds the draft, hands it to the composer and stores what the composer
// says was typed, as the session does.
constexpr auto SCREEN = R"(
    property string draft: ""
    property var templates: null
    NoteComposer {
        id: composer
        objectName: "composer"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        draft: host.draft
        chips: host.templates
        onTextEdited: text => host.draft = text
    })";

/// A model with the roles of `settings.templates`: templateId and name.
class Templates
{
public:
    Templates()
    {
        m_Model.setItemRoleNames({{Qt::UserRole + 1, "templateId"}, {Qt::UserRole + 2, "name"}});
        add(QStringLiteral("played-song"), QStringLiteral("Played song"));
        add(QStringLiteral("visitor"), QStringLiteral("Visitor interaction"));
    }

    void add(const QString& id, const QString& name)
    {
        auto* item = new QStandardItem;
        item->setData(id, Qt::UserRole + 1);
        item->setData(name, Qt::UserRole + 2);
        m_Model.appendRow(item);
    }

    QStandardItemModel* model()
    {
        return &m_Model;
    }

private:
    QStandardItemModel m_Model;
};

struct Fixture
{
    explicit Fixture(const QString& source = QString::fromUtf8(SCREEN))
    {
        host = load_host(rig, source);
        REQUIRE(host != nullptr);
        host->setProperty("templates", QVariant::fromValue<QObject*>(templates.model()));
        rig.layout();
        composer = rig.find(QStringLiteral("composer"));
        REQUIRE(composer != nullptr);
        field = rig.find(QStringLiteral("noteInput"));
        REQUIRE(field != nullptr);
        add = rig.find(QStringLiteral("addButton"));
        REQUIRE(add != nullptr);
    }

    [[nodiscard]] QString text() const
    {
        return field->property("text").toString();
    }

    [[nodiscard]] int cursor() const
    {
        return field->property("cursorPosition").toInt();
    }

    Templates templates;
    UiRig rig;
    QQuickItem* host = nullptr;
    QQuickItem* composer = nullptr;
    QQuickItem* field = nullptr;
    QQuickItem* add = nullptr;
};

QStringList texts(QSignalSpy& spy)
{
    QStringList out;
    for (const QList<QVariant>& arguments : spy)
    {
        out.push_back(arguments.at(0).toString());
    }
    return out;
}

} // namespace

TEST_CASE("composer.draft_binding: typing reports each change, and the draft that comes back changes nothing",
          "[ui][composer]")
{
    Fixture f;
    INFO(f.rig.errors().toStdString());
    QSignalSpy edited(f.composer, SIGNAL(textEdited(QString)));

    f.field->forceActiveFocus();
    f.rig.type(QStringLiteral("abc"));
    CHECK(texts(edited) == QStringList{QStringLiteral("a"), QStringLiteral("ab"), QStringLiteral("abc")});
    // The screen stored each one as the draft; the composer did not touch the text because of it.
    CHECK(f.host->property("draft").toString() == QStringLiteral("abc"));
    CHECK(f.text() == QStringLiteral("abc"));
    CHECK(f.cursor() == 3);

    // Typing in the middle: the echo of the draft leaves the cursor where the user put it.
    f.field->setProperty("cursorPosition", 1);
    f.rig.type(QStringLiteral("X"));
    CHECK(f.text() == QStringLiteral("aXbc"));
    CHECK(f.cursor() == 2);
    CHECK(f.host->property("draft").toString() == QStringLiteral("aXbc"));
    CHECK(edited.count() == 4);
}

TEST_CASE("composer.draft_binding: a draft set from outside replaces the text without being reported as typed",
          "[ui][composer]")
{
    Fixture f;
    QSignalSpy edited(f.composer, SIGNAL(textEdited(QString)));

    f.host->setProperty("draft", QStringLiteral("Played Evening Song"));
    CHECK(f.text() == QStringLiteral("Played Evening Song"));
    CHECK(f.cursor() == 19);
    CHECK(edited.count() == 0);

    f.host->setProperty("draft", QStringLiteral("Played Evening Song again"));
    CHECK(f.text() == QStringLiteral("Played Evening Song again"));
    CHECK(f.cursor() == f.text().length());
    CHECK(edited.count() == 0);

    // The same text again is not a change: the cursor stays.
    f.field->setProperty("cursorPosition", 4);
    f.host->setProperty("draft", QStringLiteral("Played Evening Song again"));
    CHECK(f.cursor() == 4);
}

TEST_CASE("composer.add: the Add button and Return emit submit once with the text", "[ui][composer]")
{
    Fixture f;
    QSignalSpy submitted(f.composer, SIGNAL(submit(QString)));

    f.host->setProperty("draft", QStringLiteral("Mute Delay at the gate"));
    f.rig.tap(f.add);
    REQUIRE(submitted.count() == 1);
    CHECK(submitted.at(0).at(0).toString() == QStringLiteral("Mute Delay at the gate"));

    f.field->forceActiveFocus();
    f.rig.key(Qt::Key_Return);
    CHECK(submitted.count() == 2);
    CHECK(submitted.at(1).at(0).toString() == QStringLiteral("Mute Delay at the gate"));
}

TEST_CASE("composer.add: nothing or only spaces is ignored, and the text stays until the screen clears the draft",
          "[ui][composer]")
{
    Fixture f;
    QSignalSpy submitted(f.composer, SIGNAL(submit(QString)));

    f.rig.tap(f.add);
    f.field->forceActiveFocus();
    f.rig.key(Qt::Key_Return);
    CHECK(submitted.count() == 0);

    f.rig.type(QStringLiteral("   "));
    f.rig.tap(f.add);
    f.rig.key(Qt::Key_Return);
    CHECK(submitted.count() == 0);
    CHECK_FALSE(f.composer->property("canSubmit").toBool());

    f.rig.type(QStringLiteral("ok"));
    CHECK(f.composer->property("canSubmit").toBool());
    f.rig.tap(f.add);
    CHECK(submitted.count() == 1);
    // A refused note is still in the field: the composer clears nothing.
    CHECK(f.text() == QStringLiteral("   ok"));

    // The session clears the draft when the note is committed, and the field follows.
    f.host->setProperty("draft", QString());
    CHECK(f.text().isEmpty());
    CHECK_FALSE(f.composer->property("canSubmit").toBool());
}

TEST_CASE("composer.chips: a chip emits chipChosen with its template id, and a long name wraps and is never cut",
          "[ui][composer]")
{
    Fixture f;
    f.templates.add(QStringLiteral("long"), QStringLiteral("A visitor asked about the Wrong Response at North Site"));
    f.rig.layout();
    QSignalSpy chosen(f.composer, SIGNAL(chipChosen(QString)));

    std::vector<Node> chips;
    for (const Node& node : f.rig.tree(f.composer))
    {
        if (node.m_ObjectName == QStringLiteral("chip"))
        {
            chips.push_back(node);
        }
    }
    REQUIRE(chips.size() == 3);
    CHECK(chips[0].m_Name == QStringLiteral("Played song"));
    CHECK(chips[1].m_Name == QStringLiteral("Visitor interaction"));
    CHECK(chips[2].m_Name == QStringLiteral("A visitor asked about the Wrong Response at North Site"));
    for (const Node& chip : chips)
    {
        CHECK(chip.m_Role == QAccessible::Button);
        CHECK(chip.m_Rect.right() <= f.rig.geometry(f.composer).right());
    }
    // The long name wraps onto a row of its own, and its chip grows to hold all of it. (How many short chips share a
    // row depends on the font, so it is not asserted.)
    CHECK(chips[2].m_Rect.top() >= chips[1].m_Rect.bottom());
    CHECK(chips[2].m_Rect.height() > 32);
    CHECK(chips[2].m_Rect.width() <= f.rig.geometry(f.composer).width());

    f.rig.tap(chips[1].m_Item);
    f.rig.tap(chips[0].m_Item);
    REQUIRE(chosen.count() == 2);
    CHECK(chosen.at(0).at(0).toString() == QStringLiteral("visitor"));
    CHECK(chosen.at(1).at(0).toString() == QStringLiteral("played-song"));

    f.rig.set_text_scale(3.1);
    expect_clean(check_text_scale(f.rig));
}

TEST_CASE("composer.chips: a template added to the model appears, and with none the row is gone and the card shrinks",
          "[ui][composer]")
{
    Fixture f;
    f.host->setProperty("draft", QStringLiteral("keep this"));
    const auto chips = [&f]
    {
        int count = 0;
        for (const Node& node : f.rig.tree(f.composer))
        {
            count += node.m_ObjectName == QStringLiteral("chip") ? 1 : 0;
        }
        return count;
    };
    CHECK(chips() == 2);
    const double with_chips = f.rig.geometry(f.composer).height();

    f.templates.add(QStringLiteral("third"), QStringLiteral("Third template"));
    f.rig.layout();
    CHECK(chips() == 3);

    f.templates.model()->clear();
    f.rig.layout();
    CHECK(chips() == 0);
    CHECK(f.rig.geometry(f.composer).height() < with_chips);
    // The note being typed is untouched by all of it.
    CHECK(f.text() == QStringLiteral("keep this"));
}

TEST_CASE("composer.undo_text: text returned by the note undo is placed in the field with the cursor at the end",
          "[ui][composer]")
{
    Fixture f;
    QSignalSpy edited(f.composer, SIGNAL(textEdited(QString)));
    QSignalSpy submitted(f.composer, SIGNAL(submit(QString)));

    f.field->forceActiveFocus();
    f.rig.type(QStringLiteral("Wrong Response"));
    f.rig.tap(f.add);
    REQUIRE(submitted.count() == 1);
    // Committed: the session clears the draft.
    f.host->setProperty("draft", QString());
    CHECK(f.text().isEmpty());
    const int typed = edited.count();

    // Undo: the session gives the text back as the draft.
    f.host->setProperty("draft", QStringLiteral("Wrong Response"));
    CHECK(f.text() == QStringLiteral("Wrong Response"));
    CHECK(f.cursor() == 14);
    CHECK(edited.count() == typed);

    // Typing goes on from the end.
    f.rig.type(QStringLiteral("!"));
    CHECK(f.text() == QStringLiteral("Wrong Response!"));
    CHECK(f.host->property("draft").toString() == QStringLiteral("Wrong Response!"));
}

TEST_CASE("composer.dictation: the field takes text committed by the input method, as dictation delivers it",
          "[ui][composer]")
{
    Fixture f;
    QSignalSpy edited(f.composer, SIGNAL(textEdited(QString)));

    f.field->forceActiveFocus();
    QInputMethodEvent event;
    event.setCommitString(QStringLiteral("Mute Delay near the stage"));
    QCoreApplication::sendEvent(f.field, &event);
    CHECK(f.text() == QStringLiteral("Mute Delay near the stage"));
    REQUIRE(edited.count() >= 1);
    CHECK(edited.last().at(0).toString() == QStringLiteral("Mute Delay near the stage"));
    CHECK(f.host->property("draft").toString() == QStringLiteral("Mute Delay near the stage"));
}

TEST_CASE("composer.skeleton: shapes only, with the same geometry as the card", "[ui][composer]")
{
    Fixture f;
    // Two short chips, which share a row whatever the font, as the two skeleton chips do.
    f.templates.model()->clear();
    f.templates.add(QStringLiteral("a"), QStringLiteral("Song"));
    f.templates.add(QStringLiteral("b"), QStringLiteral("Visit"));
    f.rig.layout();
    const QRectF card = f.rig.geometry(f.composer);
    const QRectF field = f.rig.geometry(f.field);

    f.composer->setProperty("skeleton", true);
    f.rig.layout();
    CHECK(f.rig.geometry(f.composer) == card);
    CHECK(f.rig.geometry(f.field) == field);
    for (const Node& node : f.rig.tree(f.composer))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) && node.m_Visible)
        {
            CHECK(node.m_Item->property("text").toString().isEmpty());
        }
        if (node.m_Interactive && node.m_Visible)
        {
            CHECK_FALSE(node.m_InAccessibleTree);
        }
    }
}

TEST_CASE("composer.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][composer]")
{
    Fixture f;
    expect_clean(check_everything(f.rig));
    f.rig.set_dark(true);
    expect_clean(check_everything(f.rig));
    f.rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(f.rig));
}
