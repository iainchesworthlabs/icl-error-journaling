// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/storage_error.hpp"

namespace ej
{

std::string_view storage_error_kind_name(StorageErrorKind kind)
{
    switch (kind)
    {
    case StorageErrorKind::NOT_FOUND:
        return "not found";
    case StorageErrorKind::ALREADY_EXISTS:
        return "already exists";
    case StorageErrorKind::OPEN_LOG_EXISTS:
        return "open log exists";
    case StorageErrorKind::NOT_OPEN_LOG:
        return "not the open log";
    case StorageErrorKind::LOG_NOT_ARCHIVED:
        return "log not archived";
    case StorageErrorKind::INVALID_ARGUMENT:
        return "invalid argument";
    case StorageErrorKind::LIMIT_EXCEEDED:
        return "limit exceeded";
    case StorageErrorKind::DISK_FULL:
        return "disk full";
    case StorageErrorKind::LOCKED:
        return "locked";
    case StorageErrorKind::PERMISSION:
        return "permission denied";
    case StorageErrorKind::CORRUPT:
        return "corrupt";
    case StorageErrorKind::NOT_OUR_DATABASE:
        return "not our database";
    case StorageErrorKind::NEWER_SCHEMA:
        return "newer schema";
    case StorageErrorKind::IO:
        return "i/o error";
    case StorageErrorKind::JOURNAL:
        return "journal error";
    case StorageErrorKind::DATABASE:
        return "database error";
    case StorageErrorKind::INTERNAL:
        return "internal error";
    }
    return "unknown";
}

std::string describe(const StorageError& error)
{
    std::string text(storage_error_kind_name(error.m_Kind));
    if (!error.m_Message.empty())
    {
        text += ": ";
        text += error.m_Message;
    }
    if (error.m_NativeCode != 0)
    {
        text += " (code ";
        text += std::to_string(error.m_NativeCode);
        text += ")";
    }
    return text;
}

} // namespace ej
