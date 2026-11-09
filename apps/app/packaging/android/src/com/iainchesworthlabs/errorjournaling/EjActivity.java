// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.errorjournaling;

import org.qtproject.qt.android.bindings.QtActivity;

/**
 * The application's activity. It forwards what Android delivers to Java and C++ cannot see to the classes that handle it
 * (docs/plan/stage-9.md, "Scope and conventions"): so far, when the activity is covered and uncovered, which tells
 * EjShare that a chooser was dismissed.
 */
public class EjActivity extends QtActivity
{
    @Override
    protected void onPause()
    {
        super.onPause();
        EjShare.onPaused();
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        EjShare.onResumed();
    }
}
