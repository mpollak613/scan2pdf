/**
 * @file scan2pdf.cpp
 * @copyright
 * Copyright 2022-2023 Michael Pollak.
 * All rights reserved.
 */

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
#include <hyx/circular_buffer.h> // non-standard
#include <hyx/filesystem.h>      // non-standard
#include <hyx/leptonica.h>       // non-standard
#include <hyx/logger.h>          // non-standard
#include <hyx/parser.h>          // non-standard
#include <hyx/python.h>          // non-standard
#include <hyx/sane.h>            // non-standard
#include <iostream>
#include <leptonica/allheaders.h> // non-standard
#include <limits>
#include <memory>
#include <numbers>
#include <ranges>
#include <regex>
#include <sane/sane.h>     // non-standard
#include <sane/saneopts.h> // non-standard
#include <string>
#include <tesseract/baseapi.h>  // non-standard
#include <tesseract/renderer.h> // non-standard
#include <thread>
#include <unordered_map>
#include <vector>

//! TODO: allow for other image formats if libtiff is not available
#ifdef HAVE_LIBTIFF
#include <tiffio.h>   // non-standard
#include <tiffio.hxx> // non-standard
#endif                // !HAVE_LIBTIFF

namespace global {
    constexpr std::string_view version{"2.3"};
    constexpr auto scanner_gamma_fix{2.2};
} // namespace global

hyx::logger logger(std::clog, "[cl::utc;%FT%TZ][[[::lvl;^9]]]: [sl::file_name;]@[sl::line;]: ");

//! FIXME: QuantumRange Seems broken? MaxMap works for now.

constexpr double to_quantum_percent(std::convertible_to<double> auto percent);
constexpr long double quantum_as_rgb(long double quantum_val);

consteval double operator""_quantum_percent(long double percent)
{
    return to_quantum_percent(percent);
}
consteval double operator"" _quantum_percent(unsigned long long percent)
{
    return to_quantum_percent(percent);
}

std::string get_current_date();
std::string parse_organization(const std::string& text, const std::string& default_return);

Magick::Image get_next_image(hyx::sane_device* device);

void print_help();
void print_version();

void set_device_options(hyx::sane_device* device);

void proccess(Magick::Image& image);
std::string get_trim_shadow_bounds(Magick::Image image);
void deskew(Magick::Image& image);
std::string get_trim_edges_bounds(Magick::Image image);

void transform_to_bw(Magick::Image& image);
void transform_with_text_to_bw(Magick::Image& image);
void transform_to_grayscale(Magick::Image& image);
int get_orientation(tesseract::TessBaseAPI* tess_api, PIX* pimage);
bool is_grayscale(Magick::Image& image);
bool is_bw(Magick::Image& image);
bool is_spot_colored(Magick::Image& image);
bool is_white(Magick::Image& image);
bool has_text(tesseract::TessBaseAPI* tess_api, PIX* pimage);
PIX* magick2pix(Magick::Image& image);
std::string get_text(tesseract::TessBaseAPI* tess_api, PIX* pimage);

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
    {"ald", true}};

constexpr void dump_image([[maybe_unused]] Magick::Image& img, [[maybe_unused]] const std::string& name)
{
#ifdef DEBUG
    static int idx{};

    img.write(hyx::log_path() / "scan2pdf" / ("debugged_image_" + std::to_string(idx) + name + ".png"));
    ++idx;
#endif
}

constexpr long double quantum_as_rgb(long double quantum_val)
{
    constexpr auto rgb_max{255.0L};
    return (quantum_val / MaxMap) * rgb_max;
}

constexpr double to_quantum_percent(std::convertible_to<double> auto percent)
{
    constexpr auto percent_max{100.0};
    return (static_cast<double>(percent) / percent_max) * MaxMap;
}

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
    std::cout << "-r, --resolution     sets the resolution of the scanned image [50...600]dpi\n";
    std::cout << "-o, --output-path    save the file to a given directory\n";
}

