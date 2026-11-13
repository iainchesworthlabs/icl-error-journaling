// SPDX-License-Identifier: GPL-3.0-or-later
// The versions of Today's details besides the robot's (HDR-10) and the "v" of the version field (HDR-04): the fixed v in
// front of the field, the Other versions row and the sheet it opens (add, edit, remove, refusals, Done and Cancel). The
// models are the real ones of Stage 3 over the in-memory store.
#include "header_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QQuickItem>
#include <QVariantList>
#include <QVariantMap>

using namespace ej::ui;
using namespace ej::ui::header;

namespace
{

const QSize TALL_WINDOW(402, 1600);

QQuickItem* load_screen(UiRig& rig, HeaderWorld& world)
{
    world.attach(rig);
    QQuickItem* screen = rig.load(QStringLiteral("HeaderScreen"), world.models());
    INFO(rig.errors().toStdString());
    REQUIRE(screen != nullptr);
    rig.settle();
    return screen;
}

QQuickItem* open_sheet(UiRig& rig)
{
    rig.tap(need(rig, QStringLiteral("headerOtherVersions")));
    play(rig, 500);
    rig.settle();
    QQuickItem* sheet = need(rig, QStringLiteral("extraVersionsSheet"));
    REQUIRE(sheet->property("isOpen").toBool());
    return sheet;
}

/// Every item under `from` with this objectName, in the order of the tree.
void collect(QQuickItem* from, const QString& name, QList<QQuickItem*>& out)
{
    if (from == nullptr)
    {
        return;
    }
    if (from->objectName() == name)
    {
        out.append(from);
    }
    for (QQuickItem* child : from->childItems())
    {
        collect(child, name, out);
    }
}

QList<QQuickItem*> all(QQuickItem* from, const char* name)
{
    QList<QQuickItem*> found;
    collect(from, QString::fromLatin1(name), found);
    return found;
}

QQuickItem* nth(QQuickItem* sheet, const char* name, int index)
{
    const QList<QQuickItem*> found = all(sheet, name);
    REQUIRE(index < found.size());
    return found.at(index);
}

QString shown(UiRig& rig, QQuickItem* form_field)
{
    return input_of(rig, form_field)->property("text").toString();
}

QVariantMap row_of(const char* label, const char* version)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), QString::fromUtf8(label));
    row.insert(QStringLiteral("version"), QString::fromUtf8(version));
    row.insert(QStringLiteral("commit"), QString());
    return row;
}

} // namespace

TEST_CASE("versions.prefix: the version field shows a fixed v and takes the numbers", "[ui][extra_versions]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* field = need(rig, QStringLiteral("headerVersion"));

    CHECK(field->property("prefixText").toString() == QStringLiteral("v"));
    CHECK(field->property("label").toString() == QStringLiteral("Robot software version"));
    CHECK(shown(rig, field) == QStringLiteral("1.4.2"));

    retype(rig, field, QStringLiteral("1.4.3"));
    CHECK(world.session().version_text() == QStringLiteral("1.4.3"));
    CHECK(world.session().version_display_text() == QStringLiteral("v1.4.3"));

    // A v that is typed anyway is accepted and not doubled: the field is given the numbers back.
    retype(rig, field, QStringLiteral("v1.5.0"));
    CHECK(world.session().version_text() == QStringLiteral("1.5.0"));
    CHECK(world.session().version_display_text() == QStringLiteral("v1.5.0"));
    CHECK(shown(rig, field) == QStringLiteral("1.5.0"));
}

TEST_CASE("versions.row: the Other versions row says None, and the value follows the log", "[ui][extra_versions]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* row = need(rig, QStringLiteral("headerOtherVersions"));

    CHECK(row->property("text").toString() == QStringLiteral("Other versions"));
    CHECK(row->property("value").toString() == QStringLiteral("None"));
    CHECK(row->property("showChevron").toBool());
    CHECK(row->height() >= 44.0);
    CHECK_FALSE(need(rig, QStringLiteral("extraVersionsSheet"))->property("isOpen").toBool());
    // It sits under the commit and above the new-log group.
    CHECK(rig.geometry(row).top() >= rig.geometry(need(rig, QStringLiteral("headerCommit"))).bottom());
    CHECK(rig.geometry(row).bottom() <= rig.geometry(need(rig, QStringLiteral("headerNewLog"))).top());

    QVariantList rows;
    rows.append(row_of("Influencer", "4.5.0"));
    REQUIRE(world.session().correctExtraVersions(rows));
    world.gateway().run_all();
    rig.settle();
    CHECK(row->property("value").toString() == QStringLiteral("Influencer v4.5.0"));
}

