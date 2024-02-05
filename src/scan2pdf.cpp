// <scan2pdf.cpp> -*- C++ -*-
// Copyright (C) 2022-2024 Michael Pollak

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

// Python defines many things that need to come before other imports
#include <Python.h> // non-standard

#include <Magick++.h> // non-standard
#include <any>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <hyx/circular_buffer.h> // non-standard
#include <hyx/filesystem.h>      // non-standard
#include <hyx/logger.h>          // non-standard
#include <hyx/parser.h>          // non-standard
#include <iostream>
#include <leptonica/allheaders.h> // non-standard
#include <limits>
#include <memory>
#include <numbers>
#include <ranges>
#include <regex>
#include <sane/sane.h>     // non-standard
#include <sane/saneopts.h> // non-standard
#include <stdexcept>
#include <string>
#include <tesseract/baseapi.h>  // non-standard
#include <tesseract/renderer.h> // non-standard
#include <thread>
#include <unordered_map>
#include <vector>

#include "debug.h"
#include "image_details.h"
#include "image_transformations.h"
#include "image_type.h"
#include "leptonica.h"
#include "ocr_parsing.h"
#include "python.h"
#include "sane.h"
#include "units.h"
#include "version.h"

//! TODO: allow for other image formats if libtiff is not available
#ifdef HAVE_LIBTIFF
#include <tiffio.h>   // non-standard
#include <tiffio.hxx> // non-standard
#endif                // !HAVE_LIBTIFF

hyx::logger logger(std::clog, "[cl::utc;%FT%TZ][[[::lvl;^9]]]: [sl::file_name;]@[sl::line;]: ");

/**
 * @brief Global options.
 */
//! TODO: global options should get stored in a safer way
static std::unordered_map<std::string, std::any> sane_options{
    {SANE_NAME_SCAN_SOURCE, "ADF Duplex"},
    {SANE_NAME_SCAN_MODE, SANE_VALUE_SCAN_MODE_COLOR},
    {SANE_NAME_SCAN_RESOLUTION, 300},
    {SANE_NAME_PAGE_HEIGHT, std::numeric_limits<SANE_Word>::max()},
    {SANE_NAME_PAGE_WIDTH, std::numeric_limits<SANE_Word>::max()},
    {"ald", false}};

