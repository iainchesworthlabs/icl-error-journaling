// SPDX-License-Identifier: GPL-3.0-or-later
// ImportFlow (docs/plan/stage-4.md, 4.13): import.flow (the chosen path reaches loadImport, the summary shows the
// counts, Apply applies, Cancel changes nothing), import.errors (the core's messages in file order, nothing changes)
// and import.sample (the sample file imports and the board, the picker and the lists show it).
#include "editors_support.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/error_picker_model.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

using Start = World::Start;

const QString SETUP_NAME = QStringLiteral("field-test-setup.ejconfig");

std::string replace_first(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

/// Gives the flow the file at `path`, as the shell does after its file dialog, and lets the model read it.
void begin(World& world, QQuickItem* flow, const QString& path, const QString& name = SETUP_NAME)
{
    REQUIRE(QMetaObject::invokeMethod(flow, "begin", Q_ARG(QString, path), Q_ARG(QString, name)));
    world.settle();
    world.m_Ui.layout();
}

QString page(QQuickItem* flow)
{
    return flow->property("page").toString();
}

QQuickItem* confirm(World& world)
{
    return item(world, QStringLiteral("importConfirm"));
}

QQuickItem* errors(World& world)
{
    return item(world, QStringLiteral("importErrors"));
}

QString value_of(World& world, const QString& row)
{
    return item(world, row)->property("value").toString();
}

} // namespace

TEST_CASE("import.flow: nothing is shown until a file has been read, then the summary with the counts", "[ui][import]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* flow = world.load("ImportFlow");
    world.m_Ui.layout();
    world.m_Rig.store().add_file("/files/field-test-setup.ejconfig", sample_text());

    CHECK_FALSE(flow->isVisible());
    CHECK(page(flow) == QStringLiteral("none"));

    REQUIRE(QMetaObject::invokeMethod(flow, "begin",
                                      Q_ARG(QString, QStringLiteral("/files/field-test-setup.ejconfig")),
                                      Q_ARG(QString, SETUP_NAME)));
    // The model reads the file on its storage thread: until the gateway has run, the person sees nothing yet.
    CHECK(world.m_Rig.m_Gateway.pending() == 1);
    CHECK_FALSE(flow->isVisible());
    world.settle();
    world.m_Ui.layout();

    CHECK(flow->isVisible());
    CHECK(page(flow) == QStringLiteral("confirm"));
    CHECK(confirm(world)->isVisible());
    CHECK_FALSE(errors(world)->isVisible());
    CHECK(value_of(world, QStringLiteral("errorTypesRow")) == QStringLiteral("11"));
    CHECK(value_of(world, QStringLiteral("locationsRow")) == QStringLiteral("2"));
    CHECK(value_of(world, QStringLiteral("templatesRow")) == QStringLiteral("2"));
    CHECK(item(world, QStringLiteral("fromText"))->property("text").toString() ==
          QStringLiteral("From field-test-setup.ejconfig"));

    // Nothing has changed yet.
    CHECK_FALSE(world.model().configured());
    CHECK(world.m_Rig.store().saves().empty());
}

TEST_CASE("import.flow: Apply replaces the setup in one save and the flow closes", "[ui][import]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* flow = world.load("ImportFlow");
    world.m_Rig.store().add_file("/files/setup.ejconfig", sample_text());
    QSignalSpy finished(flow, SIGNAL(finished()));
    begin(world, flow, QStringLiteral("/files/setup.ejconfig"));
    REQUIRE(page(flow) == QStringLiteral("confirm"));

    // On first launch there is nothing to replace, and the words say so.
    CHECK(item(world, QStringLiteral("replaceButton"))->property("text").toString() ==
          QStringLiteral("Use this setup"));

    tap(world, QStringLiteral("replaceButton"));
    CHECK(page(flow) == QStringLiteral("none"));
    CHECK_FALSE(flow->isVisible());
    CHECK(finished.count() == 1);
    CHECK(world.model().configured());
    CHECK(world.m_Rig.store().saves().size() == 1);
    CHECK(world.m_Rig.store().saved() == sample_text());
    CHECK(world.model().error_types()->rowCount() == 11);
}