TEST_CASE("versions.sheet: a version is added, named and saved by Done", "[ui][extra_versions]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* sheet = open_sheet(rig);

    CHECK(all(sheet, "extraVersionCard").isEmpty());
    rig.tap(need(rig, QStringLiteral("extraVersionAdd"), sheet));
    rig.settle();
    REQUIRE(all(sheet, "extraVersionCard").size() == 1);
    CHECK(nth(sheet, "extraVersionNumber", 0)->property("prefixText").toString() == QStringLiteral("v"));

    retype(rig, nth(sheet, "extraVersionLabel", 0), QStringLiteral("Influencer"));
    retype(rig, nth(sheet, "extraVersionNumber", 0), QStringLiteral("4.5.0"));
    CHECK_FALSE(sheet->property("refused").toBool());
    // Nothing is saved before Done.
    CHECK(world.session().extra_versions().isEmpty());

    rig.tap(need(rig, QStringLiteral("navTrailing"), sheet));
    play(rig, 500);
    world.gateway().run_all();
    rig.settle();
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.session().extra_versions_text() == QStringLiteral("Influencer v4.5.0"));
    CHECK(world.timeline().entry_count() == 0);
}

TEST_CASE("versions.sheet: a refused row says why, Done waits, and Cancel changes nothing", "[ui][extra_versions]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* sheet = open_sheet(rig);

    rig.tap(need(rig, QStringLiteral("extraVersionAdd"), sheet));
    rig.settle();
    retype(rig, nth(sheet, "extraVersionLabel", 0), QStringLiteral("Robot"));
    retype(rig, nth(sheet, "extraVersionNumber", 0), QStringLiteral("4.5.0"));
    CHECK(sheet->property("refused").toBool());
    CHECK(nth(sheet, "extraVersionLabel", 0)->property("invalid").toBool());
    CHECK_FALSE(nth(sheet, "extraVersionNumber", 0)->property("invalid").toBool());

    retype(rig, nth(sheet, "extraVersionLabel", 0), QStringLiteral("Influencer"));
    retype(rig, nth(sheet, "extraVersionNumber", 0), QStringLiteral("4.5"));
    CHECK(sheet->property("refused").toBool());
    CHECK(nth(sheet, "extraVersionNumber", 0)->property("invalid").toBool());
    CHECK_FALSE(nth(sheet, "extraVersionLabel", 0)->property("invalid").toBool());

    rig.tap(need(rig, QStringLiteral("navLeading"), sheet));
    play(rig, 500);
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.session().extra_versions().isEmpty());
    CHECK(world.session().pending_writes() == 0);
}

TEST_CASE("versions.sheet: the sheet starts from what the log has; a version is edited and another removed",
          "[ui][extra_versions]")
{
    HeaderWorld world(true);
    REQUIRE(world.session().correctHeader(QStringLiteral("extra_versions"),
                                          QStringLiteral("Influencer=4.5.0\nGateway=2.0.1")));
    world.gateway().run_all();
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* sheet = open_sheet(rig);

    REQUIRE(all(sheet, "extraVersionCard").size() == 2);
    CHECK(shown(rig, nth(sheet, "extraVersionLabel", 1)) == QStringLiteral("Gateway"));
    CHECK(shown(rig, nth(sheet, "extraVersionNumber", 0)) == QStringLiteral("4.5.0"));

    retype(rig, nth(sheet, "extraVersionNumber", 0), QStringLiteral("v4.6.0"));
    rig.tap(nth(sheet, "extraVersionRemove", 1));
    rig.settle();
    CHECK(all(sheet, "extraVersionCard").size() == 1);

    rig.tap(need(rig, QStringLiteral("navTrailing"), sheet));
    play(rig, 500);
    world.gateway().run_all();
    rig.settle();
    CHECK(world.session().extra_versions_text() == QStringLiteral("Influencer v4.6.0"));
}
