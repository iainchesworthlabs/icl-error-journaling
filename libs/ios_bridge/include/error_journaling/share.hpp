// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ej::bridge
{

/// How a share sheet ended.
enum class ShareOutcome
{
    SHARED,      ///< The user chose an activity and it completed (the sheet's own Copy included).
    CANCELLED,   ///< The user dismissed the sheet, or chose an activity and backed out of it.
    UNAVAILABLE, ///< This platform cannot present a share sheet (the desktop stub).
    FAILED,      ///< The sheet could not be presented, the request was not valid, or the activity reported an error.
};

/// What to share: exactly one of the two members is set.
struct ShareRequest
{
    std::string m_Text;           ///< The export text, UTF-8. Empty when sharing a file.
    std::filesystem::path m_File; ///< A file to share (the configuration export). Empty when sharing text.
};

struct ShareResult
{
    ShareOutcome m_Outcome = ShareOutcome::UNAVAILABLE;
    /// The activity the user chose, e.g. "com.apple.UIKit.activity.CopyToPasteboard"; empty when none was.
    std::string m_ActivityType;
    /// The NSError domain and code only, never text or a path. For logs.
    std::string m_Detail;
};

using ShareCallback = std::function<void(const ShareResult&)>;

/// The system share sheet. Every call is made on the main thread and the callback is delivered on it.
class Share
{
public:
    Share() = default;
    Share(const Share&) = delete;
    Share& operator=(const Share&) = delete;
    virtual ~Share() = default;

    /// False on the desktop stub, so the interface can hide the share control there.
    [[nodiscard]] virtual bool available() const noexcept = 0;

    /// Presents the sheet. `done` is called exactly once. The iOS implementation calls it after this function has
    /// returned; the desktop stub calls it before. The request is valid (see SharePresenter): the implementation does
    /// not check it again.
    virtual void present(ShareRequest request, ShareCallback done) = 0;
};

/// ios/share.mm or desktop/share.cpp.
[[nodiscard]] std::unique_ptr<Share> make_share();

} // namespace ej::bridge
