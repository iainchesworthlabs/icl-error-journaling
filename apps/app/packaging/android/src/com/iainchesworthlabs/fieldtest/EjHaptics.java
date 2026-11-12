// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.fieldtest;

import android.app.Activity;
import android.view.View;

/**
 * Haptic feedback (docs/plan/stage-9.md, "Haptics"). The constant is chosen in C++ (android_haptics_plan.cpp); this
 * posts the call to the Android UI thread, which View.performHapticFeedback needs, and returns at once. It needs no
 * permission, and it follows the system's "Touch feedback" setting.
 */
public final class EjHaptics
{
    private EjHaptics()
    {
    }

    /** Called from the Qt thread. */
    public static void fire(final Activity activity, final int constant)
    {
        activity.runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                View view = activity.getWindow().getDecorView();
                view.performHapticFeedback(constant);
            }
        });
    }
}
