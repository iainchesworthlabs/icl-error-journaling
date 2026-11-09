// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/about_model.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <utility>

namespace ej::viewmodel
{

namespace
{

constexpr auto APP_ID = "app";
/// A block with a line this long is hard-wrapped prose; a block of shorter lines is a list.
constexpr qsizetype PROSE_LINE_LENGTH = 50;
constexpr auto DEFAULT_LICENCE = "GPL-3.0-or-later";

} // namespace

AboutModel::AboutModel(AboutFacts facts, NoticesSource notices, QObject* parent)
    : QObject(parent), m_Facts(std::move(facts)), m_Source(std::move(notices))
{
}

AboutModel::~AboutModel() = default;

QString AboutModel::appName() const
{
    return m_Facts.m_Name;
}

QString AboutModel::version() const
{
    return m_Facts.m_Version;
}

QString AboutModel::buildNumber() const
{
    return m_Facts.m_BuildNumber;
}

QString AboutModel::commit() const
{
    if (m_Facts.m_Dirty && m_Facts.m_Commit != QLatin1String("unknown"))
    {
        return tr("%1 (modified)").arg(m_Facts.m_Commit);
    }
    return m_Facts.m_Commit;
}

QString AboutModel::ref() const
{
    return m_Facts.m_Ref;
}

QString AboutModel::buildDate() const
{
    return m_Facts.m_BuildDate;
}

QString AboutModel::platform() const
{
    return m_Facts.m_Platform;
}

QString AboutModel::storage() const
{
    return m_Facts.m_Storage;
}

QString AboutModel::qtVersion() const
{
    return m_Facts.m_QtVersion;
}

QString AboutModel::sourceUrl() const
{
    return m_Facts.m_SourceUrl;
}

QString AboutModel::licenceName() const
{
    load();
    const QString named =
        m_Document.value(QLatin1String("project")).toObject().value(QLatin1String("licence")).toString();
    return named.isEmpty() ? QString::fromLatin1(DEFAULT_LICENCE) : named;
}

QVariantList AboutModel::components() const
{
    load();
    return m_Components;
}

QString AboutModel::problem() const
{
    load();
    return m_Problem;
}

void AboutModel::load() const
{
    if (m_Loaded)
    {
        return;
    }
    m_Loaded = true;
    const QByteArray bytes = m_Source ? m_Source() : QByteArray();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (bytes.isEmpty() || error.error != QJsonParseError::NoError || !document.isObject())
    {
        m_Problem = tr("The list of open-source components could not be read.");
        return;
    }
    m_Document = document.object();
    const QJsonArray list = m_Document.value(QLatin1String("components")).toArray();
    for (const QJsonValue& value : list)
    {
        const QJsonObject component = value.toObject();
        m_Components.append(QVariantMap{
            {QStringLiteral("id"), component.value(QLatin1String("id")).toString()},
            {QStringLiteral("name"), component.value(QLatin1String("name")).toString()},
            {QStringLiteral("version"), component.value(QLatin1String("version")).toString()},
            {QStringLiteral("licence"), component.value(QLatin1String("licenceDisplay")).toString()},
            {QStringLiteral("linking"), component.value(QLatin1String("linking")).toString()},
        });
    }
    if (m_Components.isEmpty())
    {
        m_Problem = tr("The list of open-source components is empty.");
    }
}

QVariantList AboutModel::text_pages(const QJsonObject& component) const
{
    const QJsonObject texts = m_Document.value(QLatin1String("texts")).toObject();
    const QJsonObject exemptions = m_Document.value(QLatin1String("exemptions")).toObject();
    QVariantList pages;
    for (const QJsonValue& identifier : component.value(QLatin1String("textIds")).toArray())
    {
        const QString id = identifier.toString();
        const QString text = texts.value(id).toString();
        pages.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), id},
            {QStringLiteral("paragraphs"), paragraphs(text.isEmpty() ? tr("The text is missing.") : text)},
        });
    }
    const QString own = texts.value(component.value(QLatin1String("id")).toString()).toString();
    if (!own.isEmpty())
    {
        pages.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("licence-file")},
            {QStringLiteral("title"), tr("Licence file")},
            {QStringLiteral("paragraphs"), paragraphs(own)},
        });
    }
    for (const QJsonValue& identifier : component.value(QLatin1String("exemptIds")).toArray())
    {
        const QString id = identifier.toString();
        pages.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("title"), id},
            {QStringLiteral("paragraphs"), QStringList{exemptions.value(id).toString()}},
        });
    }
    return pages;
}

QVariantMap AboutModel::licence(const QString& id) const
{
    load();
    if (id == QLatin1String(APP_ID))
    {
        const QJsonObject project = m_Document.value(QLatin1String("project")).toObject();
        const QString expression = licenceName();
        QJsonObject component;
        QJsonArray ids;
        static const QRegularExpression OPERATOR(QStringLiteral("\\s+(?:AND|OR|WITH)\\s+"));
        for (const QString& part : expression.split(OPERATOR, Qt::SkipEmptyParts))
        {
            ids.append(part.trimmed());
        }
        component.insert(QLatin1String("textIds"), ids);
        return QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), m_Facts.m_Name},
            {QStringLiteral("version"), m_Facts.m_Version},
            {QStringLiteral("licence"), expression},
            {QStringLiteral("copyright"), project.value(QLatin1String("copyright")).toString()},
            {QStringLiteral("source"), m_Facts.m_SourceUrl},
            {QStringLiteral("linking"), QString()},
            {QStringLiteral("note"), QString()},
            {QStringLiteral("texts"), text_pages(component)},
        };
    }
    const QJsonArray list = m_Document.value(QLatin1String("components")).toArray();
    for (const QJsonValue& value : list)
    {
        const QJsonObject component = value.toObject();
        if (component.value(QLatin1String("id")).toString() != id)
        {
            continue;
        }
        return QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), component.value(QLatin1String("name")).toString()},
            {QStringLiteral("version"), component.value(QLatin1String("version")).toString()},
            {QStringLiteral("licence"), component.value(QLatin1String("licenceDisplay")).toString()},
            {QStringLiteral("copyright"), component.value(QLatin1String("copyright")).toString()},
            {QStringLiteral("source"), component.value(QLatin1String("source")).toString()},
            {QStringLiteral("linking"), component.value(QLatin1String("linking")).toString()},
            {QStringLiteral("note"), component.value(QLatin1String("note")).toString()},
            {QStringLiteral("texts"), text_pages(component)},
        };
    }
    return {};
}

QStringList AboutModel::paragraphs(const QString& text)
{
    QStringList result;
    const QString unified = QString(text).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    static const QRegularExpression BLANK(QStringLiteral("\\n[ \\t]*\\n"));
    const QStringList blocks = unified.split(BLANK, Qt::SkipEmptyParts);
    for (const QString& block : blocks)
    {
        QStringList lines = block.split(QLatin1Char('\n'));
        qsizetype longest = 0;
        for (QString& line : lines)
        {
            line = line.trimmed();
            longest = std::max(longest, line.size());
        }
        lines.removeAll(QString());
        if (lines.isEmpty())
        {
            continue;
        }
        if (lines.size() > 1 && longest >= PROSE_LINE_LENGTH)
        {
            result.append(lines.join(QLatin1Char(' ')).simplified());
        }
        else
        {
            result.append(lines.join(QLatin1Char('\n')));
        }
    }
    return result;
}

} // namespace ej::viewmodel