void print_version()
{
    std::cout << "scan2pdf " << global::version << '\n';
    std::cout << "Copyright (C) 2022-2023 Michael Pollak\n";
    std::cout << "License Apache-2.0: Apache License, Version 2.0 <https://www.apache.org/licenses/LICENSE-2.0>\n";
    std::cout << '\n';
    std::cout << "Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.\n";
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
    // basic image changes
    image.despeckle();
    image.enhance();
    image.alpha(false);

    deskew(image);
    image.repage();

    // crop the scan edges of the image
    image.crop(get_trim_edges_bounds(image));
    image.repage();

    // remove the shadow on the top of the image
    image.crop(get_trim_shadow_bounds(image));
    image.repage();

    image.gamma(global::scanner_gamma_fix);
}

void deskew(Magick::Image& image)
{
    logger("Deskewing image\n");

    // we flip the image to get the shadow on the top before deskewing
    image.flip();

    // Find the color of the scan background.
    const auto background_color{image.pixelColor(5, 5)};
    logger(hyx::logger_literals::debug, "Set background color to ({},{},{})\n", quantum_as_rgb(background_color.quantumRed()), quantum_as_rgb(background_color.quantumGreen()), quantum_as_rgb(background_color.quantumBlue()));

    image.backgroundColor(background_color);
    image.deskew(80_quantum_percent);

    const auto deskew_angle{image.formatExpression("%[deskew:angle]")};
    logger(hyx::logger_literals::debug, "Deskewed by {} degrees\n", deskew_angle);

    // reset image state besides deskewing
    image.flip();
    image.backgroundColor();

    dump_image(image, "deskewed");
}

std::string get_trim_edges_bounds(Magick::Image image)
{
    // magick in.png -fuzz 10% -set MBB '%[minimum-bounding-box]' -fill black -colorize 100 -fill white -draw "polygon %[MBB]" -define trim:percent-background=0 -trim -format "%wx%h%X%Y" info:

    logger("Trimming edges\n");

    // the colorFuzz value will influence the MBR
    image.colorFuzz(10_quantum_percent);
    // MBR = Minimum Bounding Rectangle
    auto mbr{image.formatExpression("%[minimum-bounding-box]")};

    // create our polygon coordinates
    std::vector<Magick::Coordinate> mbr_coords;
    for (const auto& coord : mbr | std::views::split(' ')) {
        for (const auto& [x, y] : coord | std::views::split(',') | std::views::pairwise) {
            mbr_coords.emplace_back(std::stod(std::string(x.begin(), x.end())), std::stod(std::string(y.begin(), y.end())));
        }
    }

    // create a black background and white box over the MBR so we can easily trim to it.
    constexpr auto colorize_alpha{100u};
    image.colorize(colorize_alpha, "black");
    image.fillColor("white");
    image.draw(Magick::DrawablePolygon(mbr_coords));
    image.artifact("trim:percent-background", "0");

    dump_image(image, "trim_edges_before_trim");

    image.trim();

    dump_image(image, "trim_edges");

    return image.formatExpression("%wx%h%X%Y");
}

