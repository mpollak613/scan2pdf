// <image_type.h> -*- C++ -*-
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

#ifndef IMAGE_TYPE_H
#define IMAGE_TYPE_H

#include <Magick++.h>
#include <hyx/logger.h>
#include <leptonica/allheaders.h>
#include <memory>
#include <string>
#include <string_view>
#include <tesseract/baseapi.h>

#include "units.h"

bool is_grayscale(hyx::logger& logger, Magick::Image image)
{
    // constinit static const auto log_prefix{"Grayscale Check"};

    // magick in.png -colorspace HSB -resize 2% -format "%[fx:mean.g] %[fx:maxima.g]\n" info:-

    image.colorSpace(Magick::HSBColorspace);
    image.resize("2%");

    const auto image_saturation_stats{image.statistics().channel(Magick::PixelChannel::GreenPixelChannel)};
    const auto mean_saturation{quantum_to_percent(image_saturation_stats.mean())};
    const auto maxima_saturation{quantum_to_percent(image_saturation_stats.maxima())};

    logger(hyx::logger_literals::debug, "Saturation mean: {}%\n", mean_saturation);
    logger(hyx::logger_literals::debug, "Saturation maxima: {}%\n", maxima_saturation);

    constexpr auto mean_threashold{5.0};
    constexpr auto maxima_threashold{10.0};
    // if we have small mean saturation and no large spike (maxima) of saturation anywhere, image is grayscale
    if (mean_saturation < mean_threashold && maxima_saturation < maxima_threashold) {
        return true;
    }

    // if we have high mean, image is not grayscale
    // if we have high maxima, image contains some significant color (e.g., logo)
    return false;
}

bool is_bw(hyx::logger& logger, Magick::Image image)
{
    // constinit static const auto log_prefix{"BW Check"};

    // magick in.png -solarize 50% -colorspace gray -identify -verbose info:

    image.solarize(50_quantum_percent);
    image.colorSpace(Magick::GRAYColorspace);

    auto image_gray_stats{image.statistics().channel(Magick::PixelChannel::GrayPixelChannel)};
    auto mean_gray{quantum_to_percent(image_gray_stats.mean())};
    auto stddev_gray{quantum_to_percent(image_gray_stats.standardDeviation())};

    logger(hyx::logger_literals::debug, "Gray mean: {}\n", mean_gray);
    logger(hyx::logger_literals::debug, "Gray standard deviation: {}\n", stddev_gray);

    constexpr auto mean_threshold{12.0};
    constexpr auto stddev_threshold{18.0};
    constexpr auto stddev_mean_diff_threashold{-0.6};
    // if we have close to zero mean and small, but larger than mean, deviation, image is bw
    // (we will allow a small padding for close deviation and mean difference to prefer bw over other options when it's unclear)
    if (mean_gray < mean_threshold && stddev_gray < stddev_threshold && (stddev_gray - mean_gray) > stddev_mean_diff_threashold) {
        return true;
    }

    // if we have any other case, image is not bw
    return false;
}

bool is_white(hyx::logger& logger, Magick::Image image)
{
    // constinit static const auto log_prefix{"All White Check"};

    image.whiteThreshold("75%");
    const auto percent_white{std::stod(image.formatExpression("%[fx:mean]"))};

    logger(hyx::logger_literals::debug, "Percent white: {}\n", percent_white);

    constexpr auto percent_white_threshold{0.9999};
    if (percent_white > percent_white_threshold) {
        return true;
    }

    return false;
}

bool has_text(PIX* pimage, tesseract::TessBaseAPI* tess_api)
{
    tess_api->SetImage(pimage);
    std::unique_ptr<char> image_text{tess_api->GetUTF8Text()};
    return !std::string_view(image_text.get()).empty();
}

#endif // IMAGE_TYPE_H