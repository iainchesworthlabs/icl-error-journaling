// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

namespace ej::viewmodel
{

/// What the About page says about this build, filled in by the composition root (main.cpp) from the generated build
/// information, the bridge and Qt.
struct AboutFacts
{
    QString m_Name;        ///< "Field Test".
    QString m_Version;     ///< "0.1.0".
    QString m_BuildNumber; ///< The iOS build number (CFBundleVersion), else the CI run number, else "unknown".
    QString m_Commit;      ///< The short hash of the commit, or "unknown".
    bool m_Dirty = false;  ///< A tracked file differed from that commit when the program was built.
    QString m_Ref;         ///< The tag or branch; empty when neither is known.
    QString m_BuildDate;   ///< The date of the commit in UTC, or $SOURCE_DATE_EPOCH, or "unknown".
    QString m_Platform;    ///< "ios", or "desktop-stub" where the bridge is a stub.
    QString m_Storage;     ///< The storage backend, such as "sqlite".
    QString m_QtVersion;   ///< The Qt the program is running on.
    QString m_SourceUrl;   ///< Where the source code is.
};

/// The model of the About pages (docs/prd.md PRF-12): the facts of the build, the licence of the app, the link to its
/// source, and the list of the third-party components that the iPhone binary links, each with its licence text.
///
/// The components come from the notices that the build generates from the real link line
/// (tools/licences/notices.py, embedded as the resource :/about/third-party-notices.json), so the list cannot say
/// something the build did not do. `notices` is called once, the first time a component or a licence is asked for, and
/// not at all if the About pages are never opened. A document that cannot be read leaves the list empty and `problem`
/// says so in words; the facts of the build are shown regardless.
///
/// A row of the component list is `{id, name, version, licence, linking}`; `licence(id)` returns the page of one:
/// `{name, version, licence, copyright, source, linking, note, texts}`, where each of `texts` is
/// `{id, title, paragraphs}`. The text of a licence is split into paragraphs, with the line breaks of the file's
/// hard-wrapped prose joined, so that it wraps to the width of the screen and a screen reader moves by paragraph.
/// `id` "app" is the licence of the application itself.
///
/// QtCore only. GUI thread only.
class AboutModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT)
    Q_PROPERTY(QString commit READ commit CONSTANT)
    Q_PROPERTY(QString ref READ ref CONSTANT)
    Q_PROPERTY(QString buildDate READ buildDate CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(QString storage READ storage CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString sourceUrl READ sourceUrl CONSTANT)
    Q_PROPERTY(QString licenceName READ licenceName CONSTANT)
    Q_PROPERTY(QVariantList components READ components CONSTANT)
    Q_PROPERTY(QString problem READ problem CONSTANT)

public:
    /// The bytes of third-party-notices.json. Called at most once.
    using NoticesSource = std::function<QByteArray()>;

    AboutModel(AboutFacts facts, NoticesSource notices, QObject* parent = nullptr);
    ~AboutModel() override;

    [[nodiscard]] QString appName() const;
    [[nodiscard]] QString version() const;
    [[nodiscard]] QString buildNumber() const;
    /// The commit, with " (modified)" after it when the tree was not clean.
    [[nodiscard]] QString commit() const;
    [[nodiscard]] QString ref() const;
    [[nodiscard]] QString buildDate() const;
    [[nodiscard]] QString platform() const;
    [[nodiscard]] QString storage() const;
    [[nodiscard]] QString qtVersion() const;
    [[nodiscard]] QString sourceUrl() const;
    /// The SPDX identifier of the application's own licence.
    [[nodiscard]] QString licenceName() const;
    [[nodiscard]] QVariantList components() const;
    /// Empty when the notices were read; otherwise what is missing, in words.
    [[nodiscard]] QString problem() const;

    /// The page of the licence of "app" or of the component `id`; empty for an id that is not known.
    Q_INVOKABLE [[nodiscard]] QVariantMap licence(const QString& id) const;

    /// Splits a licence text into paragraphs: blocks are separated by blank lines, and the lines of a block of
    /// hard-wrapped prose are joined with a space. A block of short lines (a list of copyright lines, a table) keeps
    /// its line breaks.
    [[nodiscard]] static QStringList paragraphs(const QString& text);

private:
    void load() const;
    [[nodiscard]] QVariantList text_pages(const QJsonObject& component) const;

    AboutFacts m_Facts;
    NoticesSource m_Source;
    mutable bool m_Loaded = false;
    mutable QString m_Problem;
    mutable QJsonObject m_Document;
    mutable QVariantList m_Components;
};

} // namespace ej::viewmodel
