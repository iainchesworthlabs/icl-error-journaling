// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.fieldtest;

import android.app.Activity;
import android.content.Context;
import android.database.ContentObserver;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.accessibility.AccessibilityManager;

/**
 * What the person has asked of the system that the interface follows (the Stage 9 plan, "Accessibility settings"): the
 * text size (Configuration.fontScale), Remove animations (Settings.Global.ANIMATOR_DURATION_SCALE), TalkBack (touch
 * exploration), High contrast text (the hidden Settings.Secure key high_text_contrast_enabled) and Bold text
 * (Configuration.fontWeightAdjustment, Android 12 and later), and a notice when one changes. Java only reads the
 * numbers; what they mean (the clamp of the text scale, what counts as Reduce Motion, High contrast text or Bold text) is
 * decided in C++ (android_system_plan.cpp).
 *
 * C++ registers nativeChanged, then calls start() once. A change of the font size arrives as a configuration change
 * (EjActivity.onConfigurationChanged, which also carries Bold text), the others through observers and a listener.
 */
public final class EjAccessibility
{
    /** "High contrast text" of the accessibility settings. The key is not in the public SDK; 0 or absent is off. */
    private static final String HIGH_TEXT_CONTRAST_KEY = "high_text_contrast_enabled";

    private static boolean s_Started = false;

    private EjAccessibility()
    {
    }

    private static native void nativeChanged();

    /** Called once from C++ after nativeChanged is registered. */
    public static synchronized void start(final Activity activity)
    {
        if (s_Started)
        {
            return;
        }
        s_Started = true;
        activity.runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                activity.getContentResolver().registerContentObserver(
                    Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE), false,
                    new ContentObserver(new Handler(Looper.getMainLooper()))
                    {
                        @Override
                        public void onChange(boolean selfChange)
                        {
                            nativeChanged();
                        }
                    });
                activity.getContentResolver().registerContentObserver(
                    Settings.Secure.getUriFor(HIGH_TEXT_CONTRAST_KEY), false,
                    new ContentObserver(new Handler(Looper.getMainLooper()))
                    {
                        @Override
                        public void onChange(boolean selfChange)
                        {
                            nativeChanged();
                        }
                    });
                AccessibilityManager manager =
                    (AccessibilityManager) activity.getSystemService(Context.ACCESSIBILITY_SERVICE);
                if (manager != null)
                {
                    manager.addTouchExplorationStateChangeListener(
                        new AccessibilityManager.TouchExplorationStateChangeListener()
                        {
                            @Override
                            public void onTouchExplorationStateChanged(boolean enabled)
                            {
                                nativeChanged();
                            }
                        });
                }
            }
        });
    }

    /** From EjActivity.onConfigurationChanged: the font size, among other things, may have changed. */
    public static void onConfigurationChanged()
    {
        boolean started;
        synchronized (EjAccessibility.class)
        {
            started = s_Started;
        }
        if (started)
        {
            nativeChanged();
        }
    }

    public static double fontScale(Activity activity)
    {
        return activity.getResources().getConfiguration().fontScale;
    }

    public static double animatorScale(Activity activity)
    {
        return Settings.Global.getFloat(activity.getContentResolver(), Settings.Global.ANIMATOR_DURATION_SCALE, 1.0f);
    }

    /** 1 when High contrast text is on, 0 when it is off or the system has no such setting. */
    public static int highTextContrast(Activity activity)
    {
        return Settings.Secure.getInt(activity.getContentResolver(), HIGH_TEXT_CONTRAST_KEY, 0);
    }

    /** What Bold text adds to a weight (300 when on, 0 when off), and 0 before Android 12, which has no such setting. */
    public static int fontWeightAdjustment(Activity activity)
    {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S)
        {
            return 0;
        }
        return activity.getResources().getConfiguration().fontWeightAdjustment;
    }

    public static boolean touchExploration(Activity activity)
    {
        AccessibilityManager manager = (AccessibilityManager) activity.getSystemService(Context.ACCESSIBILITY_SERVICE);
        return manager != null && manager.isTouchExplorationEnabled();
    }
}
