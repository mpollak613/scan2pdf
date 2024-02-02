/**
 * @file hyx_leptonica.h
 * @copyright
 * Copyright 2023 Michael Pollak.
 * All rights reserved.
 */

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
