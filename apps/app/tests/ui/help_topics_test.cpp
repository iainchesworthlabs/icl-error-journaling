// SPDX-License-Identifier: GPL-3.0-or-later
// HelpTopics (Stage 10, 10.6): the topics of the in-app help and the addresses of the guide that go with them.
// `tools/ci/check_help_links.py` holds the pages and headings to the guide; this holds the shape of the data in the
// app.
#include "input_components_support.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

using namespace ej::ui;
using namespace ej::ui::input;

namespace
{

constexpr auto PROBE = R"(
    Item {
        objectName: "probe"
        property var topics: HelpTopics.topics
        property string site: HelpTopics.siteUrl
        property string board: HelpTopics.topicAddress("board")
        property string today: HelpTopics.topicAddress("details")
        property string unknown: HelpTopics.topicAddress("noSuchTopic")
        property bool noneFound: HelpTopics.topic("noSuchTopic") === null
        property string foundTitle: HelpTopics.topic("review").title
        property string guide: HelpTopics.guideAddress
        property string support: HelpTopics.supportAddress
        property string privacy: HelpTopics.privacyAddress
        property string fix: HelpTopics.setupFileFixAddress
    })";

} // namespace

TEST_CASE("help.topics: seven topics, each with an id, a title and two to five paragraphs of text",
          "[ui][help][help_topics]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromLatin1(PROBE)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* probe = require_item(rig, QStringLiteral("probe"));
    const QVariantList topics = probe->property("topics").toList();
    REQUIRE(topics.size() == 7);

    QSet<QString> ids;
    QSet<QString> titles;
    for (const QVariant& value : topics)
    {
        const QVariantMap topic = value.toMap();
        const QString id = topic.value(QStringLiteral("id")).toString();
        INFO(id.toStdString());
        CHECK_FALSE(id.isEmpty());
        CHECK_FALSE(ids.contains(id));
        ids.insert(id);
        const QString title = topic.value(QStringLiteral("title")).toString();
        CHECK_FALSE(title.isEmpty());
        CHECK_FALSE(titles.contains(title));
        titles.insert(title);
        const QVariantList paragraphs = topic.value(QStringLiteral("paragraphs")).toList();
        CHECK(paragraphs.size() >= 2);
        CHECK(paragraphs.size() <= 5);
        for (const QVariant& paragraph : paragraphs)
        {
            // Plain text and the <b> of a button's name: no other markup, no address.
            const QString text = paragraph.toString();
            CHECK_FALSE(text.trimmed().isEmpty());
            CHECK_FALSE(text.contains(QStringLiteral("http")));
            QString without_bold = text;
            without_bold.remove(QStringLiteral("<b>")).remove(QStringLiteral("</b>"));
            CHECK_FALSE(without_bold.contains(QLatin1Char('<')));
        }
        CHECK(topic.value(QStringLiteral("page")).toString().endsWith(QLatin1Char('/')));
    }
    // The topics of the table of entry points, in the order of the Quick help list.
    CHECK(ids.contains(QStringLiteral("logging")));
    CHECK(ids.contains(QStringLiteral("board")));
    CHECK(ids.contains(QStringLiteral("errorTypes")));
    CHECK(ids.contains(QStringLiteral("templates")));
    CHECK(ids.contains(QStringLiteral("details")));
    CHECK(ids.contains(QStringLiteral("setupFile")));
    CHECK(ids.contains(QStringLiteral("review")));
}

TEST_CASE("help.topics: addresses are the site, then the page, then the heading, and the site is named once",
          "[ui][help][help_topics]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromLatin1(PROBE)) != nullptr);
    QQuickItem* probe = require_item(rig, QStringLiteral("probe"));
    const QString site = probe->property("site").toString();
    CHECK(site.startsWith(QStringLiteral("https://")));
    CHECK(site.endsWith(QLatin1Char('/')));

    CHECK(probe->property("board").toString() == site + QStringLiteral("guide/setup/#your-board"));
    // A topic that names no heading opens its page.
    CHECK(probe->property("today").toString() == site + QStringLiteral("guide/todays-details/"));
    // An id that is not a topic opens the guide's home, and finds no topic.
    CHECK(probe->property("unknown").toString() == site);
    CHECK(probe->property("noneFound").toBool());
    CHECK(probe->property("foundTitle").toString() == QStringLiteral("Review and sharing"));

    CHECK(probe->property("guide").toString() == site);
    CHECK(probe->property("support").toString() == site + QStringLiteral("support/"));
    CHECK(probe->property("privacy").toString() == site + QStringLiteral("privacy/"));
    CHECK(probe->property("fix").toString() == site + QStringLiteral("guide/setup-file/#when-the-app-refuses-a-file"));
}
