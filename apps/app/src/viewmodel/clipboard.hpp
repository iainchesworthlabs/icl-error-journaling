// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace ej::viewmodel
{

/// The system clipboard, behind an interface so that `ej_viewmodel` needs no QtGui. The Copy buttons of the review
/// screen call it (REV-04). The implementation over `QClipboard` is `apps/app/src/qt_clipboard.*` (3.11).
class Clipboard
{
public:
    Clipboard() = default;
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    virtual ~Clipboard() = default;

    /// Replaces the clipboard's text, UTF-8, byte for byte.
    virtual void set_text(std::string_view text) = 0;
};

} // namespace ej::viewmodel
