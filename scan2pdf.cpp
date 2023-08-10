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
#include <ctime>
#include <filesystem>
#include <hytrax/hyx_filesystem.h> // non-standard
#include <hytrax/hyx_leptonica.h>  // non-standard
#include <hytrax/hyx_logger.h>     // non-standard
#include <hytrax/hyx_parser.h>     // non-standard
#include <hytrax/hyx_python.h>     // non-standard
#include <hytrax/hyx_sane.h>       // non-standard
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
#include <unordered_map>
#include <vector>

// TODO: allow for other image formats if libtiff is not available
#ifdef HAVE_LIBTIFF
#include <tiffio.h>   // non-standard
#include <tiffio.hxx> // non-standard
#endif                // !HAVE_LIBTIFF

constexpr static std::string_view version{"2.1"};

/**
 * @brief Global options.
 */
// TODO: global options should get stored in a safer way
static std::unordered_map<std::string, std::any> sane_options{
    {SANE_NAME_SCAN_SOURCE, "ADF Duplex"},
    {SANE_NAME_SCAN_MODE, SANE_VALUE_SCAN_MODE_COLOR},
    {SANE_NAME_SCAN_RESOLUTION, 300},
    {SANE_NAME_PAGE_HEIGHT, std::numeric_limits<SANE_Word>::max()},
    {SANE_NAME_PAGE_WIDTH, std::numeric_limits<SANE_Word>::max()},
    {"ald", true}};

static void dump_image([[maybe_unused]] Magick::Image& img, [[maybe_unused]] const std::string& name)
{
#ifdef DEBUG
    static int idx{};

    img.write(hyx::log_path() / "scan2pdf" / ("debugged_image_" + std::to_string(idx) + name + ".png"));
    ++idx;
#endif
}

constexpr double to_quantum_percent(std::convertible_to<double> auto percent);
constexpr double operator""_quantum_percent(long double percent);
constexpr double operator"" _quantum_percent(unsigned long long percent);

constexpr long double rgb_value(long double quantum_val);

std::string get_current_date();
std::string parse_organization(const std::string& text, const std::string& default_return);

Magick::Image get_next_image(hyx::sane_device* device);

void print_help();
void print_version();

void set_device_options(hyx::sane_device* device);

auto remove_lines(Magick::Image& image);
double deskew(Magick::Image& image);
std::string trim_edges(Magick::Image& image);
std::string trim_shadow(Magick::Image& image);
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

constexpr long double rgb_value(long double quantum_val)
{
    constexpr auto rgb_max{255};
    return (quantum_val / QuantumRange) * rgb_max;
}

constexpr double to_quantum_percent(std::convertible_to<double> auto percent)
{
    constexpr auto decimal_percent_max{1.0};
    constexpr auto whole_percent_max{100.0};
    const auto cast_percent{static_cast<double>(percent)};
    return ((cast_percent > decimal_percent_max) ? (cast_percent / whole_percent_max) : (cast_percent / decimal_percent_max)) * QuantumRange;
}

constexpr double operator""_quantum_percent(long double percent)
{
    return to_quantum_percent(percent);
}

constexpr double operator""_quantum_percent(unsigned long long percent)
{
    return to_quantum_percent(percent);
}

std::string get_current_date()
{
    const auto now = std::chrono::system_clock::now();

    return std::format("{:%Y-%m-%d}", now);
}

std::string parse_organization(const std::string& text, const std::string& default_return)
{
    if (std::string org{hyx::py_init::get_instance().import("guess_organization")->call("guess_organization", text)}; !org.empty()) {
        return org;
    }
    else {
        return default_return;
    }
}

