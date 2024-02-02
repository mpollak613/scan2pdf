// <ocr_parsing.h> -*- C++ -*-
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

#ifndef OCR_PARSING_H
#define OCR_PARSING_H

#include <chrono>
#include <format>
#include <string>

#include "python.h"

std::string get_current_date()
{
    return std::format("{:%Y-%m-%d}", std::chrono::system_clock::now());
}

std::string parse_organization(const std::string& text, const std::string& default_return)
{
    if (std::string org{hyx::py_init::get_instance().import("guess_organization")->call("guess_organization", text)}; !org.empty()) [[likely]] {
        return org;
    }
    else [[unlikely]] {
        return default_return;
    }
}

#endif // OCR_PARSING_H