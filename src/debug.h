// <debug.h> -*- C++ -*-
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

#ifndef IAABJKFAFNJKUOEF_DEBUG_H
#define IAABJKFAFNJKUOEF_DEBUG_H

#include <Magick++.h>
#include <hyx/filesystem.h>
#include <string>

constexpr void dump_image([[maybe_unused]] Magick::Image& img, [[maybe_unused]] const std::string& name)
{
#ifdef DEBUG
    constinit static int idx{};

    img.write(hyx::home_path() / "Downloads/tmp" / ("debugged_image_" + std::to_string(idx) + name + ".png"));
    ++idx;
#endif
}

#endif // IAABJKFAFNJKUOEF_DEBUG_H