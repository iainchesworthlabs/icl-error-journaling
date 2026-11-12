// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.fieldtest;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Setup files from other apps (docs/plan/stage-9.md, "File picker and open-with"): the document picker (Settings > Import,
 * the first-launch offer) and "Open with" or "Share to" from Slack, Mail and Files. Android hands over a content:// stream;
 * this class copies it into a private file and tells C++, which treats it like any other setup file: the importer checks
 * the content (never the name or the type), and deletes the copy on every outcome. Everything that decides something is
 * in C++ (android_import_plan.cpp).
 *
 * C++ calls configure() once its natives are registered. A file that arrives before that (the application was started by
 * "Open with") waits as a Uri, and is copied then. The stream is copied on a background thread.
 */
public final class EjDocuments
{
    private static final int REQUEST_PICK = 4401;

    private static final Object s_Lock = new Object();
    private static final ExecutorService s_Copier = Executors.newSingleThreadExecutor();

    private static String s_Directory = null; // null until C++ has called configure()
    private static long s_Cap = 0;
    private static Uri s_PendingOpen = null;
    private static boolean s_PickPending = false;
    private static long s_PickId = 0;

    private EjDocuments()
    {
    }

    private static native void nativePicked(long id, int code, String detail);

    private static native void nativeOpened(String path);

    /** From C++, once the natives are registered: where the copies go and how many bytes of a stream are copied. */
    public static void configure(Activity activity, String directory, long cap)
    {
        Uri waiting;
        synchronized (s_Lock)
        {
            s_Directory = directory;
            s_Cap = cap;
            waiting = s_PendingOpen;
            s_PendingOpen = null;
        }
        if (waiting != null)
        {
            copyAndOpen(activity, waiting);
        }
    }

    /** Shows the document picker. `mimeTypes` is a comma-separated list. Called from the Qt thread. */
    public static void pick(final Activity activity, final long id, final String mimeTypes)
    {
        activity.runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                startPicker(activity, id, mimeTypes);
            }
        });
    }

    private static void startPicker(Activity activity, long id, String mimeTypes)
    {
        synchronized (s_Lock)
        {
            if (s_PickPending)
            {
                nativePicked(id, 2, "a picker is already open");
                return;
            }
            s_PickPending = true;
            s_PickId = id;
        }
        try
        {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes.split(","));
            activity.startActivityForResult(intent, REQUEST_PICK);
        }
        catch (Exception e)
        {
            finishPick(2, e.getClass().getSimpleName());
        }
    }

    /** From EjActivity.onActivityResult. Returns true when the result was the picker's. */
    public static boolean onActivityResult(final Activity activity, int requestCode, int resultCode, Intent data)
    {
        if (requestCode != REQUEST_PICK)
        {
            return false;
        }
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null)
        {
            finishPick(1, "");
            return true;
        }
        final Uri uri = data.getData();
        s_Copier.execute(new Runnable()
        {
            @Override
            public void run()
            {
                try
                {
                    finishPick(0, copy(activity, uri));
                }
                catch (IOException | RuntimeException e)
                {
                    finishPick(2, e.getClass().getSimpleName());
                }
            }
        });
        return true;
    }

    private static void finishPick(int code, String detail)
    {
        long id;
        synchronized (s_Lock)
        {
            if (!s_PickPending)
            {
                return;
            }
            s_PickPending = false;
            id = s_PickId;
        }
        nativePicked(id, code, detail);
    }

    /**
     * From EjActivity.onCreate and onNewIntent: "Open with" (ACTION_VIEW) and "Share to" (ACTION_SEND) of a setup file.
     * Any other intent, the launcher's included, is ignored.
     */
    public static void onIntent(Activity activity, Intent intent)
    {
        if (intent == null)
        {
            return;
        }
        Uri uri = null;
        if (Intent.ACTION_VIEW.equals(intent.getAction()))
        {
            uri = intent.getData();
        }
        else if (Intent.ACTION_SEND.equals(intent.getAction()))
        {
            uri = intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class);
        }
        if (uri == null)
        {
            return;
        }
        boolean ready;
        synchronized (s_Lock)
        {
            ready = s_Directory != null;
            if (!ready)
            {
                s_PendingOpen = uri;
            }
        }
        if (ready)
        {
            copyAndOpen(activity, uri);
        }
    }

    private static void copyAndOpen(final Activity activity, final Uri uri)
    {
        s_Copier.execute(new Runnable()
        {
            @Override
            public void run()
            {
                try
                {
                    nativeOpened(copy(activity, uri));
                }
                catch (IOException | RuntimeException e)
                {
                    // Nothing to open: the person sees no import start. The cause is a stream that cannot be read.
                    android.util.Log.w("ej", "open: the file could not be copied (" + e.getClass().getSimpleName() + ")");
                }
            }
        });
    }

    /** Copies at most the cap of the stream into a new file of the import directory, and returns its path. */
    private static String copy(Context context, Uri uri) throws IOException
    {
        String directory;
        long cap;
        synchronized (s_Lock)
        {
            directory = s_Directory;
            cap = s_Cap;
        }
        if (directory == null)
        {
            throw new IOException("not configured");
        }
        File parent = new File(directory);
        if (!parent.isDirectory() && !parent.mkdirs())
        {
            throw new IOException("no directory");
        }
        File out = new File(parent, "setup-" + System.nanoTime() + ".ejconfig");
        try (InputStream in = context.getContentResolver().openInputStream(uri))
        {
            if (in == null)
            {
                throw new IOException("no stream");
            }
            try (OutputStream stream = new FileOutputStream(out))
            {
                byte[] buffer = new byte[8192];
                long total = 0;
                int read;
                while (total < cap && (read = in.read(buffer)) > 0)
                {
                    int write = (int) Math.min(read, cap - total);
                    stream.write(buffer, 0, write);
                    total += write;
                }
            }
        }
        catch (IOException | RuntimeException e)
        {
            out.delete();
            throw e;
        }
        return out.getAbsolutePath();
    }
}
