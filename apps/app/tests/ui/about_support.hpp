// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the tests of the About pages (settings_screen_test.cpp, app_flow_test.cpp, visual_capture_test.cpp) give the
// screens: an AboutModel over made-up facts and made-up notices, shaped as tools/licences/notices.py writes them. Only
// fictional names: the components are "Sample Library" and its friends, and the licence texts are short stand-ins.

#include "viewmodel/about_model.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace ej::test
{

inline viewmodel::AboutFacts about_facts()
{
    viewmodel::AboutFacts facts;
    facts.m_Name = QStringLiteral("CS Bingo Board");
    facts.m_Version = QStringLiteral("0.1.0");
    facts.m_BuildNumber = QStringLiteral("57");
    facts.m_Commit = QStringLiteral("a1b2c3d");
    facts.m_Dirty = false;
    facts.m_Ref = QStringLiteral("develop");
    facts.m_BuildDate = QStringLiteral("2026-11-09 03:21 UTC");
    facts.m_Platform = QStringLiteral("ios");
    facts.m_QtVersion = QStringLiteral("6.12.0");
    facts.m_Storage = QStringLiteral("sqlite");
    facts.m_SourceUrl = QStringLiteral("https://github.com/iainchesworthlabs/icl-error-journaling");
    return facts;
}

/// `count` paragraphs of hard-wrapped prose, each of three lines, as the text of a licence has them.
inline QString about_long_text(int count)
{
    QStringList blocks;
    for (int i = 1; i <= count; ++i)
    {
        blocks.append(
            QStringLiteral("%1. Permission is granted to use, copy and share this sample text under the terms\n"
                           "written here, as long as the notice stays with every copy and nobody is told that\n"
                           "the authors approve of what was done with it.")
                .arg(i));
    }
    return blocks.join(QStringLiteral("\n\n"));
}

inline QJsonObject about_component(const QString& id, const QString& name, const QString& version,
                                   const QString& licence, const QString& display, const QStringList& text_ids,
                                   const QStringList& exempt_ids = {},
                                   const QString& linking = QStringLiteral("static"))
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("version"), version},
        {QStringLiteral("kind"), QStringLiteral("qt")},
        {QStringLiteral("licence"), licence},
        {QStringLiteral("licenceDisplay"), display},
        {QStringLiteral("copyright"),
         QStringLiteral("Copyright (c) 2020 Sam Example\nCopyright (c) 2021 North Site Authors")},
        {QStringLiteral("source"), QStringLiteral("https://example.invalid/%1.tar.xz").arg(name.toLower())},
        {QStringLiteral("linking"), linking},
        {QStringLiteral("note"), QStringLiteral("Part of the sample set.")},
        {QStringLiteral("textIds"), QJsonArray::fromStringList(text_ids)},
        {QStringLiteral("exemptIds"), QJsonArray::fromStringList(exempt_ids)},
    };
}

/// The document of three components with the four kinds of text a page can hold: one licence, a choice of two, a
/// public-domain dedication that needs no text, and a text that is the component's own file.
inline QByteArray about_notices(int long_text_paragraphs = 6, const QString& base_name = QStringLiteral("Sample Base"))
{
    QJsonArray components;
    components.append(about_component(QStringLiteral("qt/qtbase"), base_name, QStringLiteral("6.12.0"),
                                      QStringLiteral("LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-3.0-only"),
                                      QStringLiteral("Qt commercial licence OR LGPL-3.0-only OR GPL-3.0-only"),
                                      {QStringLiteral("LGPL-3.0-only"), QStringLiteral("GPL-3.0-only")}));
    components.append(about_component(QStringLiteral("qt-3rdparty/pixel"),
                                      QStringLiteral("Pixel (in %1)").arg(base_name), QStringLiteral("1.2.3"),
                                      QStringLiteral("MIT"), QStringLiteral("MIT"), {QStringLiteral("MIT")}));
    components.append(about_component(QStringLiteral("qt-3rdparty/hash"), QStringLiteral("Hash (in %1)").arg(base_name),
                                      QString(), QStringLiteral("CC0-1.0"), QStringLiteral("CC0-1.0"), {},
                                      {QStringLiteral("CC0-1.0")}));
    QJsonObject own = about_component(QStringLiteral("vcpkg/sample-library"), QStringLiteral("sample-library"),
                                      QStringLiteral("1.0#1"), QStringLiteral("see the licence text"),
                                      QStringLiteral("see the licence text"), {}, {}, QStringLiteral("dynamic/static"));
    own.insert(QStringLiteral("kind"), QStringLiteral("vcpkg"));
    own.insert(QStringLiteral("copyright"), QString());
    own.insert(QStringLiteral("source"), QString());
    own.insert(QStringLiteral("note"), QString());
    components.append(own);

    const QJsonObject texts{
        {QStringLiteral("GPL-3.0-or-later"), about_long_text(long_text_paragraphs)},
        {QStringLiteral("GPL-3.0-only"), about_long_text(long_text_paragraphs)},
        {QStringLiteral("LGPL-3.0-only"),
         QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n\n") +
             about_long_text(long_text_paragraphs)},
        {QStringLiteral("MIT"),
         QStringLiteral("Permission is hereby granted, free of charge, to any person obtaining a copy of\n"
                        "this software and associated documentation files.")},
        {QStringLiteral("vcpkg/sample-library"), QStringLiteral("Sample Library terms.\n\nNo warranty.")},
    };
    const QJsonObject document{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("project"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("CS Bingo Board")},
                     {QStringLiteral("licence"), QStringLiteral("GPL-3.0-or-later")},
                     {QStringLiteral("copyright"), QStringLiteral("Copyright (C) Iain Chesworth Labs contributors")}}},
        {QStringLiteral("components"), components},
        {QStringLiteral("texts"), texts},
        {QStringLiteral("exemptions"),
         QJsonObject{{QStringLiteral("CC0-1.0"), QStringLiteral("A public-domain dedication: it sets no condition.")}}},
    };
    return QJsonDocument(document).toJson(QJsonDocument::Compact);
}

} // namespace ej::test
