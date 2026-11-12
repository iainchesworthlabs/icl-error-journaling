// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/about_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

using namespace ej;
using viewmodel::AboutFacts;
using viewmodel::AboutModel;

namespace
{

AboutFacts facts()
{
    AboutFacts value;
    value.m_Name = QStringLiteral("Field Test");
    value.m_Version = QStringLiteral("0.1.0");
    value.m_BuildNumber = QStringLiteral("57");
    value.m_Commit = QStringLiteral("a1b2c3d");
    value.m_Ref = QStringLiteral("develop");
    value.m_BuildDate = QStringLiteral("2026-11-09 03:21 UTC");
    value.m_Platform = QStringLiteral("ios");
    value.m_Storage = QStringLiteral("sqlite");
    value.m_QtVersion = QStringLiteral("6.12.0");
    value.m_SourceUrl = QStringLiteral("https://example.invalid/source");
    return value;
}

QJsonObject component(const QString& id, const QString& name, const QStringList& text_ids,
                      const QStringList& exempt_ids = {})
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("version"), QStringLiteral("1.2")},
                       {QStringLiteral("licence"), QStringLiteral("MIT")},
                       {QStringLiteral("licenceDisplay"), QStringLiteral("MIT or similar")},
                       {QStringLiteral("copyright"), QStringLiteral("Copyright (c) 2020 Sam Example")},
                       {QStringLiteral("source"), QStringLiteral("https://example.invalid/%1").arg(name)},
                       {QStringLiteral("linking"), QStringLiteral("static")},
                       {QStringLiteral("note"), QStringLiteral("Part of the sample set.")},
                       {QStringLiteral("textIds"), QJsonArray::fromStringList(text_ids)},
                       {QStringLiteral("exemptIds"), QJsonArray::fromStringList(exempt_ids)}};
}

QByteArray notices()
{
    const QJsonObject document{
        {QStringLiteral("project"),
         QJsonObject{{QStringLiteral("licence"), QStringLiteral("GPL-3.0-or-later")},
                     {QStringLiteral("copyright"), QStringLiteral("Copyright (C) Sample Authors")}}},
        {QStringLiteral("components"),
         QJsonArray{component(QStringLiteral("a"), QStringLiteral("Alpha"), {QStringLiteral("MIT")}),
                    component(QStringLiteral("b"), QStringLiteral("Beta"), {}, {QStringLiteral("CC0-1.0")}),
                    component(QStringLiteral("vcpkg/c"), QStringLiteral("Gamma"), {})}},
        {QStringLiteral("texts"),
         QJsonObject{
             {QStringLiteral("MIT"), QStringLiteral("Permission is hereby granted, free of charge, to any person\n"
                                                    "obtaining a copy of this software.\n\nNo warranty.")},
             {QStringLiteral("GPL-3.0-or-later"), QStringLiteral("The program text.")},
             {QStringLiteral("vcpkg/c"), QStringLiteral("Gamma's own file.")}}},
        {QStringLiteral("exemptions"), QJsonObject{{QStringLiteral("CC0-1.0"), QStringLiteral("No conditions.")}}},
    };
    return QJsonDocument(document).toJson(QJsonDocument::Compact);
}

} // namespace

TEST_CASE("The facts of the build are what the composition root gave", "[viewmodel][about]")
{
    const AboutModel model(facts(), [] { return QByteArray(); });
    CHECK(model.appName() == "Field Test");
    CHECK(model.version() == "0.1.0");
    CHECK(model.buildNumber() == "57");
    CHECK(model.commit() == "a1b2c3d");
    CHECK(model.ref() == "develop");
    CHECK(model.buildDate() == "2026-11-09 03:21 UTC");
    CHECK(model.platform() == "ios");
    CHECK(model.storage() == "sqlite");
    CHECK(model.qtVersion() == "6.12.0");
    CHECK(model.sourceUrl() == "https://example.invalid/source");
}

TEST_CASE("A commit built from a tree with changes says so, and an unknown commit does not", "[viewmodel][about]")
{
    AboutFacts dirty = facts();
    dirty.m_Dirty = true;
    CHECK(AboutModel(dirty, nullptr).commit() == "a1b2c3d (modified)");
    dirty.m_Commit = QStringLiteral("unknown");
    CHECK(AboutModel(dirty, nullptr).commit() == "unknown");
}

TEST_CASE("The notices are read when they are first wanted, and once", "[viewmodel][about]")
{
    int reads = 0;
    const AboutModel model(facts(),
                           [&reads]
                           {
                               ++reads;
                               return notices();
                           });
    CHECK(model.appName() == "Field Test");
    CHECK(model.sourceUrl() == "https://example.invalid/source");
    CHECK(reads == 0);
    CHECK(model.components().size() == 3);
    CHECK(model.licenceName() == "GPL-3.0-or-later");
    CHECK(model.problem().isEmpty());
    CHECK(reads == 1);
}

