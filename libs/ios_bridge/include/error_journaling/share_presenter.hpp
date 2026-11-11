// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/share.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>

namespace ej::bridge
{

/// True when `text` is well-formed UTF-8 (no lone continuation byte, overlong form, surrogate or value above U+10FFFF).
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

/// The decisions around a Share, in portable C++ so that the Objective-C++ never sees a bad request and a second sheet
/// never reaches UIKit.
///
/// - A request must set exactly one of text and file, and a text must be valid UTF-8; otherwise `done` gets FAILED
///   (before `present` returns) and the Share is not called.
/// - One sheet at a time: while one is in flight, another request gets FAILED the same way and the Share is not called.
///   The flag clears when the first completes, whatever its outcome.
/// - A temporary file is deleted on every outcome, including a request that was refused, unless the Share retains
///   shared files (Android): then it stays after SHARED and CANCELLED, and the older files in its directory go when the
///   next one is shared.
///
/// A completion that arrives after the presenter is gone does nothing harmful. Main thread only.
class SharePresenter
{
public:
    explicit SharePresenter(Share& share);
    SharePresenter(const SharePresenter&) = delete;
    SharePresenter& operator=(const SharePresenter&) = delete;
    ~SharePresenter();

    [[nodiscard]] bool in_flight() const noexcept;

    void present(ShareRequest request, ShareCallback done);

    /// Shares `file` and removes it when the sheet has ended, or at once when the request is refused.
    void present_temporary_file(const std::filesystem::path& file, ShareCallback done);

private:
    struct State;
    Share& m_Share;
    std::shared_ptr<State> m_State;
};

/// Removes every regular file directly inside `directory` (the share export directory, at launch) and returns how many
/// went. A missing directory is not an error. Subdirectories are left alone.
std::size_t purge_directory(const std::filesystem::path& directory) noexcept;

} // namespace ej::bridge
