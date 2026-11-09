// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 9: until package 9.4 has an Android implementation of this capability it is the desktop stub (a copy of
// ../desktop/document_picker.cpp). check_platform_layout.py wants one file per capability in every platform directory.
#include "error_journaling/document_picker.hpp"

namespace ej::bridge
{

namespace
{

class DesktopDocumentPicker final : public DocumentPicker
{
public:
    void pick_configuration(std::function<void(const PickResult&)> done) override
    {
        if (done)
        {
            done(PickResult{PickOutcome::UNAVAILABLE, {}});
        }
    }
};

} // namespace

std::unique_ptr<DocumentPicker> make_document_picker()
{
    return std::make_unique<DesktopDocumentPicker>();
}

} // namespace ej::bridge