TEST_CASE("A component of the list has its name, version, licence and how it is linked", "[viewmodel][about]")
{
    const AboutModel model(facts(), notices);
    const QVariantList list = model.components();
    REQUIRE(list.size() == 3);
    const QVariantMap first = list.at(0).toMap();
    CHECK(first.value("id") == "a");
    CHECK(first.value("name") == "Alpha");
    CHECK(first.value("version") == "1.2");
    CHECK(first.value("licence") == "MIT or similar");
    CHECK(first.value("linking") == "static");
    CHECK(list.at(2).toMap().value("name") == "Gamma");
}

TEST_CASE("The page of a licence holds the facts and the text of each licence it is offered under",
          "[viewmodel][about]")
{
    const AboutModel model(facts(), notices);
    const QVariantMap page = model.licence(QStringLiteral("a"));
    CHECK(page.value("name") == "Alpha");
    CHECK(page.value("copyright") == "Copyright (c) 2020 Sam Example");
    CHECK(page.value("source") == "https://example.invalid/Alpha");
    CHECK(page.value("note") == "Part of the sample set.");
    const QVariantList texts = page.value("texts").toList();
    REQUIRE(texts.size() == 1);
    const QVariantMap mit = texts.at(0).toMap();
    CHECK(mit.value("id") == "MIT");
    CHECK(mit.value("title") == "MIT");
    CHECK(mit.value("paragraphs").toStringList() ==
          QStringList{"Permission is hereby granted, free of charge, to any person obtaining a copy of this software.",
                      "No warranty."});
}

TEST_CASE("A public-domain dedication and a component's own file have a page too", "[viewmodel][about]")
{
    const AboutModel model(facts(), notices);
    const QVariantList dedication = model.licence(QStringLiteral("b")).value("texts").toList();
    REQUIRE(dedication.size() == 1);
    CHECK(dedication.at(0).toMap().value("title") == "CC0-1.0");
    CHECK(dedication.at(0).toMap().value("paragraphs").toStringList() == QStringList{"No conditions."});
    const QVariantList own = model.licence(QStringLiteral("vcpkg/c")).value("texts").toList();
    REQUIRE(own.size() == 1);
    CHECK(own.at(0).toMap().value("paragraphs").toStringList() == QStringList{"Gamma's own file."});
}

TEST_CASE("The licence of the app is the licence of the project, with its text", "[viewmodel][about]")
{
    const AboutModel model(facts(), notices);
    const QVariantMap page = model.licence(QStringLiteral("app"));
    CHECK(page.value("name") == "Field Test");
    CHECK(page.value("licence") == "GPL-3.0-or-later");
    CHECK(page.value("copyright") == "Copyright (C) Sample Authors");
    CHECK(page.value("source") == "https://example.invalid/source");
    const QVariantList texts = page.value("texts").toList();
    REQUIRE(texts.size() == 1);
    CHECK(texts.at(0).toMap().value("paragraphs").toStringList() == QStringList{"The program text."});
}

TEST_CASE("An id that is not in the list has no page", "[viewmodel][about]")
{
    const AboutModel model(facts(), notices);
    CHECK(model.licence(QStringLiteral("nothing")).isEmpty());
    CHECK(model.licence(QString()).isEmpty());
}

TEST_CASE("Notices that cannot be read leave the facts and say so in words", "[viewmodel][about]")
{
    for (const QByteArray& bytes :
         {QByteArray(), QByteArray("not json"), QByteArray("[1, 2]"), QByteArray("{\"components\": []}")})
    {
        INFO(bytes.toStdString());
        const AboutModel model(facts(), [bytes] { return bytes; });
        CHECK(model.components().isEmpty());
        CHECK_FALSE(model.problem().isEmpty());
        CHECK(model.version() == "0.1.0");
        CHECK(model.licenceName() == "GPL-3.0-or-later");
        CHECK(model.licence(QStringLiteral("app")).value("name") == "Field Test");
    }
    CHECK_FALSE(AboutModel(facts(), nullptr).problem().isEmpty());
}

TEST_CASE("A licence text is split into paragraphs: prose is joined, a list keeps its lines", "[viewmodel][about]")
{
    const QString prose = QStringLiteral("GNU GENERAL PUBLIC LICENSE\n"
                                         "Version 3, 29 June 2007\n"
                                         "\n"
                                         "Everyone is permitted to copy and distribute verbatim copies\n"
                                         "of this license document,   but changing it is not allowed.\n"
                                         "\n"
                                         "\n"
                                         "  a) first\n"
                                         "  b) second\n"
                                         "\r\n"
                                         "Last paragraph.\n");
    CHECK(AboutModel::paragraphs(prose) ==
          QStringList{"GNU GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007",
                      "Everyone is permitted to copy and distribute verbatim copies of this license document, but "
                      "changing it is not allowed.",
                      "a) first\nb) second", "Last paragraph."});
    CHECK(AboutModel::paragraphs(QString()).isEmpty());
    CHECK(AboutModel::paragraphs(QStringLiteral("\n\n  \n")).isEmpty());
}