std::string get_trim_shadow_bounds(Magick::Image image)
{
    logger("Trimming shadow\n");

    image.gamma(global::scanner_gamma_fix);
    constexpr auto blur_radius{0.0};
    constexpr auto blur_sigma{5.0};
    image.adaptiveBlur(blur_radius, blur_sigma);
    image.negate();
    image.autoThreshold(Magick::OTSUThresholdMethod);
    image.negate();
    image.artifact("trim:percent-background", "0");
    image.artifact("trim:background-color", "black");

    dump_image(image, "trim_shadow_before_trim");

    auto image_canvas{image.formatExpression("%wx%h%X%Y")};
    logger(hyx::logger_literals::debug, "Starting dimentions: {}\n", image_canvas);
    try {
        for ([[maybe_unused]] auto _ : std::views::iota(0, 10)) {
            const auto before_trim_image_canvas = image.formatExpression("%wx%h%X%Y");
            image.trim();
            const auto after_trim_image_canvas = image.formatExpression("%wx%h%X%Y");

            // check to see if the trim removed anything
            logger(hyx::logger_literals::debug, "After trim dimentions: {}\n", after_trim_image_canvas);
            if (before_trim_image_canvas == after_trim_image_canvas) {
                // maybe the shadow is not at the edge of the image?
                // we will try to dig one pixel at a time to find the shadow
                logger(hyx::logger_literals::debug, "Removing pixel line to find shadow\n");
                image.crop("+0-1");
                image.repage();
            }
            else {
                image_canvas = after_trim_image_canvas;
                break;
            }
        }
    }
    catch (const std::exception& e) {
        logger(hyx::logger_literals::warning, "Failed to trim shadow: {}\n", e.what());
    }

    dump_image(image, "trim_shadow_after_trim");

    return image_canvas;
}

void transform_to_bw(Magick::Image& image)
{
    logger(hyx::logger_literals::debug, "Converting to black and white\n");

    constexpr auto brightness{0.0};
    constexpr auto constrast{30.0};
    image.brightnessContrast(brightness, constrast);
    image.autoThreshold(Magick::KapurThresholdMethod);
}

void transform_with_text_to_bw(Magick::Image& image)
{
    logger(hyx::logger_literals::debug, "Converting to black and white\n");

    // magick in.png -auto-level -unsharp 0x2+1.5+0.05 -resize 200% -auto-threshold otsu out.pdf

    image.autoLevel();
    constexpr auto unsharp_radius{0.0};
    constexpr auto unsharp_sigma{2.0};
    constexpr auto unsharp_amount{1.5};
    constexpr auto unsharp_threshold{0.05};
    image.unsharpmask(unsharp_radius, unsharp_sigma, unsharp_amount, unsharp_threshold);
    image.autoThreshold(Magick::OTSUThresholdMethod);
}

void transform_to_grayscale(Magick::Image& image)
{
    logger(hyx::logger_literals::debug, "Converting to greyscale\n");

    constexpr auto brightness{0.0};
    constexpr auto contrast{30.0};
    image.brightnessContrast(brightness, contrast);
    image.opaque(Magick::Color("white"), Magick::Color("white"));
    image.colorSpace(Magick::LinearGRAYColorspace);
}

int get_orientation(tesseract::TessBaseAPI* tess_api, PIX* pimage)
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

bool is_grayscale(Magick::Image& image)
{
    // constinit static const auto log_prefix{"Grayscale Check"};

    Magick::Image gray_image_test{image};
    gray_image_test.colorSpace(Magick::HSLColorspace);
    auto gray_factor{std::stod(gray_image_test.formatExpression("%[fx:mean.g]"))};
    auto gray_peak{std::stod(gray_image_test.formatExpression("%[fx:maxima.g]"))};

    logger(hyx::logger_literals::debug, "Gray mean: {}\n", gray_factor);
    logger(hyx::logger_literals::debug, "Gray peak: {}\n", gray_peak);

    constexpr auto gray_factor_threshold{0.05};
    constexpr auto gray_peak_threshold{0.1};
    if (gray_factor < gray_factor_threshold && gray_peak < gray_peak_threshold) {
        return true;
    }

    return false;
}

bool is_bw(Magick::Image& image)
{
    // constinit static const auto log_prefix{"BW Check"};

    // magick in.png -solarize 50% -colorspace gray -identify -verbose info:

    Magick::Image bw_image_test{image};
    bw_image_test.solarize(50_quantum_percent);
    bw_image_test.colorSpace(Magick::GRAYColorspace);
    auto image_stats{bw_image_test.statistics().channel(Magick::PixelChannel::GrayPixelChannel)};

    auto mean_gray{quantum_as_rgb(image_stats.mean())};
    auto std_diff_mean_gray{std::abs(quantum_as_rgb(image_stats.standardDeviation() - image_stats.mean()))};

    logger(hyx::logger_literals::debug, "Gray mean: {}\n", mean_gray);
    logger(hyx::logger_literals::debug, "Gray std-dev-mean-diff: {}\n", std_diff_mean_gray);

    constexpr auto rgb_max{255};
    constexpr auto mean_gray_rgb_threshold{rgb_max / 6};
    constexpr auto std_diff_mean_gray_threshold{15.5};
    if (mean_gray < mean_gray_rgb_threshold && std_diff_mean_gray < std_diff_mean_gray_threshold) {
        return true;
    }

    return false;
}

