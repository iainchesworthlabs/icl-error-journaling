// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/haptics.hpp"
#include "error_journaling/share.hpp"
#include "support/event_log.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ej::test
{

/// The bridge's `Share` and `Haptics` for the tests, recorded. A sheet ends the way the test says,
/// either at once, before `present` returns (the desktop stub does), or later, from
/// `complete_share_sheet` (iOS does). Haptics and sheet events go into the `EventLog` when there is one.
class FakePlatform final : public ej::bridge::Share, public ej::bridge::Haptics
{
public:
    using share_result = ej::bridge::ShareResult;
    using share_outcome = ej::bridge::ShareOutcome;
    using haptic_kind = ej::bridge::HapticKind;

    explicit FakePlatform(EventLog* log = nullptr) : m_Log(log) {}

    [[nodiscard]] bool available() const noexcept override
    {
        return true;
    }
    void present(ej::bridge::ShareRequest request, ej::bridge::ShareCallback done) override;
    void prepare() override
    {
        ++m_Prepared;
    }
    void fire(haptic_kind kind) override;

    /// How the sheets that follow end, immediately. The default is `shared`.
    void answer_with(share_outcome outcome, std::string activity_type = {});

    /// The sheets that follow do not end until `complete_share_sheet`, as on a phone.
    void hold_completion(bool hold = true)
    {
        m_Hold = hold;
    }

    /// Ends the sheet that is open. False when none is.
    bool complete_share_sheet(share_outcome outcome, std::string activity_type = {});

    [[nodiscard]] bool sheet_open() const
    {
        return m_Held.has_value();
    }
    /// The anchor of each sheet that was presented, in order (unset when the share named none).
    [[nodiscard]] const std::vector<std::optional<ej::bridge::ShareAnchor>>& anchors() const
    {
        return m_Anchors;
    }
    [[nodiscard]] std::size_t sheets_presented() const
    {
        return m_Texts.size();
    }
    [[nodiscard]] const std::vector<std::string>& shared_texts() const
    {
        return m_Texts;
    }
    [[nodiscard]] const std::vector<haptic_kind>& haptics() const
    {
        return m_Haptics;
    }
    [[nodiscard]] std::size_t haptic_count(haptic_kind kind) const;
    [[nodiscard]] int prepared() const
    {
        return m_Prepared;
    }

private:
    void finish(ej::bridge::ShareCallback done, const share_result& result);

    EventLog* m_Log;
    share_result m_Next{share_outcome::SHARED, {}, {}};
    bool m_Hold = false;
    std::optional<ej::bridge::ShareCallback> m_Held;
    std::vector<std::string> m_Texts;
    std::vector<std::optional<ej::bridge::ShareAnchor>> m_Anchors;
    std::vector<haptic_kind> m_Haptics;
    int m_Prepared = 0;
};

[[nodiscard]] std::string_view haptic_name(ej::bridge::HapticKind kind);
[[nodiscard]] std::string_view share_outcome_name(ej::bridge::ShareOutcome outcome);

} // namespace ej::test
