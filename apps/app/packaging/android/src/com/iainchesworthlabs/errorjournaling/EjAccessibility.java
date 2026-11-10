// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.errorjournaling;

import android.app.Activity;
import android.content.Context;
import android.database.ContentObserver;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.accessibility.AccessibilityManager;

/**
 * What the person has asked of the system that the interface follows (docs/plan/stage-9.md, "Accessibility settings"): the
 * text size (Configuration.fontScale), Remove animations (Settings.Global.ANIMATOR_DURATION_SCALE) and TalkBack (touch
 * exploration), and a notice when one changes. Java only reads the three numbers; what they mean (the clamp of the text
 * scale, what counts as Reduce Motion) is decided in C++ (android_system_plan.cpp).
 *
 * C++ registers nativeChanged, then calls start() once. A change of the font size arrives as a configuration change
 * (EjActivity.onConfigurationChanged), the other two through an observer and a listener.
 */
public final class EjAccessibility
{
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

    public static boolean touchExploration(Activity activity)
    {
        AccessibilityManager manager = (AccessibilityManager) activity.getSystemService(Context.ACCESSIBILITY_SERVICE);
        return manager != null && manager.isTouchExplorationEnabled();
    }
}
