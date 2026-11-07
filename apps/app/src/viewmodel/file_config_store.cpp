// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/file_config_store.hpp"

#include "error_journaling/configuration.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>
#include <QString>
#include <string>
#include <system_error>
#include <utility>

namespace ej::viewmodel
{

namespace
{

QString to_qstring(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

QString from_utf8(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// A storage error for a file that could not be opened, read or written. The message names what was being done, never
/// the path or the content.
StorageError file_error(QFileDevice::FileError error, std::string what)
{
    const StorageErrorKind kind =
        error == QFileDevice::PermissionsError ? StorageErrorKind::PERMISSION : StorageErrorKind::IO;
    return make_error(kind, std::move(what), static_cast<int>(error));
}

/// A write that failed: a full disk is told apart from any other failure, because the person can act on it.
StorageError write_error(const QString& folder, qsizetype wanted, QFileDevice::FileError error, std::string what)
{
    if (error == QFileDevice::PermissionsError)
    {
        return file_error(error, std::move(what));
    }
    const QStorageInfo volume(folder);
    if (volume.isValid() && volume.isReady() && volume.bytesAvailable() >= 0 && volume.bytesAvailable() < wanted)
    {
        return make_error(StorageErrorKind::DISK_FULL, std::move(what), static_cast<int>(error));
    }
    return file_error(error, std::move(what));
}

/// The file's bytes, at most `max_bytes` of them. A larger file is `LIMIT_EXCEEDED` when `refuse_larger`, and cut at
/// `max_bytes + 1` otherwise (the caller sees it is over the limit and has the reason).
Result<std::string, StorageError> read_up_to(const QString& path, std::size_t max_bytes, bool refuse_larger)
{
    QFile file(path);
    if (!file.exists())
    {
        return fail(make_error(StorageErrorKind::NOT_FOUND, "the file does not exist"));
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        return fail(file_error(file.error(), "the file could not be opened"));
    }
    if (refuse_larger && static_cast<std::size_t>(file.size()) > max_bytes)
    {
        return fail(make_error(StorageErrorKind::LIMIT_EXCEEDED, "the file is larger than the limit"));
    }
    const QByteArray bytes = file.read(static_cast<qint64>(max_bytes) + 1);
    if (file.error() != QFileDevice::NoError)
    {
        return fail(file_error(file.error(), "the file could not be read"));
    }
    if (refuse_larger && static_cast<std::size_t>(bytes.size()) > max_bytes)
    {
        return fail(make_error(StorageErrorKind::LIMIT_EXCEEDED, "the file is larger than the limit"));
    }
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

/// Writes `text` to `path` as a unit; `before_commit`, when it is set, runs between the write and the rename.
template <typename BeforeCommit>
Result<void, StorageError> replace_file(const QString& path, std::string_view text, BeforeCommit&& before_commit)
{
    const QString folder = QFileInfo(path).absolutePath();
    QSaveFile out(path);
    // No fallback to writing the file in place: a failure to make the temporary file is a failure.
    out.setDirectWriteFallback(false);
    if (!out.open(QIODevice::WriteOnly))
    {
        return fail(write_error(folder, static_cast<qsizetype>(text.size()), out.error(),
                                "the file could not be opened for writing"));
    }
    const auto size = static_cast<qint64>(text.size());
    if (out.write(text.data(), size) != size)
    {
        const QFileDevice::FileError code = out.error();
        out.cancelWriting();
        return fail(write_error(folder, static_cast<qsizetype>(size), code, "the file could not be written"));
    }
    if (const std::optional<StorageError> stop = before_commit())
    {
        out.cancelWriting();
        return fail(*stop);
    }
    if (!out.commit())
    {
        return fail(write_error(folder, static_cast<qsizetype>(size), out.error(), "the file could not be replaced"));
    }
    return {};
}

} // namespace

FileConfigStore::FileConfigStore(std::filesystem::path file) : m_File(std::move(file)) {}

FileConfigStore::FileConfigStore(std::filesystem::path file, BeforeCommit before_commit)
    : m_File(std::move(file)), m_BeforeCommit(std::move(before_commit))
{
}

Result<std::optional<std::string>, StorageError> FileConfigStore::load()
{
    const QString path = to_qstring(m_File);
    if (!QFileInfo::exists(path))
    {
        return std::optional<std::string>{};
    }
    // The limit is the configuration's own; a file over it is returned cut one byte over, and import reports why it is
    // refused.
    auto text = read_up_to(path, CONFIG_MAX_BYTES, false);
    if (!text)
    {
        return fail(std::move(text.error()));
    }
    return std::optional<std::string>(std::move(*text));
}

Result<void, StorageError> FileConfigStore::save(std::string_view text)
{
    const QString path = to_qstring(m_File);
    const QString folder = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(folder))
    {
        return fail(make_error(StorageErrorKind::IO, "the folder of the settings file could not be made"));
    }

    // Never lose a file this build cannot read: it may be damaged, or written by a newer build.
    if (QFileInfo::exists(path))
    {
        const auto old_text = read_up_to(path, CONFIG_MAX_BYTES, false);
        if (!old_text)
        {
            return fail(old_text.error());
        }
        if (!import_configuration(*old_text))
        {
            const QString copy = to_qstring(next_invalid_copy());
            if (!QFile::copy(path, copy))
            {
                return fail(make_error(StorageErrorKind::IO, "the old settings file could not be kept"));
            }
        }
    }

    return replace_file(path, text,
                        [this]() -> std::optional<StorageError>
                        {
                            if (m_BeforeCommit)
                            {
                                return m_BeforeCommit();
                            }
                            return std::nullopt;
                        });
}

Result<std::string, StorageError> FileConfigStore::read_file(std::string_view path, std::size_t max_bytes)
{
    return read_up_to(from_utf8(path), max_bytes, true);
}

Result<void, StorageError> FileConfigStore::write_file(std::string_view path, std::string_view text)
{
    return replace_file(from_utf8(path), text, []() -> std::optional<StorageError> { return std::nullopt; });
}

std::filesystem::path FileConfigStore::next_invalid_copy() const
{
    for (int number = 1;; ++number)
    {
        std::filesystem::path candidate = m_File;
        candidate += number == 1 ? std::string(".invalid") : ".invalid-" + std::to_string(number);
        std::error_code ignored;
        if (!std::filesystem::exists(candidate, ignored))
        {
            return candidate;
        }
    }
}

} // namespace ej::viewmodel
