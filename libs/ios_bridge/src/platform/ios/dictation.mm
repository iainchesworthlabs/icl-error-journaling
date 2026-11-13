// SPDX-License-Identifier: GPL-3.0-or-later
// Dictation on iOS: SFSpeechRecognizer fed by AVAudioEngine, with requiresOnDeviceRecognition set, so that the audio is
// recognised on the phone and nothing is sent to Apple. A language the phone cannot recognise on the device makes
// `available()` false and the microphone is not shown; there is no fallback to server recognition. The microphone and
// speech recognition permissions are asked for at the first tap, never at launch; the texts the system shows are
// NSMicrophoneUsageDescription and NSSpeechRecognitionUsageDescription in Info.plist.in.
//
// Not run on a device: no Mac is available to the developer, so this file is built by CI only.
#include "error_journaling/dictation.hpp"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <Speech/Speech.h>

#include <cstdint>
#include <string>
#include <utility>

namespace ej::bridge
{

namespace
{

/// kAFAssistantErrorDomain code for "no speech detected".
constexpr NSInteger NO_SPEECH_CODE = 1110;

class IosDictation final : public Dictation
{
public:
    ~IosDictation() override
    {
        tear_down();
    }

    [[nodiscard]] bool available() const noexcept override
    {
        if (!m_Checked)
        {
            SFSpeechRecognizer* recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale currentLocale]];
            m_Available = recognizer != nil && recognizer.supportsOnDeviceRecognition;
            m_Checked = true;
        }
        return m_Available;
    }

    void start(DictationHandlers handlers) override
    {
        tear_down();
        m_Handlers = std::move(handlers);
        const std::uint64_t run = ++m_Run;
        if (!available())
        {
            finish_later(run, DictationEnd::UNAVAILABLE);
            return;
        }
        [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (run != m_Run)
            {
                return;
            }
            if (status != SFSpeechRecognizerAuthorizationStatusAuthorized)
            {
                finish(run, DictationEnd::DENIED);
                return;
            }
            request_microphone(run);
          });
        }];
    }

    void stop() override
    {
        if (m_Request == nil && m_Task == nil)
        {
            return;
        }
        const std::uint64_t run = m_Run;
        // No more audio; the recogniser then delivers its last words as the final result, which ends the run. The
        // microphone is released at once.
        [m_Engine stop];
        [[m_Engine inputNode] removeTapOnBus:0];
        [m_Request endAudio];
        // The final result should come within a moment; if it does not, the run ends anyway.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
          if (run == m_Run)
          {
              finish(run, DictationEnd::FINISHED);
          }
        });
    }

private:
    void request_microphone(std::uint64_t run)
    {
        auto answered = ^(BOOL granted) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (run != m_Run)
            {
                return;
            }
            if (!granted)
            {
                finish(run, DictationEnd::DENIED);
                return;
            }
            listen(run);
          });
        };
        // The deployment target is iOS 18, so the iOS 17 API is the one to use.
        [AVAudioApplication requestRecordPermissionWithCompletionHandler:answered];
    }

    void listen(std::uint64_t run)
    {
        NSError* error = nil;
        AVAudioSession* session = [AVAudioSession sharedInstance];
        if (![session setCategory:AVAudioSessionCategoryRecord mode:AVAudioSessionModeMeasurement options:0 error:&error] ||
            ![session setActive:YES withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:&error])
        {
            finish(run, DictationEnd::FAILED);
            return;
        }
        m_Recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale currentLocale]];
        if (m_Recognizer == nil || !m_Recognizer.supportsOnDeviceRecognition)
        {
            finish(run, DictationEnd::UNAVAILABLE);
            return;
        }
        m_Request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
        m_Request.shouldReportPartialResults = YES;
        m_Request.requiresOnDeviceRecognition = YES; // never the server
        m_Request.taskHint = SFSpeechRecognitionTaskHintDictation;
        m_Engine = [[AVAudioEngine alloc] init];
        SFSpeechAudioBufferRecognitionRequest* request = m_Request;
        AVAudioInputNode* input = [m_Engine inputNode];
        [input installTapOnBus:0
                    bufferSize:1024
                        format:[input outputFormatForBus:0]
                         block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
                           [request appendAudioPCMBuffer:buffer];
                         }];
        [m_Engine prepare];
        if (![m_Engine startAndReturnError:&error])
        {
            finish(run, DictationEnd::FAILED);
            return;
        }
        __block bool heard = false;
        m_Task = [m_Recognizer recognitionTaskWithRequest:m_Request
                                            resultHandler:^(SFSpeechRecognitionResult* result, NSError* failure) {
                                              std::string text;
                                              bool final_result = false;
                                              if (result != nil)
                                              {
                                                  text = std::string([result.bestTranscription.formattedString UTF8String]);
                                                  final_result = result.isFinal;
                                              }
                                              const NSInteger code = failure != nil ? failure.code : 0;
                                              dispatch_async(dispatch_get_main_queue(), ^{
                                                if (run != m_Run)
                                                {
                                                    return;
                                                }
                                                if (!text.empty())
                                                {
                                                    heard = true;
                                                    if (m_Handlers.m_Text)
                                                    {
                                                        m_Handlers.m_Text(text);
                                                    }
                                                }
                                                if (failure != nil)
                                                {
                                                    finish(run, heard ? DictationEnd::FINISHED
                                                                      : code == NO_SPEECH_CODE ? DictationEnd::NO_SPEECH
                                                                                               : DictationEnd::FAILED);
                                                }
                                                else if (final_result)
                                                {
                                                    finish(run, DictationEnd::FINISHED);
                                                }
                                              });
                                            }];
    }

    void tear_down()
    {
        if (m_Engine != nil)
        {
            [m_Engine stop];
            [[m_Engine inputNode] removeTapOnBus:0];
            m_Engine = nil;
        }
        if (m_Task != nil)
        {
            [m_Task cancel];
            m_Task = nil;
        }
        m_Request = nil;
        m_Recognizer = nil;
        if (m_SessionActive)
        {
            [[AVAudioSession sharedInstance] setActive:NO
                                           withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                                 error:nil];
        }
        m_SessionActive = false;
    }

    /// Ends the run `run` once: releases the microphone, then reports. A later call for the same run does nothing.
    void finish(std::uint64_t run, DictationEnd end)
    {
        if (run != m_Run)
        {
            return;
        }
        ++m_Run;
        m_SessionActive = true; // the session may have been activated; tear_down deactivates it either way
        tear_down();
        DictationHandlers handlers = std::move(m_Handlers);
        m_Handlers = {};
        if (handlers.m_Ended)
        {
            handlers.m_Ended(end);
        }
    }

    /// The callback is never made from inside start().
    void finish_later(std::uint64_t run, DictationEnd end)
    {
        dispatch_async(dispatch_get_main_queue(), ^{
          finish(run, end);
        });
    }

    mutable bool m_Checked = false;
    mutable bool m_Available = false;
    bool m_SessionActive = false;
    std::uint64_t m_Run = 0;
    DictationHandlers m_Handlers;
    SFSpeechRecognizer* m_Recognizer = nil;
    SFSpeechAudioBufferRecognitionRequest* m_Request = nil;
    SFSpeechRecognitionTask* m_Task = nil;
    AVAudioEngine* m_Engine = nil;
};

} // namespace

std::unique_ptr<Dictation> make_dictation()
{
    return std::make_unique<IosDictation>();
}

} // namespace ej::bridge
