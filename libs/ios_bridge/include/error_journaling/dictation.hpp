// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ej::bridge
{

/// Why one run of dictation ended.
enum class DictationEnd
{
    FINISHED,    ///< Ended by `stop()`, or the recogniser finished by itself.
    NO_SPEECH,   ///< The recogniser heard nothing for a while and gave up.
    DENIED,      ///< The person refused the microphone or speech recognition, or it is restricted.
    UNAVAILABLE, ///< This device cannot recognise speech on the device itself.
    FAILED,      ///< Any other error.
};

/// What a run reports. Both are called on the main thread, never from inside `start()` or `stop()`.
struct DictationHandlers
{
    /// The words heard in this run so far, UTF-8. Each call replaces the one before it (a recogniser revises its guess
    /// as it goes); a new run starts again from empty.
    std::function<void(const std::string&)> m_Text;
    /// Called exactly once for each `start()`, after the last text.
    std::function<void(DictationEnd)> m_Ended;
};

/// Speech to text with the facility the phone already has: SFSpeechRecognizer on iOS and SpeechRecognizer on Android,
/// both asked for recognition on the device itself, so that no audio and no text leave the phone. The application adds no
/// recogniser of its own, uses no library for it and has no fallback to a recogniser that sends audio to a server: where
/// the device cannot recognise on the device, `available()` is false and the control is not shown.
/// Every call is made on the main thread.
class Dictation
{
public:
    Dictation() = default;
    Dictation(const Dictation&) = delete;
    Dictation& operator=(const Dictation&) = delete;
    virtual ~Dictation() = default;

    /// True when on-device recognition is supported for the person's language. False on the desktop stub, so the
    /// interface hides the microphone there.
    [[nodiscard]] virtual bool available() const noexcept = 0;

    /// Starts listening, asking for the permissions first if they have not been asked for. At most one run at a time:
    /// the caller does not start while a run is going. `m_Ended` is called once.
    virtual void start(DictationHandlers handlers) = 0;

    /// Ends the run in progress; `m_Ended(FINISHED)` follows. Does nothing when there is none.
    virtual void stop() = 0;
};

/// ios/dictation.mm, android/dictation.cpp or desktop/dictation.cpp (unavailable).
[[nodiscard]] std::unique_ptr<Dictation> make_dictation();

} // namespace ej::bridge