Magick::Image get_next_image(hyx::sane_device* device)
{
    auto sane_params{device->get_parameters()};

    std::ostringstream tiff_ostream;
    auto* tifffile = TIFFStreamOpen("tiff_frame", &tiff_ostream);
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

    auto buf_size = sane_params.bytes_per_line;
    auto* data = static_cast<SANE_Byte*>(_TIFFmalloc(buf_size));
    for (auto y{0u}; device->read(data, buf_size); ++y) {
        if (TIFFWriteScanline(tifffile, data, y, 0) != 1) {
            hyx::logger.warning("bad write!\n");
            break;
        }
    }

    _TIFFfree(data);
    TIFFClose(tifffile);

    // since we read the exact number of bytes per line we don't get EOF until the next call.
    // if the next call doesn't read zero bytes or return EOF, we have image bytes that were not read.
    if (device->read(data, buf_size)) {
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
    std::cout << "scan2pdf " << version << '\n';
    std::cout << "Copyright (C) 2022-2023 Michael Pollak\n";
    std::cout << "License Apache-2.0: Apache License, Version 2.0 <https://www.apache.org/licenses/LICENSE-2.0>\n";
    std::cout << '\n';
    std::cout << "Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.\n";
}

void set_device_options(hyx::sane_device* device)
{
    for (const auto& opt : device->get_options()) {
        if (sane_options.contains(opt->name)) {
            if (auto bop{dynamic_cast<hyx::sane_device::bool_option*>(opt)}) {
                device->set_option(bop, static_cast<SANE_Bool>(std::any_cast<bool>(sane_options.at(opt->name))));
            }
            else if (auto fop{dynamic_cast<hyx::sane_device::fixed_option*>(opt)}) {
                device->set_option(fop, std::any_cast<SANE_Fixed>(sane_options.at(opt->name)));
            }
            else if (auto iop{dynamic_cast<hyx::sane_device::int_option*>(opt)}) {
                device->set_option(iop, std::any_cast<SANE_Int>(sane_options.at(opt->name)));
            }
            else if (auto sop{dynamic_cast<hyx::sane_device::string_option*>(opt)}) {
                // FIXME: const_cast is a bad idea (but last resort for now).
                device->set_option(sop, const_cast<SANE_String>(std::any_cast<SANE_String_Const>(sane_options.at(opt->name))));
            }
        }
    }
}

auto remove_lines(Magick::Image& image)
{
    struct remove_lines_r {
        Magick::Image lines_removed_image;
        Magick::Image lines_image;
    };

    hyx::logger.info("Removing Lines\n");

    // magick \( in.png -alpha off -brightness-contrast 0x10 -colorspace lineargray \) \( -clone 0 -morphology close rectangle:1x80 -negate \) \( -clone 0 -morphology close rectangle:80x1 -negate \) \( -clone 1 -clone 2 -evaluate-sequence add +write lines.png \) -delete 1,2 -compose plus -composite out.png

    Magick::Image image_no_alpha{image};
    image_no_alpha.alpha(false);
    image_no_alpha.brightnessContrast(0, 10);
    image_no_alpha.colorSpace(Magick::LinearGRAYColorspace);

    dump_image(image_no_alpha, "before_line_remove");

    Magick::Image image_vertical_lines{image_no_alpha};
    image_vertical_lines.morphology(Magick::CloseMorphology, Magick::RectangleKernel, "1x80");
    image_vertical_lines.negate();

    dump_image(image_vertical_lines, "vertical_lines");

    Magick::Image image_horizontal_lines{image_no_alpha};
    image_horizontal_lines.morphology(Magick::CloseMorphology, Magick::RectangleKernel, "80x1");
    image_horizontal_lines.negate();

    dump_image(image_horizontal_lines, "horizontal_lines");

    // we combine both vertical and horizontal lines into one image
    Magick::Image lines_image;
    std::vector line_images = {image_vertical_lines, image_horizontal_lines};
    Magick::evaluateImages(&lines_image, line_images.begin(), line_images.end(), Magick::AddEvaluateOperator);

    dump_image(lines_image, "combined_lines");

    // now we place the origional image over the inverted lines to remove them
    Magick::Image composite_image{lines_image};
    composite_image.composite(image, "0x0", Magick::PlusCompositeOp);

    dump_image(composite_image, "lines_removed");

    return remove_lines_r{composite_image, lines_image};
}

double deskew(Magick::Image& image)
{
    hyx::logger.info("Deskewing image\n");
    // Find the color of the scan background.
    Magick::Color border_color{image.pixelColor(0, 0)};
    hyx::logger.debug("Set border color to ({},{},{})\n", rgb_value(border_color.quantumRed()), rgb_value(border_color.quantumGreen()), rgb_value(border_color.quantumBlue()));

    image.backgroundColor(border_color);
    image.deskew(80_quantum_percent);
    double deskew_angle = std::stod(image.formatExpression("%[deskew:angle]"), nullptr);
    hyx::logger.debug("Deskewing by {} degrees\n", deskew_angle);

    dump_image(image, "deskewed");

    return deskew_angle;
}

std::string trim_edges(Magick::Image& image)
{
    hyx::logger.info("Trimming edges\n");

    image.artifact("trim:percent-background", "75");
    image.trim();

    dump_image(image, "trim_edges");

    return image.formatExpression("%wx%h%X+%Y");
}

std::string trim_shadow(Magick::Image& image)
{
    hyx::logger.info("Trimming shadow\n");

    image.alpha(false);
    image.autoThreshold(Magick::OTSUThresholdMethod);
    image.splice("0x1", "yellow");
    image.splice("0x1", "black");

    dump_image(image, "trim_shadow_before_trim");

    image.backgroundColor("black");
    image.artifact("trim:percent-background", "0");
    image.trim();
    image.chop("0x1");

    dump_image(image, "trim_shadow");

    return image.formatExpression("%wx%h%X+%Y");
}

void transform_to_bw(Magick::Image& image)
{
    hyx::logger.debug("Converting to black and white\n");

    image.brightnessContrast(0, 30);
    image.autoThreshold(Magick::KapurThresholdMethod);
    image.resize("200%");
}

void transform_with_text_to_bw(Magick::Image& image)
{
    hyx::logger.debug("Converting to black and white\n");

    // magick in.png -auto-level -unsharp 0x2+1.5+0.05 -resize 200% -auto-threshold otsu out.pdf

    image.autoLevel();
    image.unsharpmask(0, 2, 1.5, 0.05);
    image.resize("200%");
    image.autoThreshold(Magick::OTSUThresholdMethod);
    // image.threshold(75_quantum_percent);
}

void transform_to_grayscale(Magick::Image& image)
{
    hyx::logger.debug("Converting to greyscale\n");

    image.brightnessContrast(0, 30);
    image.opaque(Magick::Color("white"), Magick::Color("white"));
    image.colorSpace(Magick::LinearGRAYColorspace);
    image.resize("200%");
}

int get_orientation(tesseract::TessBaseAPI* tess_api, PIX* pimage)
{
    hyx::logger.info("Getting orientation\n");
    tess_api->SetImage(pimage);

    int ori_deg;
    // float ori_conf;
    // const char* script_name;
    // float script_conf;
    tess_api->DetectOrientationScript(&ori_deg, nullptr, nullptr, nullptr);

    hyx::logger.debug("Orientation off by {} degrees\n", ori_deg);

    return ori_deg;
}

bool is_grayscale(Magick::Image& image)
{
    hyx::logger.push_prefix("Grayscale Check");

    Magick::Image gray_image_test{image};
    gray_image_test.colorSpace(Magick::HSLColorspace);
    auto gray_factor{std::stod(gray_image_test.formatExpression("%[fx:mean.g]"))};
    auto gray_peak{std::stod(gray_image_test.formatExpression("%[fx:maxima.g]"))};

    hyx::logger.debug("Gray mean: {}\n", gray_factor);
    hyx::logger.debug("Gray peak: {}\n", gray_peak);

    hyx::logger.pop_prefix();

    if (gray_factor < 0.05 && gray_peak < 0.1) {
        return true;
    }

    return false;
}

bool is_bw(Magick::Image& image)
{
    hyx::logger.push_prefix("BW Check");

    // magick in.png -solarize 50% -colorspace gray -identify -verbose info:

    Magick::Image bw_image_test{image};
    bw_image_test.solarize(50_quantum_percent);
    bw_image_test.colorSpace(Magick::GRAYColorspace);
    auto image_stats{bw_image_test.statistics().channel(Magick::PixelChannel::GrayPixelChannel)};

    auto mean_gray{rgb_value(image_stats.mean())};
    auto std_diff_mean_gray{std::abs(rgb_value(image_stats.standardDeviation() - image_stats.mean()))};

    hyx::logger.debug("Gray mean: {}\n", mean_gray);
    hyx::logger.debug("Gray std-dev-mean-diff: {}\n", std_diff_mean_gray);

    hyx::logger.pop_prefix();

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
    hyx::logger.push_prefix("Spot Color Check");

    Magick::Image spot_image_test{image};
    spot_image_test.colorSpace(Magick::HCLColorspace);
    spot_image_test.resize("1:50");
    auto gray_factor{std::stod(spot_image_test.formatExpression("%[fx:mean.g]"))};
    auto gray_peak{std::stod(spot_image_test.formatExpression("%[fx:maxima.g]"))};

    hyx::logger.debug("Color mean: {}\n", gray_factor);
    hyx::logger.debug("Color peak: {}\n", gray_peak);

    hyx::logger.pop_prefix();

    constexpr auto gray_factor_threshold{0.05};
    constexpr auto gray_peak_min{0.3};
    if (gray_factor < gray_factor_threshold && gray_peak > gray_peak_min) {
        return true;
    }

    return false;
}

bool is_white(Magick::Image& image)
{
    hyx::logger.push_prefix("All White Check");

    Magick::Image percent_white_image{image};
    percent_white_image.whiteThreshold("75%");
    auto percent_white = std::stod(percent_white_image.formatExpression("%[fx:mean]"));

    hyx::logger.debug("Percent white: {}\n", percent_white);

    hyx::logger.pop_prefix();

    if (percent_white > 0.99) {
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

    try {
        hyx::logger.swap_to(logpath / "scan2pdf.log");
        hyx::logger.info("======Starting Program======\n");
    }
    catch (const std::exception& e) {
        std::cout << "WARNING: Failed to open file for logging: " << logpath / "scan2pdf.log" << '\n';
        // it is ok to continue without logging opened.
    }

    hyx::sane_init* sane;
    std::unique_ptr<tesseract::TessBaseAPI> tess_api;

    try {
        hyx::logger.info("Initializing components\n");

        sane = &hyx::sane_init::get_instance();

        tess_api = std::make_unique<tesseract::TessBaseAPI>();
        if (tess_api->Init(nullptr, "eng")) {
            throw std::runtime_error("Could not initialize tesseract");
        }
        tess_api->SetVariable("debug_file", (logpath / "tess.log").c_str());

        Magick::InitializeMagick(*argv);
        hyx::logger.info("All components initialized\n");

        Magick::ResourceLimits::thread(1LL);
        Magick::ResourceLimits::throttle(20LL);

        try {
            // we are just initializing python early so if it fails we know before starting the scan
            std::ignore = hyx::py_init::get_instance().import("guess_organization")->call("guess_organization", std::string("test string with Walmart"));
        }
        catch (const std::exception& py_e) {
            // don't fail without python init; just continue without things that depend on it
            hyx::logger.warning("Failed to initialize Python components: {}", py_e.what());
            filename = std::regex_replace(filename, std::regex("%o"), "[org]");
        }
    }
    catch (const std::exception& e) {
        std::cout << "Failed to Initialize: " << e.what() << '\n';
        hyx::logger.fatal("Failed to Initialize: {}", e.what());
        return 1;
    }

    try {
        hyx::sane_device* device{sane->open_device()};

        set_device_options(device);

        if (!std::filesystem::exists(tmppath)) {
            std::filesystem::create_directory(tmppath);
        }

        // we start processing images
        hyx::logger.info("Scanning Document\n");
        // TODO: Don't use two vectors
        std::vector<Magick::Image> images{};
        std::string document_text{};
        for (auto i = 0; device->start(); ++i) {
            hyx::logger.info("Obtaining image {}\n", i);
            images.push_back(get_next_image(device));
        }

        for (const auto& [i, image] : std::views::enumerate(images)) {
            hyx::logger.push_prefix("Image " + std::to_string(i));

            dump_image(image, "initial");

            hyx::logger.info("Digesting image\n");

            // set image settings
            image.compressType(Magick::LZWCompression);
            image.density(std::any_cast<SANE_Word>(sane_options.at(SANE_NAME_SCAN_RESOLUTION)));

            // basic image changes
            image.gamma(2.2);
            image.despeckle();
            image.enhance();

            auto&& [line_removed_image, lines_image] = remove_lines(image);

            // attempt to deskew image.
            auto deskew_deg{deskew(line_removed_image)};
            image.backgroundColor(image.pixelColor(0, 0));
            image.rotate(deskew_deg);
            lines_image.backgroundColor(lines_image.pixelColor(0, 0));
            lines_image.rotate(deskew_deg);

            // FIXME: Crops too far on sideways scans
            // attempt to remove image edges and shadow from top of the image
            lines_image.autoThreshold(Magick::OTSUThresholdMethod);
            lines_image.artifact("trim:percent-background", "20");
            dump_image(lines_image, "lines_before_trim");
            lines_image.trim();
            image.crop(lines_image.formatExpression("%wx%h%X+%Y"));
            image.repage();

            dump_image(image, "enhanced");

            // ask user if almost white images should be deleted.
            auto keep_image{true};
            if (is_white(image)) {
                hyx::logger.warning("May be blank\n");

                std::string user_answer;
                std::cout << "  Possible blank page found (pg. " << i << "). Keep the page? [Y/n] ";
                std::getline(std::cin, user_answer);

                if (!user_answer.empty() && (user_answer.front() == 'N' || user_answer.front() == 'n')) {
                    hyx::logger.info("Removing image\n");
                    keep_image = false;
                    images.erase(images.begin() + i);
                }
                else {
                    hyx::logger.info("Keeping image\n");
                }
            }

            if (keep_image) {
                if (is_bw(image)) {
                    has_text(tess_api.get(), hyx::unique_pix(magick2pix(image)).get()) ? transform_with_text_to_bw(image) : transform_to_bw(image);
                }
                else if (is_grayscale(image)) {
                    transform_to_grayscale(image);
                }
                else {
                    image.resize("200%");
                }

                dump_image(image, "reduced");

                hyx::unique_pix pimage{magick2pix(image)};

                // attempt to orient using tesseract.
                auto ori_deg{get_orientation(tess_api.get(), pimage.get())};

                hyx::logger.debug("Rotating by {} degrees\n", ori_deg);
                image.rotate(360 - ori_deg);

                hyx::logger.info("Collecting text\n");
                pimage.reset(pixRotateOrth(pimage.get(), ori_deg / 90));
                document_text += get_text(tess_api.get(), pimage.get());

                hyx::logger.info("Adding to list of images\n");
            }

            hyx::logger.pop_prefix();
        }

        if (images.size() == 0) {
            throw std::runtime_error("Too few images to output a pdf.");
        }

        std::string combined_pages_filepath{(tmppath / "combined_pages")};

        hyx::logger.info("Starting to process pdf\n");
        Magick::writeImages(images.begin(), images.end(), combined_pages_filepath + ".tiff");

        if (auto_mode && !document_text.empty()) {
            filename = std::regex_replace(filename, std::regex("%o"), parse_organization(document_text, "<org>"));
            filename = std::regex_replace(filename, std::regex("%d"), hyx::parser::parse_date(document_text, get_current_date()));
            filename = std::regex_replace(filename, std::regex("%s"), hyx::parser::parse_store(document_text, "<store>"));
            filename = std::regex_replace(filename, std::regex("%t"), hyx::parser::parse_transaction(document_text, "<transaction>"));
        }
        hyx::logger.debug("File name is \'{}\'\n", filename);

        if (!tess_api->ProcessPages((combined_pages_filepath + ".tiff").c_str(), nullptr, static_cast<int>(10'000 * images.size()), std::make_unique<tesseract::TessPDFRenderer>(combined_pages_filepath.c_str(), tess_api->GetDatapath(), false).get())) {
            hyx::logger.warning("OCR taking too long: skipping\n");
            Magick::writeImages(images.begin(), images.end(), (combined_pages_filepath + ".pdf"));
        }

        std::error_code ecode;
        std::filesystem::rename(combined_pages_filepath + ".pdf", outpath / (filename + ".pdf"), ecode);
        if (ecode.value() != 0) {
            hyx::logger.warning("Failed to move output file by renaming: \'{}\' -> Trying copy instead\n", ecode.message());
            if (!std::filesystem::copy_file(combined_pages_filepath + ".pdf", outpath / (filename + ".pdf"), std::filesystem::copy_options::update_existing, ecode)) {
                throw std::runtime_error("Failed to move or copy output file: \'" + ecode.message() + "\'");
            }
        }
        hyx::logger.debug("Moved file successfully\n");
    }
    catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        hyx::logger.error("{}\n", e.what());
        return 1;
    }
    // catch (const Magick::Error& me) {
    //     // return 2;
    // }

    hyx::logger.info("Document ready!\n");
    return 0;
}