Magick::Image get_next_image(hyx::sane_device* device)
{
    const auto sane_params{device->get_parameters()};

    std::ostringstream tiff_ostream;
    auto* tifffile{TIFFStreamOpen("tiff_frame", &tiff_ostream)};
    TIFFSetField(tifffile, TIFFTAG_IMAGEWIDTH, sane_params.pixels_per_line);
    TIFFSetField(tifffile, TIFFTAG_IMAGELENGTH, (sane_params.lines != -1) ? sane_params.lines : 0);
    TIFFSetField(tifffile, TIFFTAG_BITSPERSAMPLE, sane_params.depth);
    TIFFSetField(tifffile, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tifffile, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tifffile, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tifffile, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    TIFFSetField(tifffile, TIFFTAG_SOFTWARE, "scan2pdf");
    TIFFSetField(tifffile, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    TIFFSetField(tifffile, TIFFTAG_XRESOLUTION, 300);
    TIFFSetField(tifffile, TIFFTAG_YRESOLUTION, 300);
    TIFFSetField(tifffile, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tifffile, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tifffile, (uint32_t)-1));

    const auto buf_size{sane_params.bytes_per_line};
    std::unique_ptr<SANE_Byte, decltype(&_TIFFfree)> data{static_cast<SANE_Byte*>(_TIFFmalloc(buf_size)), &_TIFFfree};
    for (auto row{0u}; device->read(data.get(), buf_size); ++row) {
        if (TIFFWriteScanline(tifffile, data.get(), row) != 1) {
            logger(hyx::logger_literals::warning, "bad write!\n");
            throw std::runtime_error("Bad image write");
        }
    }

    //! FIXME: if we throw, the file is not closed
    TIFFClose(tifffile);

    // since we read the exact number of bytes per line we don't get EOF until the next call.
    // if the next call doesn't read zero bytes or return EOF, we have image bytes that were not read.
    if (device->read(data.get(), buf_size)) [[unlikely]] {
        throw std::runtime_error("Remaining bytes after image read");
    }

    return {Magick::Blob(tiff_ostream.view().data(), tiff_ostream.view().size())};
}

void print_help()
{
    std::cout << "Usage: scan2pdf [options...] file\n";
    std::cout << '\n';
    std::cout << "-r, --resolution    sets the resolution of the scanned image [50...600]dpi\n";
    std::cout << "-o, --outfile       save the file to a given directory\n";
}

void print_version()
{
    std::cout << "scan2pdf " << get_scan2pdf_version() << '\n';
    std::cout << "Copyright (C) 2022-2024 Michael Pollak\n";
}

void set_device_options(hyx::sane_device* device)
{
    for (const auto& opt : device->get_options()) {
        if (sane_options.contains(opt->name)) {
            if (const auto bop{dynamic_cast<hyx::sane_device::bool_option*>(opt)}) {
                device->set_option(bop, static_cast<SANE_Bool>(std::any_cast<bool>(sane_options.at(opt->name))));
            }
            else if (const auto fop{dynamic_cast<hyx::sane_device::fixed_option*>(opt)}) {
                device->set_option(fop, std::any_cast<SANE_Fixed>(sane_options.at(opt->name)));
            }
            else if (const auto iop{dynamic_cast<hyx::sane_device::int_option*>(opt)}) {
                device->set_option(iop, std::any_cast<SANE_Int>(sane_options.at(opt->name)));
            }
            else if (const auto sop{dynamic_cast<hyx::sane_device::string_option*>(opt)}) {
                //! FIXME: const_cast is a bad idea (but last resort for now).
                device->set_option(sop, const_cast<SANE_String>(std::any_cast<SANE_String_Const>(sane_options.at(opt->name))));
            }
        }
    }
}

void proccess(Magick::Image& image)
{
    // we need to despeckle before any trims so small artifacts won't mess with them
    image.despeckle();
    image.alpha(false);

    // crop the scan edges of the image
    image.crop(get_trim_edges_bounds(logger, image));
    image.repage();

    dump_image(image, "cropped");

    image.enhance();

    deskew(logger, image);
    image.repage();

    // remove the shadow on the top of the image
    image.crop(get_trim_shadow_bounds(logger, image));
    image.repage();

    constexpr auto gamma_fix{2.2};
    image.gamma(gamma_fix);
}

int main(int argc, char** argv)
{
    std::ios_base::sync_with_stdio(false);

    // NOTE: args will never be empty since argv[0] is the program name
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));

    auto outfile{std::filesystem::absolute(std::format("{:%F-%H-%M-%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())))};
    const hyx::temporary_path tmppath{std::filesystem::temp_directory_path() / "scan2pdf"};
    auto logpath{hyx::log_path() / "scan2pdf"};
    auto auto_mode{false};

    for (auto it{std::ranges::next(args.begin())}, it_end{args.end()}; it != it_end; ++it) {
        std::string_view arg{*it};

        const auto assert_next_arg{[&it, &it_end, &arg]() {
            if ((it + 1) == it_end) {
                std::cerr << "ERROR: Missing argument for \'" << arg << "\'!\n";
                std::exit(1);
            }
        }};

        if ((arg == "-h") || (arg == "--help")) {
            print_help();
            return 0;
        }
        else if ((arg == "-v") || (arg == "--version")) {
            print_version();
            return 0;
        }
        else if ((arg == "-r") || (arg == "--resolution")) {
            assert_next_arg();
            ++it;
            sane_options.at(SANE_NAME_SCAN_RESOLUTION) = std::stoi(*it);
        }
        else if ((arg == "-o") || (arg == "--outfile")) {
            assert_next_arg();
            ++it;
            if (outfile = std::filesystem::absolute(*it); !(outfile.has_filename() && std::filesystem::exists(outfile.parent_path()))) {
                std::cerr << "ERROR: Invalid output file " << outfile << "!\n";
                return 1;
            }
            // else, we are ok
        }
        else if (arg.starts_with("--auto=")) {
            const auto autofile{arg.substr(arg.find('=') + 1)};
            if (autofile.empty()) {
                std::cerr << "ERROR: Missing auto format\n";
                return 1;
            }

            outfile = std::filesystem::absolute(autofile);
            if (!(outfile.has_filename() && std::filesystem::exists(outfile.parent_path()))) {
                std::cerr << "ERROR: Invalid output file " << outfile << "!\n";
                return 1;
            }

            auto_mode = true;
        }
        else {
            std::cerr << "ERROR: Unkown option \'" << arg << "\'!\n";
            return 1;
        }
    }
    outfile.replace_extension(".pdf");

    const auto prog_start{std::chrono::high_resolution_clock::now()};
    try {
        logger.swap_to(logpath / "scan2pdf.log");
        logger("======Starting Program======\n");
    }
    catch (const std::exception& e) {
        std::cout << "WARNING: Failed to open file for logging: " << logpath / "scan2pdf.log" << '\n';
        // it is ok to continue without logging opened.
    }

    hyx::sane_init* sane{};
    std::unique_ptr<tesseract::TessBaseAPI> tess_api;

    try {
        logger("Initializing components\n");

        sane = &hyx::sane_init::get_instance();
        SANE_Int sane_version{sane->get_version()};
        if (sane_version) [[likely]] {
            logger(hyx::logger_literals::debug, "Initialized SANE {}.{}.{}\n", SANE_VERSION_MAJOR(sane_version), SANE_VERSION_MINOR(sane_version), SANE_VERSION_BUILD(sane_version));
        }
        else [[unlikely]] {
            logger(hyx::logger_literals::debug, "WARNING: unable to get SANE version\n");
        }

        tess_api = std::make_unique<tesseract::TessBaseAPI>();
        if (tess_api->Init(nullptr, "eng")) {
            throw std::runtime_error("Could not initialize tesseract");
        }
        tess_api->SetVariable("debug_file", (logpath / "tess.log").c_str());
        logger(hyx::logger_literals::debug, "Initialized Tesseract {}\n", tess_api->Version());

        Magick::InitializeMagick(*argv);
        if (!Magick::EnableOpenCL()) {
            logger(hyx::logger_literals::warning, "GPU acceleration failed to initialize -> falling back to CPU only\n");
        }
        logger(hyx::logger_literals::debug, "Initialized {}\n", MagickCore::GetMagickVersion(nullptr));

        try {
            // we are just initializing python early so if it fails we know before starting the scan
            std::ignore = hyx::py_init::get_instance().import("guess_organization");
        }
        catch (const std::exception& py_e) {
            // don't fail without python init; just continue without things that depend on it
            logger(hyx::logger_literals::warning, "Failed to initialize Python components: {}\n", py_e.what());
            outfile.replace_filename(std::regex_replace(outfile.filename().c_str(), std::regex("%o"), "[org]"));
        }

        logger("All components initialized\n");
    }
    catch (const std::exception& e) {
        std::cout << "Failed to Initialize: " << e.what() << '\n';
        logger(hyx::logger_literals::fatal, "Failed to Initialize: {}", e.what());
        return 1;
    }

    try {
        hyx::sane_device* device{sane->open_device()};

        set_device_options(device);

        if (!std::filesystem::exists(tmppath)) {
            std::filesystem::create_directory(tmppath);
        }

        // we start processing images
        logger("Scanning Document\n");
        std::atomic<bool> done_scanning{false};
        hyx::circular_buffer<Magick::Image> images_buffer;
        std::vector<Magick::Image> images;
        std::string document_text{};

        { // jthread start
            // we only share the images container and atomic boolean—which gets set as the last thing the thread does—so it should be thread safe
            std::jthread t1([&images_buffer, &device, &done_scanning]() {
                for (auto i{0}; device->start(); ++i) {
                    logger("Obtaining image {}\n", i);
                    images_buffer.emplace(get_next_image(device));
                }

                // ok, we are done and images is not empty (unless nothing was scanned)
                done_scanning = true;
                logger("Done obtaining images\n");
            });

            for (int img_num{0}; !done_scanning || !images_buffer.empty(); /* empty */) {
                // we need to wait for an image before proccesing one
                if (images_buffer.empty()) {
                    using namespace std::chrono_literals;
                    std::this_thread::sleep_for(100ms);
                }
                else {
                    // take and process an image
                    Magick::Image image{images_buffer.take()};

                    // static const auto log_prefix{"Image " + std::to_string(img_num)};
                    dump_image(image, "initial");

                    logger("Digesting image\n");

                    // set image settings
                    image.compressType(Magick::LZWCompression);
                    image.density(std::any_cast<SANE_Word>(sane_options.at(SANE_NAME_SCAN_RESOLUTION)));

                    proccess(image);

                    dump_image(image, "proccessed");

                    if (is_white(logger, image)) {
                        logger("Removing image\n");
                    }
                    else {
                        logger("Keeping image\n");

                        if (is_bw(logger, image)) {
                            has_text(hyx::unique_pix(magick2pix(image)).get(), tess_api.get()) ? transform_with_text_to_bw(logger, image) : transform_to_bw(logger, image);
                        }
                        else if (is_grayscale(logger, image)) {
                            transform_to_grayscale(logger, image);
                        }
                        // else, image is color

                        dump_image(image, "reduced");

                        hyx::unique_pix pimage{magick2pix(image)};

                        // attempt to orient using tesseract.
                        auto ori_deg{get_orientation(logger, tess_api.get(), pimage.get())};

                        logger(hyx::logger_literals::debug, "Rotating by {} degrees\n", ori_deg);
                        image.rotate(360 - ori_deg);

                        logger("Collecting text\n");
                        pimage.reset(pixRotateOrth(pimage.get(), ori_deg / 90));
                        document_text += get_text(tess_api.get(), pimage.get());

                        logger("Adding to list of images\n");
                        images.emplace_back(std::move(image));
                    }

                    ++img_num;
                }
            }
        } // jthread join

        if (images.size() == 0) {
            throw std::runtime_error("Too few images to output a pdf.");
        }

        std::string combined_pages_filepath{(tmppath / "combined_pages")};

        logger("Starting to process pdf\n");
        Magick::writeImages(images.begin(), images.end(), combined_pages_filepath + ".tiff");

        if (auto_mode && !document_text.empty()) {
            outfile.replace_filename(std::regex_replace(outfile.filename().c_str(), std::regex("%o"), parse_organization(document_text, "<org>")));
            outfile.replace_filename(std::regex_replace(outfile.filename().c_str(), std::regex("%d"), hyx::parser::parse_date(document_text, get_current_date())));
            outfile.replace_filename(std::regex_replace(outfile.filename().c_str(), std::regex("%s"), hyx::parser::parse_store(document_text, "<store>")));
            outfile.replace_filename(std::regex_replace(outfile.filename().c_str(), std::regex("%t"), hyx::parser::parse_transaction(document_text, "<transaction>")));
        }
        logger(hyx::logger_literals::debug, "File name is \'{}\'\n", outfile.filename().string());

        if (!tess_api->ProcessPages((combined_pages_filepath + ".tiff").c_str(), nullptr, static_cast<int>(10'000 * images_buffer.size()), std::make_unique<tesseract::TessPDFRenderer>(combined_pages_filepath.c_str(), tess_api->GetDatapath(), false).get())) {
            logger(hyx::logger_literals::warning, "OCR taking too long: skipping\n");
            Magick::writeImages(images.begin(), images.end(), (combined_pages_filepath + ".pdf"));
        }

        std::error_code ecode;
        if (!std::filesystem::copy_file(combined_pages_filepath + ".pdf", outfile, std::filesystem::copy_options::update_existing, ecode)) {
            throw std::runtime_error("Failed to move output file: \'" + ecode.message() + "\'");
        }
        logger(hyx::logger_literals::debug, "Moved file successfully\n");

        logger("Document ready!\n");
    }
    catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        logger(hyx::logger_literals::fatal, "{}\n", e.what());
        return 1;
    }
    // catch (const Magick::Error& me) {
    //     // return 2;
    // }

    const auto prog_end{std::chrono::high_resolution_clock::now()};
    logger(hyx::logger_literals::debug, "finished in {:%Q%q}\n", std::chrono::duration_cast<std::chrono::milliseconds>(prog_end - prog_start));
    return 0;
}
