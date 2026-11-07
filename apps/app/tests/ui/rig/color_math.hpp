// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>

#include <algorithm>
#include <array>
#include <cmath>

namespace ej::ui
{

/// The colour arithmetic of the accessibility tests: WCAG 2.x contrast, the hue of a colour, and the distance between two
/// colours as a person with deuteranopia (no medium-wavelength cones) sees them.

[[nodiscard]] inline double linear_channel(double value)
{
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

/// WCAG relative luminance, 0 (black) to 1 (white).
[[nodiscard]] inline double relative_luminance(const QColor& colour)
{
    return 0.2126 * linear_channel(colour.redF()) + 0.7152 * linear_channel(colour.greenF()) +
           0.0722 * linear_channel(colour.blueF());
}

/// WCAG contrast ratio, 1 to 21, whichever colour is the lighter.
[[nodiscard]] inline double contrast_ratio(const QColor& a, const QColor& b)
{
    const double la = relative_luminance(a);
    const double lb = relative_luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// Hue in degrees, 0 to 360.
[[nodiscard]] inline double hue_degrees(const QColor& colour)
{
    return colour.hslHueF() * 360.0;
}

/// A colour seen with deuteranopia, by the Machado, Oliveira and Fernandes (2009) matrix at full severity, applied in
/// linear light. Returns linear RGB.
[[nodiscard]] inline std::array<double, 3> deuteranopia_linear(const QColor& colour)
{
    const double r = linear_channel(colour.redF());
    const double g = linear_channel(colour.greenF());
    const double b = linear_channel(colour.blueF());
    const auto clamp = [](double v) { return std::clamp(v, 0.0, 1.0); };
    return {clamp(0.367322 * r + 0.860646 * g - 0.227968 * b), clamp(0.280085 * r + 0.672501 * g + 0.047413 * b),
            clamp(-0.011820 * r + 0.042940 * g + 0.968881 * b)};
}

/// CIE L*a*b* of a linear RGB triple (D65).
[[nodiscard]] inline std::array<double, 3> lab_of(const std::array<double, 3>& rgb)
{
    const double x = (0.4124 * rgb[0] + 0.3576 * rgb[1] + 0.1805 * rgb[2]) / 0.95047;
    const double y = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
    const double z = (0.0193 * rgb[0] + 0.1192 * rgb[1] + 0.9505 * rgb[2]) / 1.08883;
    const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    return {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
}

/// The CIE76 distance between two colours as a person with deuteranopia sees them. Two colours that look the same to
/// them are under about 2.
[[nodiscard]] inline double deuteranopia_distance(const QColor& a, const QColor& b)
{
    const auto la = lab_of(deuteranopia_linear(a));
    const auto lb = lab_of(deuteranopia_linear(b));
    return std::sqrt(std::pow(la[0] - lb[0], 2) + std::pow(la[1] - lb[1], 2) + std::pow(la[2] - lb[2], 2));
}

} // namespace ej::ui
