// <units.h> -*- C++ -*-
// Copyright (C) 2024 Michael Pollak

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#ifndef UNITS_H
#define UNITS_H

#include <Magick++.h>

constexpr long double quantum_as_rgb(long double quantum_val)
{
    constexpr auto rgb_max{255.0L};
    return (quantum_val / MaxMap) * rgb_max;
}

constexpr double percent_to_quantum(std::convertible_to<double> auto percent)
{
    constexpr auto percent_max{100.0};
    return (static_cast<double>(percent) / percent_max) * MaxMap;
}

constexpr double quantum_to_percent(std::convertible_to<double> auto quantum)
{
    constexpr auto percent_max{100.0};
    return (static_cast<double>(quantum) / MaxMap) * percent_max;
}

consteval double operator""_quantum_percent(long double percent)
{
    return percent_to_quantum(percent);
}
consteval double operator"" _quantum_percent(unsigned long long percent)
{
    return percent_to_quantum(percent);
}

#endif // UNITS_H