// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/share_presenter.hpp"

#include <system_error>
#include <utility>

namespace ej::bridge
{

struct SharePresenter::State
{
    bool m_InFlight = false;
};

namespace
{

bool is_continuation(unsigned char byte) noexcept
{
    return (byte & 0xC0U) == 0x80U;
}

void remove_quietly(const std::filesystem::path& file) noexcept
{
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

} // namespace

bool is_valid_utf8(std::string_view text) noexcept
{
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n)
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80U)
        {
            ++i;
            continue;
        }
        std::size_t length = 0;
        char32_t value = 0;
        char32_t minimum = 0;
        if (lead >= 0xC2U && lead <= 0xDFU)
        {
            length = 2;
            value = lead & 0x1FU;
            minimum = 0x80;
        }
        else if (lead >= 0xE0U && lead <= 0xEFU)
        {
            length = 3;
            value = lead & 0x0FU;
            minimum = 0x800;
        }
        else if (lead >= 0xF0U && lead <= 0xF4U)
        {
            length = 4;
            value = lead & 0x07U;
            minimum = 0x10000;
        }
        else
        {
            return false;
        }
        if (i + length > n)
        {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k)
        {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if (!is_continuation(next))
            {
                return false;
            }
            value = (value << 6U) | (next & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
        {
            return false;
        }
        i += length;
    }
    return true;
}

SharePresenter::SharePresenter(Share& share) : m_Share(share), m_State(std::make_shared<State>()) {}

SharePresenter::~SharePresenter() = default;

bool SharePresenter::in_flight() const noexcept
{
    return m_State->m_InFlight;
}

void SharePresenter::present(ShareRequest request, ShareCallback done)
{
    const bool has_text = !request.m_Text.empty();
    const bool has_file = !request.m_File.empty();
    const bool valid = (has_text != has_file) && (!has_text || is_valid_utf8(request.m_Text));
    if (!valid || m_State->m_InFlight)
    {
        if (done)
        {
            done(ShareResult{ShareOutcome::FAILED, {}, {}});
        }
        return;
    }

    m_State->m_InFlight = true;
    // The state lives on in the closure, so a completion after the presenter is gone only clears a flag nobody reads.
    m_Share.present(std::move(request),
                    [state = m_State, done = std::move(done)](const ShareResult& result)
                    {
                        state->m_InFlight = false;
                        if (done)
                        {
                            done(result);
                        }
                    });
}

void SharePresenter::present_temporary_file(const std::filesystem::path& file, ShareCallback done)
{
    ShareRequest request;
    request.m_File = file;
    present(std::move(request),
            [file, done = std::move(done)](const ShareResult& result)
            {
                remove_quietly(file);
                if (done)
                {
                    done(result);
                }
            });
}

std::size_t purge_directory(const std::filesystem::path& directory) noexcept
{
    std::size_t removed = 0;
    std::error_code error;
    std::filesystem::directory_iterator it(directory, error);
    if (error)
    {
        return 0;
    }
    for (const std::filesystem::directory_iterator end; it != end; it.increment(error))
    {
        if (error)
        {
            break;
        }
        std::error_code status_error;
        if (it->is_regular_file(status_error) && std::filesystem::remove(it->path(), status_error))
        {
            ++removed;
        }
    }
    return removed;
}

} // namespace ej::bridge
