// <version.h> -*- C++ -*-
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

#ifndef AGHUIFFHUIHUI_VERSION_H
#define AGHUIFFHUIHUI_VERSION_H

#define SCAN2PDF_MAJOR 2
#define SCAN2PDF_MINOR 4
#define SCAN2PDF_PATCH 2
#define SCAN2PDF_BUILD 2

#include <string>

inline std::string_view get_scan2pdf_version()
{
    const static std::string version{std::to_string(SCAN2PDF_MAJOR) + "." + std::to_string(SCAN2PDF_MINOR) + "." + std::to_string(SCAN2PDF_PATCH) + "-" + std::to_string(SCAN2PDF_BUILD)};
    return version;
}

#endif // AGHUIFFHUIHUI_VERSION_H