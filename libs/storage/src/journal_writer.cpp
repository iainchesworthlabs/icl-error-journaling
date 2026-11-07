// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/journal_writer.hpp"

#include "error_journaling/journal_reader.hpp"

namespace ej
{

Result<std::unique_ptr<JournalWriter>, StorageError> JournalWriter::open(std::unique_ptr<JournalFile> file, LogId log,
                                                                         CrashHook hook)
{
    auto content = file->read_all();
    if (!content)
    {
        return fail(content.error());
    }
    auto scan = scan_journal_bytes(*content, log);
    if (!scan)
    {
        return fail(scan.error());
    }
    if (!scan->is_clean())
    {
        return fail(make_error(StorageErrorKind::JOURNAL, "the journal needs recovery before it can be appended to"));
    }
    const std::uint64_t last_seq = scan->last_seq();
    return std::unique_ptr<JournalWriter>(new JournalWriter(std::move(file), log, std::move(hook), last_seq));
}

Result<void, StorageError> JournalWriter::cut_back(std::uint64_t length)
{
    if (auto cut = m_File->truncate(length); !cut)
    {
        return cut;
    }
    return m_File->sync();
}

Result<JournalMark, StorageError> JournalWriter::append_line(const JournalRecord& record, bool with_hooks)
{
    if (record.m_Log != m_Log || record.m_Seq != m_LastSeq + 1)
    {
        return fail(make_error(StorageErrorKind::INTERNAL, "a record out of order was given to the journal"));
    }
    auto line = encode_record(record);
    if (!line)
    {
        return fail(line.error());
    }
    line->push_back('\n');

    const std::uint64_t offset = m_File->size();
    // Whatever goes wrong from here, the file goes back to what it was, so no half line is left in front of the next
    // record. Cutting back is best effort: if it fails too, the next launch reads the rest as a torn tail.
    const auto undo = [this, offset](StorageError error)
    {
        (void)cut_back(offset);
        return error;
    };

    if (auto appended = m_File->append(*line); !appended)
    {
        return fail(undo(appended.error()));
    }
    if (with_hooks && m_Hook)
    {
        if (auto hooked = m_Hook(CrashPoint::AFTER_JOURNAL_APPEND); !hooked)
        {
            return fail(undo(hooked.error()));
        }
    }
    if (auto synced = m_File->sync(); !synced)
    {
        return fail(undo(synced.error()));
    }
    if (with_hooks && m_Hook)
    {
        if (auto hooked = m_Hook(CrashPoint::AFTER_JOURNAL_SYNC); !hooked)
        {
            return fail(undo(hooked.error()));
        }
    }
    m_LastSeq = record.m_Seq;
    return JournalMark{m_Log, record.m_Seq, offset};
}

Result<JournalMark, StorageError> JournalWriter::append(const JournalRecord& record)
{
    return append_line(record, true);
}

Result<void, StorageError> JournalWriter::cancel(const JournalMark& mark)
{
    JournalRecord cancelling;
    cancelling.m_Seq = m_LastSeq + 1;
    cancelling.m_Log = m_Log;
    cancelling.m_Body = VoidBody{mark.m_Seq};
    const auto voided = append_line(cancelling, false);
    if (voided)
    {
        return {};
    }
    // The void record could not be written. Cut the journal back to before the record instead.
    if (mark.m_Seq != m_LastSeq)
    {
        return fail(make_error(StorageErrorKind::JOURNAL, "only the last record of a journal can be cut back"));
    }
    if (auto cut = cut_back(mark.m_Offset); !cut)
    {
        return cut;
    }
    m_LastSeq = mark.m_Seq - 1;
    return {};
}

}  // namespace ej
