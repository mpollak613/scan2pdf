// <image_details.h> -*- C++ -*-
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

#ifndef IMAGE_DETAILS_H
#define IMAGE_DETAILS_H

#include <Magick++.h>
#include <hyx/logger.h>
#include <leptonica/allheaders.h>
#include <memory>
#include <string>
#include <tesseract/baseapi.h>

#include "debug.h"
#include "units.h"

double get_deskew_angle(hyx::logger& logger, Magick::Image image)
{
    logger(hyx::logger_literals::debug, "Getting deskew angle\n");

    // reducing the image size will greatly improve deskew time and accuracy due to less pixels and 'fuzzing'
    image.resize("10%");
    image.autoThreshold(Magick::OTSUThresholdMethod);
    image.deskew(80_quantum_percent);

    return std::stod(image.artifact("deskew:angle"));
}

void deskew(hyx::logger& logger, Magick::Image& image)
{
    logger("Deskewing image\n");

    // TODO: replace 'true' with has_text for the image
    if (true) {
        auto angle{0.0};
        auto angle_delta{1.0};

        const auto count_white_lines{[](Magick::Image image, const auto ang) {
            image.rotate(ang);
            image.negate();
            image.autoThreshold(Magick::OTSUThresholdMethod);
            image.negate();
            image.scale("1x!");
        }};
    }
    else {
        // use code below
    }

    // Find the color of the scan background.
    const auto background_color{image.pixelColor(5, 5)};
    image.backgroundColor(background_color);
    logger(hyx::logger_literals::debug, "Set background color to ({},{},{})\n", quantum_as_rgb(background_color.quantumRed()), quantum_as_rgb(background_color.quantumGreen()), quantum_as_rgb(background_color.quantumBlue()));

    const auto deskew_angle{get_deskew_angle(logger, image)};
    image.rotate(deskew_angle);
    logger(hyx::logger_literals::debug, "Deskewed by {} degrees\n", deskew_angle);

    dump_image(image, "deskewed");
}

Magick::Geometry get_trim_edges_bounds(hyx::logger& logger, Magick::Image image)
{
    // magick in.png -fuzz 10% -format "%[minimum-bounding-box]\n" info:

    logger("Trimming edges\n");

    // the colorFuzz value will influence the Minimum Bounding Rectangle
    image.colorFuzz(10_quantum_percent);

    const auto bb{image.boundingBox()};
    logger(hyx::logger_literals::debug, "Image bounding Box: {}x{}{:+}{:+}\n", bb.width(), bb.height(), bb.xOff(), bb.yOff());

    return bb;
}

Magick::Geometry get_trim_shadow_bounds(hyx::logger& logger, Magick::Image image)
{
    logger("Trimming shadow\n");

    constexpr auto gamma_fix{2.2};
    image.gamma(gamma_fix);
    constexpr auto blur_radius{0.0};
    constexpr auto blur_sigma{5.0};
    image.adaptiveBlur(blur_radius, blur_sigma);
    image.negate();
    image.autoThreshold(Magick::OTSUThresholdMethod);
    image.negate();
    image.artifact("trim:percent-background", "2");
    image.artifact("trim:background-color", "black");

    dump_image(image, "trim_shadow_before_trim");

    auto image_canvas{image.size()};
    logger(hyx::logger_literals::debug, "Starting dimentions: {}x{}{:+}{:+}\n", image_canvas.width(), image_canvas.height(), image_canvas.xOff(), image_canvas.yOff());
    try {
        for ([[maybe_unused]] auto _ : std::views::iota(0, 10)) {
            const auto before_trim_image_canvas = image.size();
            image.trim();
            const auto after_trim_image_canvas = image.size();
            logger(hyx::logger_literals::debug, "After trim dimentions: {}x{}{:+}{:+}\n", after_trim_image_canvas.width(), after_trim_image_canvas.height(), after_trim_image_canvas.xOff(), after_trim_image_canvas.yOff());

            constexpr auto min_image_dims{500};
            if (image.size().height() < min_image_dims || image.size().width() < min_image_dims) {
                throw std::runtime_error("image is too small after trim");
            }
            else if (before_trim_image_canvas != after_trim_image_canvas) {
                // ok, we removed the shadow
                break;
            }
            // else

            // maybe the shadow is not at the edge of the image?
            // we will try to dig one pixel at a time to find the shadow
            logger(hyx::logger_literals::debug, "Removing pixel line to find shadow\n");
            image.crop({0, 0, 0, -1});
            image.repage();
        }
    }
    catch (const std::exception& e) {
        logger(hyx::logger_literals::warning, "Failed to trim shadow: {}\n", e.what());
    }

    image.size(image_canvas);
    dump_image(image, "trim_shadow_after_trim");

    return image_canvas;
}

int get_orientation(hyx::logger& logger, tesseract::TessBaseAPI* tess_api, PIX* pimage)
{
    logger("Getting orientation\n");
    tess_api->SetImage(pimage);

    // default to not changing the orientation
    int ori_deg{0};
    //// float ori_conf;
    //// const char* script_name;
    //// float script_conf;
    tess_api->DetectOrientationScript(&ori_deg, nullptr, nullptr, nullptr);

    logger(hyx::logger_literals::debug, "Orientation off by {} degrees\n", ori_deg);

    return ori_deg;
}

std::string get_text(tesseract::TessBaseAPI* tess_api, PIX* pimage)
{
    tess_api->SetImage(pimage);
    auto ocr_text{std::unique_ptr<char[]>(tess_api->GetUTF8Text())};
    return (ocr_text) ? ocr_text.get() : "";
}

#endif // IMAGE_DETAILS_H