TEST_CASE("import.flow: with a setup in force the words say it is replaced, and Cancel changes nothing", "[ui][import]")
{
    World world(Start::SAMPLE);
    QQuickItem* flow = world.load("ImportFlow");
    world.m_Rig.store().add_file("/files/setup.json", sample_text());
    QSignalSpy finished(flow, SIGNAL(finished()));
    begin(world, flow, QStringLiteral("/files/setup.json"), QStringLiteral("setup.json"));
    REQUIRE(page(flow) == QStringLiteral("confirm"));

    CHECK(item(world, QStringLiteral("replaceButton"))->property("text").toString() ==
          QStringLiteral("Replace my setup"));
    CHECK(item(world, QStringLiteral("replaceBanner"))->property("title").toString() ==
          QStringLiteral("This replaces your current setup"));

    tap(world, QStringLiteral("cancelButton"));
    CHECK(page(flow) == QStringLiteral("none"));
    CHECK(finished.count() == 1);
    CHECK(world.m_Rig.store().saves().empty());

    // The file was put away, so there is nothing to apply any more.
    QSignalSpy failed(&world.model(), &viewmodel::SettingsModel::writeFailed);
    world.model().applyImport();
    CHECK(failed.count() == 1);
    CHECK(world.m_Rig.store().saves().empty());
}

TEST_CASE("import.flow: a file saved with the other accepted extension imports the same way", "[ui][import]")
{
    for (const char* path :
         {"/files/anything.json", "/files/My Setup.ejconfig", "/files/field-test-setup.ejconfig"})
    {
        World world(Start::FIRST_LAUNCH);
        QQuickItem* flow = world.load("ImportFlow");
        world.m_Rig.store().add_file(path, sample_text());
        begin(world, flow, QString::fromLatin1(path));
        INFO(path);
        CHECK(page(flow) == QStringLiteral("confirm"));
    }
}

TEST_CASE("import.flow: a save that fails keeps the page, says why, and a second try works", "[ui][import]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* flow = world.load("ImportFlow");
    world.m_Rig.store().add_file("/files/setup.json", sample_text());
    begin(world, flow, QStringLiteral("/files/setup.json"));

    world.m_Rig.store().fail_next_save(make_error(StorageErrorKind::DISK_FULL, "no room"));
    tap(world, QStringLiteral("replaceButton"));
    CHECK(page(flow) == QStringLiteral("confirm"));
    CHECK(item(world, QStringLiteral("problemBanner"))->isVisible());
    CHECK_FALSE(item(world, QStringLiteral("problemBanner"))->property("message").toString().isEmpty());
    CHECK_FALSE(world.model().configured());

    tap(world, QStringLiteral("replaceButton"));
    CHECK(page(flow) == QStringLiteral("none"));
    CHECK(world.model().configured());
}

