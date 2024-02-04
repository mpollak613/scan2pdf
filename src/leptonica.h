// <hyx_leptonica.h> -*- C++ -*-
// Copyright (C) 2023-2024 Michael Pollak

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

#ifndef HYX_LEPTONICA_H
#define HYX_LEPTONICA_H

#include <leptonica/allheaders.h>
#include <memory>

namespace hyx {
    using unique_pixa = std::unique_ptr<PIXA, decltype([](PIXA* pixa) { pixaDestroy(&pixa); })>;
    using unique_pix = std::unique_ptr<PIX, decltype([](Pix* pix) { pixDestroy(&pix); })>;
    using unique_boxa = std::unique_ptr<BOXA, decltype([](Boxa* boxa) { boxaDestroy(&boxa); })>;
    using unique_box = std::unique_ptr<BOX, decltype([](Box* box) { boxDestroy(&box); })>;
} // namespace hyx

#endif // HYX_LEPTONICA_H
