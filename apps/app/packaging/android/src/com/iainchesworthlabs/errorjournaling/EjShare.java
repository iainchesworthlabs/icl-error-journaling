// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.errorjournaling;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import androidx.core.content.FileProvider;

import java.io.File;

/**
 * The share sheet: ACTION_SEND through the system chooser (docs/plan/stage-9.md, "Share"). Everything that decides
 * something is in C++ (android_share_plan.cpp); this class makes the intent, starts the chooser and reports how it ended
 * as one of three codes: 0 a component was chosen, 1 the chooser was dismissed, 2 an error.
 *
 * Android does not say whether a share completed. The chooser's IntentSender broadcasts the component the person picked;
 * when the application comes back to the front with no such broadcast, the chooser was dismissed. All of it runs on the
 * Android UI thread, and nativeFinished is called from there.
 */
public final class EjShare
{
    private static final String ACTION = "com.iainchesworthlabs.errorjournaling.SHARE_CHOSEN";
    // How long after the application returns to the front the broadcast of a choice is waited for.
    private static final long GRACE_MS = 700;

    private static long s_Id = 0;
    private static boolean s_Pending = false;
    private static boolean s_Paused = false;
    private static Activity s_Activity = null;
    private static BroadcastReceiver s_Receiver = null;
    private static final Handler s_Handler = new Handler(Looper.getMainLooper());

    private EjShare()
    {
    }

    private static native void nativeFinished(long id, int code, String detail);

    /** Shows the chooser for `text`, or, when `path` is not null, for that file. Called from the Qt thread. */
    public static void present(final Activity activity, final long id, final String text, final String path)
    {
        activity.runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                start(activity, id, text, path);
            }
        });
    }

    private static void start(Activity activity, long id, String text, String path)
    {
        if (s_Pending)
        {
            nativeFinished(id, 2, "a share is already open");
            return;
        }
        try
        {
            Intent send = new Intent(Intent.ACTION_SEND);
            if (path != null)
            {
                Uri uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".files", new File(path));
                send.setType(path.endsWith(".txt") ? "text/plain" : "application/json");
                send.putExtra(Intent.EXTRA_STREAM, uri);
                send.setClipData(ClipData.newRawUri("", uri));
                send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            }
            else
            {
                send.setType("text/plain");
                send.putExtra(Intent.EXTRA_TEXT, text);
            }

            s_Id = id;
            s_Activity = activity;
            s_Pending = true;
            s_Paused = false;
            s_Receiver = new BroadcastReceiver()
            {
                @Override
                public void onReceive(Context context, Intent intent)
                {
                    ComponentName chosen = intent.getParcelableExtra(Intent.EXTRA_CHOSEN_COMPONENT, ComponentName.class);
                    finish(0, chosen != null ? chosen.getPackageName() : "");
                }
            };
            IntentFilter filter = new IntentFilter(ACTION);
            if (Build.VERSION.SDK_INT >= 33)
            {
                activity.registerReceiver(s_Receiver, filter, Context.RECEIVER_NOT_EXPORTED);
            }
            else
            {
                activity.registerReceiver(s_Receiver, filter);
            }
            // The system adds EXTRA_CHOSEN_COMPONENT to the intent, so it has to be mutable from Android 12.
            int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            PendingIntent chosen = PendingIntent.getBroadcast(
                activity, 0, new Intent(ACTION).setPackage(activity.getPackageName()), flags);
            activity.startActivity(Intent.createChooser(send, null, chosen.getIntentSender()));
        }
        catch (Exception e)
        {
            finish(2, e.getClass().getSimpleName());
        }
    }

    /** From EjActivity.onPause: the chooser (or anything else) has covered the application. */
    public static void onPaused()
    {
        if (s_Pending)
        {
            s_Paused = true;
        }
    }

    /** From EjActivity.onResume: if a chooser was open and no choice has been broadcast, it was dismissed. */
    public static void onResumed()
    {
        if (s_Pending && s_Paused)
        {
            final long id = s_Id;
            s_Handler.postDelayed(new Runnable()
            {
                @Override
                public void run()
                {
                    if (s_Pending && s_Id == id)
                    {
                        finish(1, "");
                    }
                }
            }, GRACE_MS);
        }
    }

    private static void finish(int code, String detail)
    {
        if (!s_Pending)
        {
            return;
        }
        s_Pending = false;
        try
        {
            if (s_Activity != null && s_Receiver != null)
            {
                s_Activity.unregisterReceiver(s_Receiver);
            }
        }
        catch (RuntimeException ignored)
        {
            // Already unregistered.
        }
        long id = s_Id;
        s_Receiver = null;
        s_Activity = null;
        nativeFinished(id, code, detail);
    }
}
