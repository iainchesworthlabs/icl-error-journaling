// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop file chooser (docs/plan/stage-4.md, 4.15, decision 17): what a dialog gives back, a URL, becomes the local path
// that the import and export take and the name that the confirmation shows. No dialog is opened: it would wait for a person.
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QMetaObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QUrl>
#include <QVariant>

using namespace ej::ui;

namespace
{

QString call(QQuickItem* chooser, const char* method, const QUrl& file)
{
    QString result;
    REQUIRE(QMetaObject::invokeMethod(chooser, method, Q_RETURN_ARG(QString, result), Q_ARG(QUrl, file)));
    return result;
}

} // namespace

TEST_CASE("chooser.local_path: a file URL becomes the path the import takes, on either kind of file system", "[ui][chooser]")
{
    UiRig rig;
    QQuickItem* chooser = rig.load(QStringLiteral("FileChooser"));
    INFO(rig.errors().toStdString());
    REQUIRE(chooser != nullptr);

    CHECK(call(chooser, "localPath", QUrl(QStringLiteral("file:///C:/data/cs-bingo-board-setup.ejconfig"))) ==
          QStringLiteral("C:/data/cs-bingo-board-setup.ejconfig"));
    CHECK(call(chooser, "localPath", QUrl(QStringLiteral("file:///home/sam/setup.json"))) ==
          QStringLiteral("/home/sam/setup.json"));
    // A space and a letter that is not ASCII arrive percent-encoded and leave it.
    CHECK(call(chooser, "localPath", QUrl(QStringLiteral("file:///home/sam/My%20setup.json"))) ==
          QStringLiteral("/home/sam/My setup.json"));
    CHECK(call(chooser, "localPath", QUrl(QStringLiteral("file:///home/s%C3%A1m/a.json"))) ==
          QStringLiteral("/home/s\u00E1m/a.json"));
    // Something that is not a file URL is not changed.
    CHECK(call(chooser, "localPath", QUrl(QStringLiteral("/already/a/path.json"))) == QStringLiteral("/already/a/path.json"));
}

TEST_CASE("chooser.display_name: the name of the file is what follows the last slash", "[ui][chooser]")
{
    UiRig rig;
    QQuickItem* chooser = rig.load(QStringLiteral("FileChooser"));
    REQUIRE(chooser != nullptr);

    CHECK(call(chooser, "displayName", QUrl(QStringLiteral("file:///C:/data/cs-bingo-board-setup.ejconfig"))) ==
          QStringLiteral("cs-bingo-board-setup.ejconfig"));
    CHECK(call(chooser, "displayName", QUrl(QStringLiteral("file:///home/sam/My%20setup.json"))) ==
          QStringLiteral("My setup.json"));
    CHECK(call(chooser, "displayName", QUrl(QStringLiteral("file:///setup.json"))) == QStringLiteral("setup.json"));
}

TEST_CASE("chooser.signals: a chosen file to open and a chosen place to save are each one signal", "[ui][chooser]")
{
    UiRig rig;
    QQuickItem* chooser = rig.load(QStringLiteral("FileChooser"));
    REQUIRE(chooser != nullptr);
    QSignalSpy opened(chooser, SIGNAL(openChosen(QString, QString)));
    QSignalSpy saved(chooser, SIGNAL(saveChosen(QString)));

    REQUIRE(QMetaObject::invokeMethod(chooser, "receiveOpen", Q_ARG(QUrl, QUrl(QStringLiteral("file:///C:/data/a.json")))));
    REQUIRE(opened.count() == 1);
    CHECK(opened.at(0).at(0).toString() == QStringLiteral("C:/data/a.json"));
    CHECK(opened.at(0).at(1).toString() == QStringLiteral("a.json"));
    CHECK(saved.count() == 0);

    REQUIRE(QMetaObject::invokeMethod(chooser, "receiveSave",
                                      Q_ARG(QUrl, QUrl(QStringLiteral("file:///C:/data/out.ejconfig")))));
    REQUIRE(saved.count() == 1);
    CHECK(saved.at(0).at(0).toString() == QStringLiteral("C:/data/out.ejconfig"));
    CHECK(opened.count() == 1);
}

TEST_CASE("chooser.filters: the dialogs offer the setup files and save as .ejconfig", "[ui][chooser]")
{
    UiRig rig;
    QQuickItem* chooser = rig.load(QStringLiteral("FileChooser"));
    REQUIRE(chooser != nullptr);

    CHECK(chooser->property("nameFilters").toStringList() ==
          QStringList{QStringLiteral("Setup files (*.ejconfig *.json)")});
    CHECK(chooser->property("defaultSuffix").toString() == QStringLiteral("ejconfig"));
}
