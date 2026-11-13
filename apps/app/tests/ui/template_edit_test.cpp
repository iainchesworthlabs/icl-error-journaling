// SPDX-License-Identifier: GPL-3.0-or-later
// TemplateEdit (docs/plan/stage-4.md, 4.13): editor.templates (name, question, line, the live message, the example,
// Insert answer, the defaults of a new template, Save), editor.voiceover and the cross-cutting checks. The page uses
// plain words: Name, Question to ask, Line in the log, and [answer] for the stored {value}.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

/// Loads TemplateEdit for the template in row `row` of the model (a new one for -1).
QQuickItem* load_edit(World& world, int row)
{
    QVariantMap properties;
    if (row >= 0)
    {
        const QAbstractItemModel* model = world.model().templates();
        const auto role = [&](const char* name)
        { return model->data(model->index(row, 0), test::role_named(*model, name)); };
        properties.insert(QStringLiteral("templateId"), role("templateId"));
        properties.insert(QStringLiteral("templateName"), role("name"));
        properties.insert(QStringLiteral("prompt"), role("valuePrompt"));
        properties.insert(QStringLiteral("pattern"), role("outputPattern"));
    }
    QQuickItem* page = world.load("TemplateEdit", properties);
    world.m_Ui.layout();
    return page;
}

QString preview(World& world)
{
    return item(world, QStringLiteral("previewText"))->property("text").toString();
}

/// What a screen reader adds after the name (the description of the item).
QString description_of(QQuickItem* target)
{
    QAccessibleInterface* iface = target != nullptr ? QAccessible::queryAccessibleInterface(target) : nullptr;
    return iface != nullptr ? iface->text(QAccessible::Description) : QString();
}

/// The text cursor of the input inside the `FormField` named `field_name`.
void put_cursor(World& world, const QString& field_name, int position)
{
    QQuickItem* input = find_item(item(world, field_name), QStringLiteral("formInput"));
    REQUIRE(input != nullptr);
    input->forceActiveFocus();
    input->setProperty("cursorPosition", position);
}

bool save_enabled(QQuickItem* page)
{
    return child(page, QStringLiteral("navTrailing"))->isEnabled();
}

void tap_save(World& world, QQuickItem* page)
{
    world.m_Ui.tap(find_item(page, QStringLiteral("navTrailing")));
    world.settle();
    world.m_Ui.layout();
}

} // namespace

TEST_CASE("editor.templates: an existing template shows its name, question and line, and an example", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);

    CHECK(page->property("title").toString() == QStringLiteral("Played song"));
    CHECK(field_text(world, QStringLiteral("nameField")) == QStringLiteral("Played song"));
    CHECK(field_text(world, QStringLiteral("promptField")) == QStringLiteral("Which song?"));
    // The field shows the answer as [answer]; the stored pattern is still {value}.
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Played [answer]"));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    // The model builds the note line as the Log screen shows it, with its own sample time and answer.
    CHECK(preview(world) == QStringLiteral("In the log: 10:30 Played Morning Medley"));
    CHECK(preview(world) == QStringLiteral("In the log: ") +
                                world.model().previewTemplateLine(QStringLiteral("Played {value}"), QString(), 37800));
    CHECK(save_enabled(page));

    // One sentence says what a template is for, and each field says what it is.
    CHECK(shows_text(world,
                     QStringLiteral("Templates are quick notes you log often. Tap one on the Log screen and answer one "
                                    "question."),
                     page));
    CHECK(item(world, QStringLiteral("nameField"))->property("label").toString() == QStringLiteral("Name"));
    CHECK(item(world, QStringLiteral("promptField"))->property("label").toString() ==
          QStringLiteral("Question to ask"));
    CHECK(item(world, QStringLiteral("patternField"))->property("label").toString() ==
          QStringLiteral("Line in the log"));
    CHECK(shows_text(world, QStringLiteral("This is what the chip on the Log screen says."), page));
    CHECK(shows_text(world, QStringLiteral("This is what you are asked when you tap the chip."), page));
    CHECK(shows_text(
        world, QStringLiteral("This is what gets written. Tap Insert answer to put [answer] where the answer goes."),
        page));
    CHECK(shows_text(world, QStringLiteral("Insert answer"), page));

    // The old words and the raw braces are gone from everything the page shows.
    for (const Node& node : world.m_Ui.tree(page))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) && node.m_Visible)
        {
            const QString shown = node.m_Item->property("text").toString();
            CHECK_FALSE(shown.contains(QStringLiteral("{value}")));
            CHECK_FALSE(shown.contains(QStringLiteral("pattern"), Qt::CaseInsensitive));
            CHECK_FALSE(shown.contains(QStringLiteral("prompt"), Qt::CaseInsensitive));
        }
    }
}

