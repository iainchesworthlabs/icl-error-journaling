// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ios_bridge.hpp"
#include "support/event_log.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ej::test
{

/// `platform_services` for the tests: the share sheet and the haptics, recorded. A sheet ends the way the test says,
/// either at once, before `present_share_sheet` returns (the desktop stub does), or later, from
/// `complete_share_sheet` (iOS does). Haptics and sheet events go into the `EventLog` when there is one.
class FakePlatform final : public error_journaling::bridge::platform_services
{
public:
    using share_result = error_journaling::bridge::share_result;
    using share_outcome = error_journaling::bridge::share_outcome;
    using haptic_kind = error_journaling::bridge::haptic_kind;

    explicit FakePlatform(EventLog* log = nullptr) : m_Log(log) {}

    void present_share_sheet(std::string_view text, error_journaling::bridge::share_callback done) override;
    void prepare_haptics() override
    {
        ++m_Prepared;
    }
    void fire_haptic(haptic_kind kind) override;

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
    void finish(error_journaling::bridge::share_callback done, const share_result& result);

    EventLog* m_Log;
    share_result m_Next{share_outcome::shared, {}};
    bool m_Hold = false;
    std::optional<error_journaling::bridge::share_callback> m_Held;
    std::vector<std::string> m_Texts;
    std::vector<haptic_kind> m_Haptics;
    int m_Prepared = 0;
};

[[nodiscard]] std::string_view haptic_name(error_journaling::bridge::haptic_kind kind);
[[nodiscard]] std::string_view share_outcome_name(error_journaling::bridge::share_outcome outcome);

} // namespace ej::test
