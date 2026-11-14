// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <atomic>
#include <filesystem>

namespace ej::test
{

/// A second connection to a database file, for tests that look at what the repository wrote or damage it behind its
/// back.
class RawDb
{
public:
    explicit RawDb(const std::filesystem::path& file)
    {
        static std::atomic<unsigned> counter{0};
        m_Name = QStringLiteral("ej-raw-%1").arg(++counter);
        m_Db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_Name);
        m_Db.setDatabaseName(QString::fromStdString(file.generic_string()));
        m_Open = m_Db.open();
    }
    RawDb(const RawDb&) = delete;
    RawDb& operator=(const RawDb&) = delete;
    ~RawDb()
    {
        m_Db.close();
        m_Db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_Name);
    }

    [[nodiscard]] bool is_open() const { return m_Open; }

    /// True when the statement ran.
    bool exec(const QString& sql, const QVariantList& args = {})
    {
        QSqlQuery query(m_Db);
        if (!query.prepare(sql))
        {
            return false;
        }
        for (const QVariant& arg : args)
        {
            query.addBindValue(arg);
        }
        return query.exec();
    }

    /// The first column of the first row; an invalid variant for no row or a failed statement.
    [[nodiscard]] QVariant scalar(const QString& sql, const QVariantList& args = {})
    {
        QSqlQuery query(m_Db);
        if (!query.prepare(sql))
        {
            return {};
        }
        for (const QVariant& arg : args)
        {
            query.addBindValue(arg);
        }
        if (!query.exec() || !query.next())
        {
            return {};
        }
        return query.value(0);
    }

    /// Takes the schema back to what version 3 had: the tables of the interaction schema are dropped (the `entry` table
    /// keeps the wider CHECK, which no row of version 3 breaks). For a test that starts from a database "as an older
    /// build left it" and has the migration build the rest.
    bool drop_interaction_schema()
    {
        for (const char* table : {"interaction_entry_person", "interaction_entry", "interaction_type", "person"})
        {
            if (!exec(QStringLiteral("DROP TABLE %1").arg(QLatin1String(table))))
            {
                return false;
            }
        }
        return true;
    }

private:
    QString m_Name;
    QSqlDatabase m_Db;
    bool m_Open = false;
};

}  // namespace ej::test
