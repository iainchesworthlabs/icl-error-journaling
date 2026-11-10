// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.errorjournaling;

import android.content.Intent;
import android.os.Bundle;

import org.qtproject.qt.android.bindings.QtActivity;

/**
 * The application's activity. It forwards what Android delivers to Java and C++ cannot see to the classes that handle it
 * (docs/plan/stage-9.md, "Scope and conventions"): when the activity is covered and uncovered (EjShare, which learns that
 * a chooser was dismissed), the result of the document picker, and a setup file another app opens or sends (EjDocuments).
 */
public class EjActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        // Only a fresh start: after the system recreated the activity, getIntent() is the one that started it first, and
        // that file has been imported already.
        if (savedInstanceState == null)
        {
            EjDocuments.onIntent(this, getIntent());
        }
    }

    @Override
    protected void onNewIntent(Intent intent)
    {
        super.onNewIntent(intent);
        setIntent(intent);
        EjDocuments.onIntent(this, intent);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data)
    {
        if (!EjDocuments.onActivityResult(this, requestCode, resultCode, data))
        {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

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