TEST_CASE("import.errors: the core's messages are shown in file order and nothing changes", "[ui][import]")
{
    World world(Start::SAMPLE);
    QQuickItem* flow = world.load("ImportFlow");
    const std::string before = export_configuration(world.model().configuration());
    QSignalSpy finished(flow, SIGNAL(finished()));

    SECTION("duplicate ids and names")
    {
        std::string text = replace_first(sample_text(), "0000000000a2", "0000000000a1");
        text = replace_first(text, "Wrong Track Played", "Wrong Response");
        world.m_Rig.store().add_file("/files/bad.json", text);
        begin(world, flow, QStringLiteral("/files/bad.json"), QStringLiteral("bad.json"));

        REQUIRE(page(flow) == QStringLiteral("errors"));
        const QStringList shown = row_names(world, QStringLiteral("messageRow"));
        REQUIRE(shown.size() == 2);
        CHECK(shown.at(0) == QStringLiteral("Error type 2 uses an id that another one already has."));
        CHECK(shown.at(1) == QStringLiteral("Error types 1 and 3 are both called Wrong Response."));
        CHECK(item(world, QStringLiteral("messageGroup"))->property("title").toString() ==
              QStringLiteral("2 problems"));
    }
    SECTION("a future schema version")
    {
        world.m_Rig.store().add_file("/files/new.json",
                                     replace_first(sample_text(), "\"schema_version\": 1", "\"schema_version\": 7"));
        begin(world, flow, QStringLiteral("/files/new.json"), QStringLiteral("new.json"));

        REQUIRE(page(flow) == QStringLiteral("errors"));
        const QStringList shown = row_names(world, QStringLiteral("messageRow"));
        REQUIRE(shown.size() == 1);
        CHECK(shown.front().contains(QStringLiteral("newer"), Qt::CaseInsensitive));
        CHECK(item(world, QStringLiteral("messageGroup"))->property("title").toString() == QStringLiteral("1 problem"));
    }
    SECTION("a file that is not JSON, and one that is not there")
    {
        world.m_Rig.store().add_file("/files/broken.json", "{ \"schema_version\": ");
        begin(world, flow, QStringLiteral("/files/broken.json"), QStringLiteral("broken.json"));
        REQUIRE(page(flow) == QStringLiteral("errors"));
        CHECK(row_names(world, QStringLiteral("messageRow")).size() == 1);
        tap(world, QStringLiteral("cancelButton"));
        REQUIRE(page(flow) == QStringLiteral("none"));

        begin(world, flow, QStringLiteral("/files/missing.json"), QStringLiteral("missing.json"));
        REQUIRE(page(flow) == QStringLiteral("errors"));
        CHECK(row_names(world, QStringLiteral("messageRow")).size() == 1);
    }

    CHECK(errors(world)->isVisible());
    CHECK_FALSE(confirm(world)->isVisible());
    CHECK(item(world, QStringLiteral("nothingChanged"))->isVisible());
    CHECK(world.m_Rig.store().saves().empty());
    CHECK(export_configuration(world.model().configuration()) == before);

    // Cancel closes; the flow can be used again.
    tap(world, QStringLiteral("cancelButton"));
    CHECK(page(flow) == QStringLiteral("none"));
    CHECK(finished.count() >= 1);
    world.m_Rig.store().add_file("/files/good.json", sample_text());
    begin(world, flow, QStringLiteral("/files/good.json"), QStringLiteral("good.json"));
    CHECK(page(flow) == QStringLiteral("confirm"));
}

TEST_CASE("import.errors: Choose another file asks the shell for the file dialog again", "[ui][import]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* flow = world.load("ImportFlow");
    world.m_Rig.store().add_file("/files/bad.json", "not json");
    QSignalSpy requested(flow, SIGNAL(importRequested()));
    begin(world, flow, QStringLiteral("/files/bad.json"), QStringLiteral("bad.json"));
    REQUIRE(page(flow) == QStringLiteral("errors"));

    tap(world, QStringLiteral("chooseAnotherButton"));
    CHECK(requested.count() == 1);
    CHECK(page(flow) == QStringLiteral("none"));
    CHECK_FALSE(world.model().configured());
}

TEST_CASE("import.sample: the sample file imports and the board, the picker and the lists show it", "[ui][import]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* flow = world.load("ImportFlow");
    world.m_Rig.store().add_file("/files/field-test-setup.ejconfig", sample_text());
    begin(world, flow, QStringLiteral("/files/field-test-setup.ejconfig"));
    tap(world, QStringLiteral("replaceButton"));
    REQUIRE(world.model().configured());

    // The settings lists.
    CHECK(world.model().error_types()->rowCount() == 11);
    CHECK(world.model().locations()->rowCount() == 2);
    CHECK(world.model().templates()->rowCount() == 2);
    CHECK(world.model().tester() == QStringLiteral("Sam Example"));
    CHECK(world.model().unit_prefix() == QStringLiteral("UNIT"));

    // The board and the picker read the configuration that is now in force.
    test::FakeOpenLogSource open_log;
    viewmodel::BoardModel board(world.model(), open_log);
    REQUIRE(board.rowCount() == 12);
    const int label = board.roleNames().key("label");
    CHECK(board.data(board.index(0), label).toString() == QStringLiteral("Wrong Response"));
    CHECK(board.data(board.index(1), label).toString() == QStringLiteral("Sequence Out of Order"));
    CHECK(board.data(board.index(10), label).toString() == QStringLiteral("Navigation Failure"));

    viewmodel::ErrorPickerModel picker(world.model(), open_log, world.model());
    CHECK(picker.rowCount() >= 11);
}
