// <image_transformations.h> -*- C++ -*-
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

#ifndef IMAGE_TRANSFORMATIONS_H
#define IMAGE_TRANSFORMATIONS_H

#include <Magick++.h>
#include <hyx/logger.h>
#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

void transform_to_bw(hyx::logger& logger, Magick::Image& image)
{
    logger(hyx::logger_literals::debug, "Converting to black and white\n");

    constexpr auto brightness{0.0};
    constexpr auto constrast{30.0};
    image.brightnessContrast(brightness, constrast);
    image.autoThreshold(Magick::KapurThresholdMethod);
}

void transform_with_text_to_bw(hyx::logger& logger, Magick::Image& image)
{
    logger(hyx::logger_literals::debug, "Converting to black and white\n");

    // magick in.png -auto-level -unsharp 0x2+1.5+0.05 -resize 200% -auto-threshold otsu out.pdf
    // TODO: use -colorspace gray -auto-level -negate -lat 30x30+10% -negate

    image.autoLevel();
    constexpr auto unsharp_radius{0.0};
    constexpr auto unsharp_sigma{2.0};
    constexpr auto unsharp_amount{1.5};
    constexpr auto unsharp_threshold{0.05};
    image.unsharpmask(unsharp_radius, unsharp_sigma, unsharp_amount, unsharp_threshold);
    image.autoThreshold(Magick::OTSUThresholdMethod);
}

void transform_to_grayscale(hyx::logger& logger, Magick::Image& image)
{
    logger(hyx::logger_literals::debug, "Converting to greyscale\n");

    constexpr auto brightness{0.0};
    constexpr auto contrast{30.0};
    image.brightnessContrast(brightness, contrast);
    image.opaque(Magick::Color("white"), Magick::Color("white"));
    image.colorSpace(Magick::LinearGRAYColorspace);
}

PIX* magick2pix(Magick::Image& image)
{
    Magick::Blob bimage;
    image.write(&bimage, "tiff");
    return pixReadMemTiff(static_cast<const unsigned char*>(bimage.data()), bimage.length(), 0);
}

#endif // IMAGE_TRANSFORMATIONS_H