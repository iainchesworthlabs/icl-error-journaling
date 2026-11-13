// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/person.hpp"

#include "error_journaling/utf8.hpp"

#include <QByteArray>
#include <QChar>
#include <QString>
#include <vector>

namespace ej
{

namespace
{

QString from_utf8(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string to_utf8(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

/// The code points of `text`.
std::vector<char32_t> code_points(const QString& text)
{
    std::vector<char32_t> result;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
        {
            result.push_back(QChar::surrogateToUcs4(c, text.at(i + 1)));
            ++i;
        }
        else
        {
            result.push_back(c.unicode());
        }
    }
    return result;
}

std::string from_code_points(const std::vector<char32_t>& points)
{
    QString text;
    for (const char32_t point : points)
    {
        text.append(QString::fromUcs4(&point, 1));
    }
    return to_utf8(text);
}

bool is_separator(char32_t point)
{
    return QChar::isSpace(point);
}

}  // namespace

Result<std::string, NameError> normalise_person_name(std::string_view typed)
{
    if (!utf8_code_points(typed))
    {
        return fail(NameError::NOT_UTF8);
    }
    std::vector<char32_t> out;
    bool pending_space = false;
    for (const char32_t point : code_points(from_utf8(typed)))
    {
        // A control character that is not white space is not part of a name either.
        if (is_separator(point) || point < 0x20U || point == 0x7FU)
        {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space)
        {
            out.push_back(U' ');
            pending_space = false;
        }
        out.push_back(point);
    }
    if (out.empty())
    {
        return fail(NameError::EMPTY);
    }
    if (out.size() > MAX_PERSON_NAME_CODE_POINTS)
    {
        return fail(NameError::TOO_LONG);
    }
    return from_code_points(out);
}

std::string person_name_key(std::string_view name)
{
    const auto normal = normalise_person_name(name);
    if (!normal)
    {
        return {};
    }
    const QString decomposed = from_utf8(*normal).normalized(QString::NormalizationForm_D);
    QString stripped;
    for (const QChar c : decomposed)
    {
        const auto category = c.category();
        if (category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining ||
            category == QChar::Mark_Enclosing)
        {
            continue;
        }
        stripped.append(c);
    }
    return to_utf8(stripped.toCaseFolded().normalized(QString::NormalizationForm_C));
}

bool person_names_match(std::string_view a, std::string_view b)
{
    const std::string key_a = person_name_key(a);
    return !key_a.empty() && key_a == person_name_key(b);
}

std::string_view name_level_key(NameLevel level)
{
    switch (level)
    {
    case NameLevel::FULL:
        return "full";
    case NameLevel::INITIALS:
        return "initials";
    case NameLevel::HIDDEN:
        return "hidden";
    }
    return {};
}

std::optional<NameLevel> name_level_from_key(std::string_view key)
{
    for (const NameLevel level : {NameLevel::FULL, NameLevel::INITIALS, NameLevel::HIDDEN})
    {
        if (name_level_key(level) == key)
        {
            return level;
        }
    }
    return std::nullopt;
}

std::string person_initials(std::string_view name)
{
    std::vector<char32_t> out;
    bool at_word_start = true;
    for (const char32_t point : code_points(from_utf8(name)))
    {
        if (is_separator(point) || point == U'-')
        {
            at_word_start = true;
            continue;
        }
        if (at_word_start)
        {
            out.push_back(QChar::toUpper(point));
            out.push_back(U'.');
            at_word_start = false;
        }
    }
    return from_code_points(out);
}

std::string person_name_at_level(std::string_view name, NameLevel level)
{
    switch (level)
    {
    case NameLevel::FULL:
        return std::string(name);
    case NameLevel::INITIALS:
    {
        auto initials = person_initials(name);
        return initials.empty() ? std::string(HIDDEN_NAME) : initials;
    }
    case NameLevel::HIDDEN:
        break;
    }
    return std::string(HIDDEN_NAME);
}

std::string render_people(std::span<const std::optional<std::string>> names, NameLevel level)
{
    if (names.empty())
    {
        return std::string(PERSON_NOT_SET);
    }
    if (level == NameLevel::HIDDEN)
    {
        return std::string(HIDDEN_NAME);
    }
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (i != 0)
        {
            out += i + 1 == names.size() ? " and " : ", ";
        }
        out += names[i] ? person_name_at_level(*names[i], level) : std::string(HIDDEN_NAME);
    }
    return out;
}

}  // namespace ej
