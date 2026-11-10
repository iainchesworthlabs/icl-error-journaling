// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/share.hpp"

#include <cstddef>

namespace ej::bridge
{

/// The decisions around an Android share, in portable C++ so that the JNI code (platform/android/share.cpp) only passes
/// them on and a test needs no phone (docs/plan/stage-9.md, "Share").

/// Text above this many bytes (UTF-8) is shared as a `.txt` file instead of in `EXTRA_TEXT`: the text travels in a Binder
/// transaction, which Android limits to about 1 MB for the whole process.
inline constexpr std::size_t android_inline_text_limit = 256 * 1024;

enum class AndroidShareKind
{
    TEXT,      ///< `ACTION_SEND` with `EXTRA_TEXT`.
    TEXT_FILE, ///< The text written to a `.txt` file, shared like a file.
    FILE,      ///< The request's own file (the configuration export).
};

/// How `request` (valid: exactly one of text and file) goes out.
[[nodiscard]] AndroidShareKind plan_android_share(const ShareRequest& request) noexcept;

/// How the chooser ended, as far as Android lets the application tell.
enum class AndroidShareEnd
{
    CHOSEN,    ///< The chooser's `IntentSender` broadcast the component the person picked.
    DISMISSED, ///< The application came back to the front and no component was chosen.
    ERROR,     ///< The chooser could not be started, or the file could not be made available.
};

[[nodiscard]] ShareOutcome android_share_outcome(AndroidShareEnd end) noexcept;

/// The integers `EjShare.java` passes to `nativeFinished`: 0 chosen, 1 dismissed, 2 error. Anything else is an error.
[[nodiscard]] AndroidShareEnd android_share_end_from_code(int code) noexcept;

} // namespace ej::bridge
