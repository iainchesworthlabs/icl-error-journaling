// SPDX-License-Identifier: GPL-3.0-or-later
package com.iainchesworthlabs.fieldtest;

import androidx.core.content.FileProvider;

/**
 * Gives other apps a content:// URI for a file the application shares (the configuration export, or a long text), with
 * permission to read it for that one share. A class of its own, because Qt's manifest already declares androidx's
 * FileProvider for its own files and the manifest merger refuses the same component twice. The files it may serve are
 * listed in res/xml/ej_file_paths.xml.
 */
public final class EjFileProvider extends FileProvider
{
}
