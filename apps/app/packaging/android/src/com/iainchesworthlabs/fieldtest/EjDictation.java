// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.fieldtest;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;

import java.util.ArrayList;

/**
 * Dictation (PRD NOTE-07): the on-device SpeechRecognizer of Android 13 and later, which recognises on the phone and
 * sends no audio anywhere. The ordinary SpeechRecognizer, which may use a server, is never used: where the on-device one
 * is not available, isAvailable() is false and the application shows no microphone.
 *
 * C++ (libs/ios_bridge/src/platform/android/dictation.cpp) has asked for RECORD_AUDIO before it calls start(). The
 * recogniser needs the Android UI thread, so every call posts there. The results go back through nativeText and
 * nativeEnded, which C++ registers; the codes of nativeEnded are those of DictationEnd (0 finished, 1 no speech,
 * 2 denied, 3 unavailable, 4 failed).
 */
public final class EjDictation
{
    private static SpeechRecognizer sRecognizer;
    private static long sId;

    private EjDictation()
    {
    }

    private static native void nativeText(long id, String text);

    private static native void nativeEnded(long id, int code);

    /** True on Android 13 and later where an on-device recogniser is installed for the language. */
    public static boolean isAvailable(Context context)
    {
        return Build.VERSION.SDK_INT >= 33 && SpeechRecognizer.isOnDeviceRecognitionAvailable(context);
    }

    /** Called from the Qt thread. */
    public static void start(final Activity activity, final long id)
    {
        activity.runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                release();
                if (!isAvailable(activity))
                {
                    nativeEnded(id, 3);
                    return;
                }
                sId = id;
                sRecognizer = SpeechRecognizer.createOnDeviceSpeechRecognizer(activity);
                sRecognizer.setRecognitionListener(new Listener(id));
                Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
                intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
                intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true);
                intent.putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true);
                sRecognizer.startListening(intent);
            }
        });
    }

    /** Called from the Qt thread. The last words follow as a result, then nativeEnded. */
    public static void stop(final Activity activity)
    {
        activity.runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                if (sRecognizer != null)
                {
                    sRecognizer.stopListening();
                }
            }
        });
    }

    private static void release()
    {
        if (sRecognizer != null)
        {
            sRecognizer.destroy();
            sRecognizer = null;
        }
    }

    private static String first(Bundle bundle)
    {
        ArrayList<String> list = bundle == null ? null : bundle.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
        return list == null || list.isEmpty() ? null : list.get(0);
    }

    private static int codeOf(int error)
    {
        switch (error)
        {
        case SpeechRecognizer.ERROR_NO_MATCH:
        case SpeechRecognizer.ERROR_SPEECH_TIMEOUT:
            return 1;
        case SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS:
            return 2;
        case SpeechRecognizer.ERROR_LANGUAGE_NOT_SUPPORTED:
        case SpeechRecognizer.ERROR_LANGUAGE_UNAVAILABLE:
            return 3;
        default:
            return 4;
        }
    }

    private static final class Listener implements RecognitionListener
    {
        private final long mId;

        Listener(long id)
        {
            mId = id;
        }

        @Override
        public void onPartialResults(Bundle partialResults)
        {
            String text = first(partialResults);
            if (text != null && mId == sId)
            {
                nativeText(mId, text);
            }
        }

        @Override
        public void onResults(Bundle results)
        {
            String text = first(results);
            if (mId != sId)
            {
                return;
            }
            if (text != null)
            {
                nativeText(mId, text);
            }
            release();
            nativeEnded(mId, 0);
        }

        @Override
        public void onError(int error)
        {
            if (mId != sId)
            {
                return;
            }
            release();
            nativeEnded(mId, codeOf(error));
        }

        @Override
        public void onReadyForSpeech(Bundle params)
        {
        }

        @Override
        public void onBeginningOfSpeech()
        {
        }

        @Override
        public void onRmsChanged(float rmsdB)
        {
        }

        @Override
        public void onBufferReceived(byte[] buffer)
        {
        }

        @Override
        public void onEndOfSpeech()
        {
        }

        @Override
        public void onEvent(int eventType, Bundle params)
        {
        }
    }
}
