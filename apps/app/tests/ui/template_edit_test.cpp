// SPDX-License-Identifier: GPL-3.0-or-later
// TemplateEdit (docs/plan/stage-4.md, 4.13): editor.templates (name, prompt, pattern, the live pattern message, the
// preview, Save), editor.voiceover and the cross-cutting checks.
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

TEST_CASE("editor.templates: an existing template shows its name, prompt and pattern, and a preview", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);

    CHECK(page->property("title").toString() == QStringLiteral("Played song"));
    CHECK(field_text(world, QStringLiteral("nameField")) == QStringLiteral("Played song"));
    CHECK(field_text(world, QStringLiteral("promptField")) == QStringLiteral("Which song?"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Played {value}"));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    // The model builds the note line as the Log screen shows it, with its own sample time and answer.
    CHECK(preview(world) == QStringLiteral("10:30 (Free Text) Played Morning Medley"));
    CHECK(preview(world) == world.model().previewTemplateLine(QStringLiteral("Played {value}"), QString(), 37800));
    CHECK(save_enabled(page));
    CHECK(shows_text(world, QStringLiteral("{value} is replaced by what you type when you tap the chip."), page));
    // The prompt has no hint of its own: the mock-up shows none.
    CHECK_FALSE(shows_text(world, QStringLiteral("The question asked when the chip is tapped."), page));
}

TEST_CASE("editor.templates: a pattern the model refuses shows its message, no preview, and Save is dimmed",
          "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);

    // No {value}: the message is the model's, word for word.
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Played"));
    const QString refusal = world.model().validatePattern(QStringLiteral("Played"));
    REQUIRE_FALSE(refusal.isEmpty());
    CHECK(field_error(world, QStringLiteral("patternField")) == refusal);
    CHECK(preview(world) == QStringLiteral("Fix the pattern to see a preview."));
    CHECK_FALSE(save_enabled(page));

    // Tapping the dimmed Save changes nothing.
    tap_save(world, page);
    CHECK(world.m_Rig.store().saves().empty());

    // {name} is refused too, and so is a pattern over 500 characters.
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

    // A good pattern brings the preview back, while the person types.
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Heard {value} twice"));
    CHECK(field_error(world, QStringLiteral("patternField")).isEmpty());
    CHECK(preview(world) == QStringLiteral("10:30 (Free Text) Heard Morning Medley twice"));
    CHECK(save_enabled(page));
}

TEST_CASE("editor.templates: Save updates the template and closes the page", "[ui][editor]")
{
    World world;
    QQuickItem* page = load_edit(world, 0);
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Song played"));
    enter_text(world, QStringLiteral("promptField"), QStringLiteral("Which tune?"));
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Tune {value}"));
    tap_save(world, page);

    CHECK(closed.count() == 1);
    const auto& templates = world.model().configuration().m_Templates;
    REQUIRE(templates.size() == 2);
    CHECK(templates.front().m_Name == "Song played");
    CHECK(templates.front().m_ValuePrompt == "Which tune?");
    CHECK(templates.front().m_OutputPattern == "Tune {value}");
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
          QStringLiteral("Prompt"));
    CHECK(name_of(find_item(item(world, QStringLiteral("patternField")), QStringLiteral("formInput"))) ==
          QStringLiteral("Output pattern"));
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