TEST_CASE("editor.templates: a line the model refuses shows its message, no example, and Save is dimmed",
          "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);

    // No answer in it: the message is the model's, word for word, and says what to tap.
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Played"));
    const QString refusal = world.model().validatePattern(QStringLiteral("Played"));
    REQUIRE_FALSE(refusal.isEmpty());
    CHECK(field_error(world, QStringLiteral("patternField")) == refusal);
    CHECK(refusal.contains(QStringLiteral("Insert answer")));
    CHECK_FALSE(refusal.contains(QStringLiteral("{")));
    CHECK(preview(world) == QStringLiteral("Fix the line to see an example."));
    CHECK_FALSE(save_enabled(page));

    // Tapping the dimmed Save changes nothing.
    tap_save(world, page);
    CHECK(world.m_Rig.store().saves().empty());

    // {name} is refused too, and so is a line over 500 characters.
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Played {name}"));
    CHECK(field_error(world, QStringLiteral("patternField")) ==
          world.model().validatePattern(QStringLiteral("Played {name}")));
    CHECK_FALSE(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK_FALSE(save_enabled(page));

    enter_text(world, QStringLiteral("patternField"), QStringLiteral("{value}") + QString(494, QLatin1Char('x')));
    CHECK_FALSE(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK_FALSE(save_enabled(page));
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("{value}") + QString(493, QLatin1Char('x')));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK(save_enabled(page));

    // A good line brings the example back, while the person types: with the token the button writes ...
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Heard [answer] twice"));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK(preview(world) == QStringLiteral("In the log: 10:30 Heard Morning Medley twice"));
    CHECK(save_enabled(page));

    // ... and with {value} typed by hand, which still works.
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Heard {value} twice"));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK(preview(world) == QStringLiteral("In the log: 10:30 Heard Morning Medley twice"));
    CHECK(save_enabled(page));
}

TEST_CASE("editor.templates: Save updates the template and closes the page", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Song played"));
    enter_text(world, QStringLiteral("promptField"), QStringLiteral("Which tune?"));
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Tune [answer]"));
    tap_save(world, page);

    CHECK(closed.count() == 1);
    const auto& templates = world.model().configuration().m_Templates;
    REQUIRE(templates.size() == 2);
    CHECK(templates.front().m_Name == "Song played");
    CHECK(templates.front().m_ValuePrompt == "Which tune?");
    // The file format is unchanged: the stored pattern says {value}.
    CHECK(templates.front().m_OutputPattern == "Tune {value}");
}

TEST_CASE("editor.templates: Insert answer puts [answer] at the cursor, over a selection, and keeps the focus",
          "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);
    QQuickItem* input = find_item(item(world, QStringLiteral("patternField")), QStringLiteral("formInput"));
    REQUIRE(input != nullptr);

    // In the middle of the line.
    put_cursor(world, QStringLiteral("patternField"), 7);
    tap(world, QStringLiteral("insertAnswerButton"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Played [answer][answer]"));
    CHECK(input->property("cursorPosition").toInt() == 15);
    CHECK(input->property("activeFocus").toBool());
    CHECK(preview(world) == QStringLiteral("In the log: 10:30 Played Morning MedleyMorning Medley"));

    // At the start.
    put_cursor(world, QStringLiteral("patternField"), 0);
    tap(world, QStringLiteral("insertAnswerButton"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("[answer]Played [answer][answer]"));
    CHECK(input->property("cursorPosition").toInt() == 8);

    // In place of the selected text, and the same field says where the cursor is next.
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Played x"));
    QMetaObject::invokeMethod(input, "select", Q_ARG(int, 7), Q_ARG(int, 8));
    tap(world, QStringLiteral("insertAnswerButton"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Played [answer]"));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK(save_enabled(page));

    // Saved, the stored pattern says {value}.
    QSignalSpy closed(page, SIGNAL(closeRequested()));
    tap_save(world, page);
    CHECK(closed.count() == 1);
    CHECK(world.model().configuration().m_Templates.front().m_OutputPattern == "Played {value}");
}

TEST_CASE("editor.templates: Insert answer makes a refused line good", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);

    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Heard "));
    REQUIRE_FALSE(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK_FALSE(save_enabled(page));

    put_cursor(world, QStringLiteral("patternField"), 6);
    tap(world, QStringLiteral("insertAnswerButton"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Heard [answer]"));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK(preview(world) == QStringLiteral("In the log: 10:30 Heard Morning Medley"));
    CHECK(save_enabled(page));
}

TEST_CASE("editor.templates: a new template starts with a question and a line made from its name", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, -1);
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    // Nothing is made from an empty name.
    CHECK(field_text(world, QStringLiteral("promptField")).isEmpty());
    CHECK(field_text(world, QStringLiteral("patternField")).isEmpty());
    CHECK_FALSE(save_enabled(page));

    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Visitor note"));
    CHECK(field_text(world, QStringLiteral("promptField")) == QStringLiteral("Visitor note?"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Visitor note: [answer]"));
    CHECK(preview(world) == QStringLiteral("In the log: 10:30 Visitor note: Morning Medley"));
    CHECK(save_enabled(page));

    // They follow the name while they are untouched ...
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Visitor chat"));
    CHECK(field_text(world, QStringLiteral("promptField")) == QStringLiteral("Visitor chat?"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Visitor chat: [answer]"));

    // ... and stop following once the person has written their own.
    enter_text(world, QStringLiteral("promptField"), QStringLiteral("Who came by?"));
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Visitor visit"));
    CHECK(field_text(world, QStringLiteral("promptField")) == QStringLiteral("Who came by?"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Visitor visit: [answer]"));
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Visit by [answer]"));
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Visit"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Visit by [answer]"));

    tap_save(world, page);
    CHECK(closed.count() == 1);
    const auto& added = world.model().configuration().m_Templates.back();
    CHECK(added.m_Name == "Visit");
    CHECK(added.m_ValuePrompt == "Who came by?");
    CHECK(added.m_OutputPattern == "Visit by {value}");
}

TEST_CASE("editor.templates: the defaults are saved as they are shown, and an existing template is not rewritten",
          "[ui][editor]")
{
    {
        World world;
        QQuickItem* page = load_edit(world, -1);
        enter_text(world, QStringLiteral("nameField"), QStringLiteral("Volume check"));
        tap_save(world, page);
        const auto& added = world.model().configuration().m_Templates.back();
        CHECK(added.m_Name == "Volume check");
        CHECK(added.m_ValuePrompt == "Volume check?");
        CHECK(added.m_OutputPattern == "Volume check: {value}");
    }
    {
        // Renaming a template that exists leaves its question and line alone.
        World world;
        QQuickItem* page = load_edit(world, 0);
        enter_text(world, QStringLiteral("nameField"), QStringLiteral("Song played"));
        CHECK(field_text(world, QStringLiteral("promptField")) == QStringLiteral("Which song?"));
        CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Played [answer]"));
        CHECK(page != nullptr);
    }
}

TEST_CASE("editor.templates: a new template needs its three fields, and a refused name is shown in a banner",
          "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, -1);
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    CHECK(page->property("title").toString() == QStringLiteral("New template"));
    // An empty pattern is not an error yet (the field only has its hint), but Save is dimmed.
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK_FALSE(save_enabled(page));

    // A name that another template has, in other capitals: refused, banner, Save dimmed until an edit.
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("visitor INTERACTION"));
    enter_text(world, QStringLiteral("promptField"), QStringLiteral("Who?"));
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Visitor {value}"));
    REQUIRE(save_enabled(page));
    tap_save(world, page);
    CHECK(closed.count() == 0);
    QQuickItem* banner = item(world, QStringLiteral("refusalBanner"));
    CHECK(banner->isVisible());
    CHECK_FALSE(banner->property("message").toString().isEmpty());
    CHECK_FALSE(save_enabled(page));
    CHECK(world.model().templates()->rowCount() == 2);

    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Visitor note"));
    CHECK_FALSE(banner->isVisible());
    CHECK(save_enabled(page));
    tap_save(world, page);
    CHECK(closed.count() == 1);
    CHECK(world.model().templates()->rowCount() == 3);
}

TEST_CASE("editor.templates: the name and the prompt are limited to 200 characters", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, -1);
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    enter_text(world, QStringLiteral("nameField"), QString(201, QLatin1Char('n')));
    enter_text(world, QStringLiteral("promptField"), QStringLiteral("Who?"));
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Seen {value}"));
    tap_save(world, page);
    CHECK(closed.count() == 0);
    CHECK(item(world, QStringLiteral("refusalBanner"))->isVisible());

    enter_text(world, QStringLiteral("nameField"), QString(200, QLatin1Char('n')));
    enter_text(world, QStringLiteral("promptField"), QString(201, QLatin1Char('p')));
    tap_save(world, page);
    CHECK(closed.count() == 0);
    CHECK(item(world, QStringLiteral("refusalBanner"))->isVisible());

    enter_text(world, QStringLiteral("promptField"), QString(200, QLatin1Char('p')));
    tap_save(world, page);
    CHECK(closed.count() == 1);
}

TEST_CASE("editor.templates: Back closes without saving", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);
    QSignalSpy closed(page, SIGNAL(closeRequested()));
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Changed"));
    world.m_Ui.tap(find_item(page, QStringLiteral("navLeading")));
    world.settle();

    CHECK(closed.count() == 1);
    CHECK(world.m_Rig.store().saves().empty());
    CHECK(world.model().configuration().m_Templates.front().m_Name == "Played song");
}

TEST_CASE("editor.voiceover: the template fields are named, the invalid state is read, and the page passes the checks",
          "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);

    CHECK(name_of(find_item(item(world, QStringLiteral("nameField")), QStringLiteral("formInput"))) ==
          QStringLiteral("Name"));
    CHECK(name_of(find_item(item(world, QStringLiteral("promptField")), QStringLiteral("formInput"))) ==
          QStringLiteral("Question to ask"));
    CHECK(name_of(find_item(item(world, QStringLiteral("patternField")), QStringLiteral("formInput"))) ==
          QStringLiteral("Line in the log"));
    // The insert button is a named button that says what it does; the token is explained by the field's description.
    QQuickItem* insert = item(world, QStringLiteral("insertAnswerButton"));
    CHECK(name_of(insert) == QStringLiteral("Insert answer"));
    CHECK(description_of(insert).contains(QStringLiteral("[answer]")));
    CHECK(description_of(find_item(item(world, QStringLiteral("patternField")), QStringLiteral("formInput")))
              .contains(QStringLiteral("[answer]")));
    CHECK(name_of(find_item(page, QStringLiteral("navLeading"))) == QStringLiteral("Back to Note templates"));
    CHECK(name_of(find_item(page, QStringLiteral("navTrailing"))) == QStringLiteral("Save"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(false);

    // The same page with a pattern refused.
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Played"));
    world.m_Ui.layout();
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Played"));
    expect_clean(check_everything(world.m_Ui));
}