bool is_spot_colored(Magick::Image& image)
{
    // constinit static const auto log_prefix{"Spot Color Check"};

    Magick::Image spot_image_test{image};
    spot_image_test.colorSpace(Magick::HCLColorspace);
    spot_image_test.resize("1:50");
    auto gray_factor{std::stod(spot_image_test.formatExpression("%[fx:mean.g]"))};
    auto gray_peak{std::stod(spot_image_test.formatExpression("%[fx:maxima.g]"))};

    logger(hyx::logger_literals::debug, "Color mean: {}\n", gray_factor);
    logger(hyx::logger_literals::debug, "Color peak: {}\n", gray_peak);

    constexpr auto gray_factor_threshold{0.05};
    constexpr auto gray_peak_min{0.3};
    if (gray_factor < gray_factor_threshold && gray_peak > gray_peak_min) {
        return true;
    }

    return false;
}

bool is_white(Magick::Image& image)
{
    // constinit static const auto log_prefix{"All White Check"};

    Magick::Image percent_white_image{image};
    percent_white_image.whiteThreshold("75%");
    auto percent_white = std::stod(percent_white_image.formatExpression("%[fx:mean]"));

    logger(hyx::logger_literals::debug, "Percent white: {}\n", percent_white);

    constexpr auto percent_white_threshold{0.99};
    if (percent_white > percent_white_threshold) {
        return true;
    }

    return false;
}

bool has_text(tesseract::TessBaseAPI* tess_api, PIX* pimage)
{
    tess_api->SetImage(pimage);
    std::string image_text{tess_api->GetUTF8Text()};
    return !image_text.empty();
}

PIX* magick2pix(Magick::Image& image)
{
    Magick::Blob bimage;
    image.write(&bimage, "tiff");
    return pixReadMemTiff(static_cast<const unsigned char*>(bimage.data()), bimage.length(), 0);
}

std::string get_text(tesseract::TessBaseAPI* tess_api, PIX* pimage)
{
    tess_api->SetImage(pimage);
    auto ocr_text{std::unique_ptr<char[]>(tess_api->GetUTF8Text())};
    return (ocr_text) ? ocr_text.get() : "";
}

