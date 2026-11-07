// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viewmodel/clipboard.hpp"

#include <string_view>

namespace ej::app
{

/// The system clipboard over `QClipboard` (QtGui, which is why it is not in `ej_viewmodel`). The Copy buttons of the
/// review screen reach it through `viewmodel::Clipboard` (REV-04). GUI thread only; needs a `QGuiApplication`.
class QtClipboard final : public viewmodel::Clipboard
{
public:
    /// Replaces the clipboard's text. The bytes are UTF-8 and are converted without any change to the text, so what
    /// the person pastes is what the export holds, line endings included.
    void set_text(std::string_view text) override;
};

} // namespace ej::app
