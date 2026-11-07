// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <functional>
#include <memory>

namespace ej::bridge
{

enum class PickOutcome
{
    PICKED,      ///< A file was chosen; `m_Path` is a private copy of it.
    CANCELLED,   ///< The person dismissed the picker.
    UNAVAILABLE, ///< This platform has no document picker (the desktop stub; it uses Qt's file dialog from QML).
    FAILED,      ///< The picker could not be shown.
};

struct PickResult
{
    PickOutcome m_Outcome = PickOutcome::UNAVAILABLE;
    std::filesystem::path m_Path;
};

/// The system document picker, for the setup file (Settings > Import, and the first-launch offer). Every call is made
/// on the main thread, and `done` is called there.
class DocumentPicker
{
public:
    DocumentPicker() = default;
    DocumentPicker(const DocumentPicker&) = delete;
    DocumentPicker& operator=(const DocumentPicker&) = delete;
    virtual ~DocumentPicker() = default;

    /// Shows the picker for the setup type and plain JSON. `done` is called exactly once, after this function has
    /// returned. The file is a copy in the application's own temporary space, which the caller deletes.
    virtual void pick_configuration(std::function<void(const PickResult&)> done) = 0;
};

/// ios/document_picker.mm or desktop/document_picker.cpp (UNAVAILABLE).
[[nodiscard]] std::unique_ptr<DocumentPicker> make_document_picker();

} // namespace ej::bridge