int main(int argc, char** argv)
{
    std::string filename;
    std::filesystem::path outpath{"./"};
    const hyx::temporary_path tmppath{std::filesystem::temp_directory_path() / "scan2pdf"};
    std::filesystem::path logpath{hyx::log_path() / "scan2pdf"};

    auto auto_mode{false};

    // empty
    if (argc == 1) {
        print_help();
        return 0;
    }
    for (auto idx{1}; idx < argc; ++idx) {
        auto arg{std::string_view(argv[idx])};

        if ((arg == "-h") || (arg == "--help")) {
            print_help();
            return 0;
        }
        else if ((arg == "-v") || (arg == "--version")) {
            print_version();
            return 0;
        }
        else if (((arg == "-r") || (arg == "--resolution")) && ((idx + 1) < argc)) {
            sane_options.at(SANE_NAME_SCAN_RESOLUTION) = std::stoi(argv[++idx]);
        }
        else if (((arg == "-o") || (arg == "--outpath")) && ((idx + 1) < argc)) {
            arg = std::string_view(argv[++idx]);
            if (std::filesystem::exists(arg)) {
                outpath = std::filesystem::absolute(arg);
            }
            else {
                std::cout << "Path " << arg << " does not exist!\n";
                return 1;
            }
        }
        else if (arg.starts_with("--auto=")) {
            auto_mode = true;
            filename = arg.substr(arg.find('=') + 1);
        }
        // bad option
        else if (arg.starts_with('-')) {
            std::cout << "Unkown option \'" << arg << "\'!\n";
            return 1;
        }
        // stop at filename
        else {
            filename = arg;
            break;
        }
    }

    if (filename.empty()) {
        std::cout << "No filename detected!\n";
        return 1;
    }

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

        SANE_Int sane_version{};
        sane = &hyx::sane_init::get_instance(&sane_version);
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
            filename = std::regex_replace(filename, std::regex("%o"), "[org]");
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

                    // ask user if almost white images should be deleted.
                    //! FIXME: using a lambda for now
                    auto ask_user_should_keep_image = [&image, &img_num]() -> bool {
                        if (is_white(image)) {
                            logger(hyx::logger_literals::warning, "May be blank\n");

                            std::string user_answer;
                            std::cout << "Possible blank page found (pg. " << img_num << "). Keep the page? [Y/n] ";
                            std::getline(std::cin, user_answer);

                            if (!user_answer.empty() && (user_answer.front() == 'N' || user_answer.front() == 'n')) {
                                logger("Removing image\n");
                                return false;
                            }
                            else {
                                logger("Keeping image\n");
                                // fall through
                            }
                        }

                        return true;
                    };

                    if (ask_user_should_keep_image()) {
                        if (is_bw(image)) {
                            has_text(tess_api.get(), hyx::unique_pix(magick2pix(image)).get()) ? transform_with_text_to_bw(image) : transform_to_bw(image);
                        }
                        else if (is_grayscale(image)) {
                            transform_to_grayscale(image);
                        }

                        dump_image(image, "reduced");

                        hyx::unique_pix pimage{magick2pix(image)};

                        // attempt to orient using tesseract.
                        auto ori_deg{get_orientation(tess_api.get(), pimage.get())};

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
            filename = std::regex_replace(filename, std::regex("%o"), parse_organization(document_text, "<org>"));
            filename = std::regex_replace(filename, std::regex("%d"), hyx::parser::parse_date(document_text, get_current_date()));
            filename = std::regex_replace(filename, std::regex("%s"), hyx::parser::parse_store(document_text, "<store>"));
            filename = std::regex_replace(filename, std::regex("%t"), hyx::parser::parse_transaction(document_text, "<transaction>"));
        }
        logger(hyx::logger_literals::debug, "File name is \'{}\'\n", filename);

        if (!tess_api->ProcessPages((combined_pages_filepath + ".tiff").c_str(), nullptr, static_cast<int>(10'000 * images_buffer.size()), std::make_unique<tesseract::TessPDFRenderer>(combined_pages_filepath.c_str(), tess_api->GetDatapath(), false).get())) {
            logger(hyx::logger_literals::warning, "OCR taking too long: skipping\n");
            Magick::writeImages(images.begin(), images.end(), (combined_pages_filepath + ".pdf"));
        }

        std::error_code ecode;
        if (!std::filesystem::copy_file(combined_pages_filepath + ".pdf", outpath / (filename + ".pdf"), std::filesystem::copy_options::update_existing, ecode)) {
            throw std::runtime_error("Failed to move output file: \'" + ecode.message() + "\'");
        }
        logger(hyx::logger_literals::debug, "Moved file successfully\n");

        logger("Document ready!\n");
    }
    catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        logger(hyx::logger_literals::error, "{}\n", e.what());
        return 1;
    }
    // catch (const Magick::Error& me) {
    //     // return 2;
    // }

    const auto prog_end{std::chrono::high_resolution_clock::now()};
    logger(hyx::logger_literals::debug, "finished in {:%Q%q}\n", std::chrono::duration_cast<std::chrono::milliseconds>(prog_end - prog_start));
    return 0;
}
