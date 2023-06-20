/**
 * @file scan2pdf.cpp
 * @copyright
 * Copyright 2022-2023 Michael Pollak.
 * All rights reserved.
 */

#include <Magick++.h> // non-standard
#include <any>
#include <chrono>
#include <concepts>
#include <cstdlib>
#include <filesystem>
#include <guess_organization.h>
#include <hytrax/hyx_filesystem.h>
#include <hytrax/hyx_leptonica.h>
#include <hytrax/hyx_logger.h>
#include <hytrax/hyx_parser.h>
#include <hytrax/hyx_sane.h>
#include <iostream>
#include <leptonica/allheaders.h> // non-standard
#include <limits>
#include <math.h>
#include <memory>
#include <numbers>
#include <ranges>
#include <sane/sane.h>     // non-standard
#include <sane/saneopts.h> // non-standard
#include <span>
#include <stdio.h>
#include <strings.h>
#include <tesseract/baseapi.h>  // non-standard
#include <tesseract/renderer.h> // non-standard
#include <time.h>
#include <unordered_map>
#include <vector>

#ifdef HAVE_LIBTIFF
#include <tiffio.h>   // non-standard
#include <tiffio.hxx> // non-standard
#endif                // !HAVE_LIBTIFF

constexpr static std::string_view version{"2.0.c.3"};

// our logger needs to be global so we can call it from anywhere.
hyx::logger* logger;

/**
 * @brief Global options.
 */
static std::unordered_map<std::string, std::any> sane_options{
    {SANE_NAME_SCAN_SOURCE, "ADF Duplex"},
    {SANE_NAME_SCAN_MODE, SANE_VALUE_SCAN_MODE_COLOR},
    {SANE_NAME_SCAN_RESOLUTION, 300},
    {SANE_NAME_PAGE_HEIGHT, std::numeric_limits<SANE_Word>::max()},
    {SANE_NAME_PAGE_WIDTH, std::numeric_limits<SANE_Word>::max()},
    {"ald", true}};

long double operator""_quantum_percent(long double percent);
long double operator"" _quantum_percent(unsigned long long percent);

constexpr long double rgb_value(long double quantum_val);

std::string get_curent_date();

Magick::Image get_next_image(hyx::sane_device* device);

void print_help();
void print_version();

void set_device_options(hyx::sane_device* device);

void trim_shadow(Magick::Image& image);
void deskew(Magick::Image& image);
void trim_edges(Magick::Image& image);
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

static void dump_image(Magick::Image& img, const std::string& name)
{
#ifdef DEBUG
    img.write(hyx::log_path() / "scan2pdf" / ("debugged_image_" + name + ".png"));
#endif
}

int main(int argc, char** argv)
{
    std::string filename;
    auto auto_mode{false};
    std::filesystem::path outpath{"./"};
    const hyx::temporary_path tmppath{std::filesystem::temp_directory_path() / "scan2pdf"};
    std::filesystem::path logpath{hyx::log_path() / "scan2pdf"};

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
        else if (arg == "--auto") {
            auto_mode = true;
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

    if (filename.empty() && !auto_mode) {
        std::cout << "No filename detected!\n";
        return 1;
    }

    try {
        logger = hyx::logger::get_instance(logpath / "scan2pdf.log");
        logger->info << "======Starting Program======\n";
    }
    catch (const std::exception& e) {
        std::cout << "WARNING: Failed to open file for logging: " << logpath / "scan2pdf.log" << '\n';
        // it is ok to continue without logging opened.
    }

    hyx::sane_init* sane;
    std::unique_ptr<tesseract::TessBaseAPI> tess_api;

    try {
        logger->info << "Initializing components\n";

        sane = &hyx::sane_init::get_instance();

        tess_api = std::make_unique<tesseract::TessBaseAPI>();
        if (tess_api->Init(nullptr, "eng")) {
            throw std::runtime_error("Could not initialize tesseract");
        }
        tess_api->SetVariable("debug_file", (logpath / "tess.log").c_str());

        Magick::InitializeMagick(*argv);
        logger->info << "All components initialized\n";
    }
    catch (const std::exception& e) {
        std::cout << "Failed to Initialize: " << e.what() << '\n';
        logger->fatal << "Failed to Initialize: " << e.what() << '\n';
        return 1;
    }

    try {
        hyx::sane_device* device{sane->open_device()};

        set_device_options(device);

        if (!std::filesystem::exists(tmppath)) {
            std::filesystem::create_directory(tmppath);
        }

        // we start processing images
        logger->info << "Scanning Document" << '\n';
        std::vector<Magick::Image> images{};
        std::string document_text{};
        for (auto i = 0; device->start(); ++i) {
            Magick::Image image{get_next_image(device)};
            logger->push_prefix("Image " + std::to_string(i));

            dump_image(image, std::to_string(1) + "initial" + std::to_string(rand()));

            logger->info << "Digesting image" << '\n';
            std::cout << "Digesting Page(" << i + 1 << "):\n";

            // Set image settings
            image.compressType(Magick::LZWCompression);
            image.density(std::any_cast<SANE_Word>(sane_options.at(SANE_NAME_SCAN_RESOLUTION)));

            // Attempt to deskew image.
            deskew(image);

            // Remove the scanner edges from the image
            trim_edges(image);

            // Adjust gamma to correct values.
            image.gamma(2.2);
            image.enhance();

            dump_image(image, std::to_string(7) + "enhanced" + std::to_string(i));

            std::cout << "  Attempting to reduce file size:\n";

            // Ask user if almost white images should be deleted.
            auto keep_image{true};
            if (is_white(image)) {
                logger->info << "May be blank" << '\n';

                std::string user_answer;
                std::cout << "  Possible blank page found (pg. " << i + 1 << "). Keep the page? [Y/n] ";
                std::getline(std::cin, user_answer);

                if (!user_answer.empty() && (user_answer.front() == 'N' || user_answer.front() == 'n')) {
                    // Although we don't really 'remove' the image here, it is still implicitly destroyed.
                    std::cout << "  Removing page: " << i + 1 << '\n';
                    logger->info << "Removing image" << '\n';
                    keep_image = false;
                }
                else {
                    std::cout << "  Keeping page: " << i + 1 << '\n';
                    logger->info << "Keeping image" << '\n';
                }
            }
            else {
                std::cout << "  page not blank\n";
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

                dump_image(image, std::to_string(8) + "reduced" + std::to_string(rand()));

                hyx::unique_pix pimage{magick2pix(image)};

                // Attempt to orient using tesseract.
                std::cout << "  Attempting to re-orient image: ";

                auto ori_deg{get_orientation(tess_api.get(), pimage.get())};

                logger->debug << "Rotating by " << ori_deg << " degrees" << '\n';
                std::cout << "Rotated image " << ori_deg << " degrees.\n";
                image.rotate(360 - ori_deg);

                logger->info << "Collecting text" << '\n';
                pimage.reset(pixRotateOrth(pimage.get(), ori_deg / 90));
                document_text += get_text(tess_api.get(), pimage.get());

                logger->info << "Adding to list of images" << '\n';
                images.push_back(image);
            }

            logger->pop_prefix();
        }

        if (images.size() == 0) {
            throw std::runtime_error("Too few images to output a pdf.");
        }

        std::string combined_pages_filepath{(tmppath / "combined_pages")};

        logger->info << "Starting to process pdf" << '\n';
        std::cout << "Converting to a searchable PDF: ";
        Magick::writeImages(images.begin(), images.end(), combined_pages_filepath + ".tiff");

        if (auto_mode && !document_text.empty()) {
            filename = guess_organization({document_text});
        }
        else {
            filename = "<name>";
        }
        logger->debug << "Setting name to \'" << filename << "\'" << '\n';


        std::string filedate{hyx::parser::parse_date(document_text, get_curent_date())};
        logger->debug << "Setting date to \'" << filedate << "\'" << '\n';

        std::string filestore{hyx::parser::parse_store(document_text, "<store>")};
        logger->debug << "Setting store to \'" << filestore << "\'" << '\n';

        std::string filetransaction{hyx::parser::parse_transaction(document_text, "<transaction>")};
        logger->debug << "Setting transaction to \'" << filetransaction << "\'" << '\n';

        if (!tess_api->ProcessPages((combined_pages_filepath + ".tiff").c_str(), nullptr, 5000, std::make_unique<tesseract::TessPDFRenderer>(combined_pages_filepath.c_str(), tess_api->GetDatapath(), false).get())) {
            throw std::runtime_error("Could not write output file.");
        }

        std::string newFileName = filedate + "_" + filename + "_" + filestore + "_" + filetransaction + ".pdf";

        std::error_code ecode;
        std::filesystem::rename(combined_pages_filepath + ".pdf", outpath / newFileName, ecode);
        if (ecode.value() != 0) {
            logger->warning << "Failed to move output file by renaming: \'" << ecode.message() << "\' -> Trying copy instead" << '\n';
            if (!std::filesystem::copy_file(combined_pages_filepath + ".pdf", outpath / newFileName, std::filesystem::copy_options::update_existing, ecode)) {
                throw std::runtime_error("Failed to move or copy output file: \'" + ecode.message() + "\'");
            }
        }
        logger->debug << "Moved file successfully" << '\n';
    }
    catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        logger->error << e.what() << '\n';
        return 1;
    }
    // catch (const Magick::Error& me) {
    //     // return 2;
    // }

    std::cout << "Document ready!\n";
    logger->info << "Document ready!\n";
    return 0;
}

constexpr long double rgb_value(long double quantum_val)
{
    return quantum_val / QuantumRange * 255;
}

long double operator""_quantum_percent(long double percent)
{
    return ((percent > 1.0l) ? (percent / 100.0l) : percent) * QuantumRange;
}

long double operator""_quantum_percent(unsigned long long percent)
{
    return ((percent > 1.0l) ? (percent / 100.0l) : percent) * QuantumRange;
}

std::string get_curent_date()
{
    auto date_length{10};
    std::string date;
    date.resize(date_length);
    auto raw_time{std::time(nullptr)};
    std::strftime(date.data(), date_length, "%Y-%m-%d", std::localtime(&raw_time));

    return date;
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
    for (auto y = 0; device->read(data, buf_size); ++y) {
        if (TIFFWriteScanline(tifffile, data, y, 0) != 1) {
            std::cout << "bad write!\n";
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

    return Magick::Image(Magick::Blob(tiff_ostream.view().data(), tiff_ostream.view().size()));
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

void trim_shadow(Magick::Image& image)
{
    logger->info << "Removing top scan shadow" << '\n';
    image.colorFuzz(10_quantum_percent);
    image.splice(Magick::Geometry(0, 1), Magick::Color("black"));
    image.trim();
    image.repage();
    image.chop(Magick::Geometry(0, 1));
    image.repage();
    image.colorFuzz(0.0);

    dump_image(image, std::to_string(2) + "top-trim" + std::to_string(rand()));
}

void deskew(Magick::Image& image)
{
    logger->info << "Deskewing image" << '\n';
    // Find the color of the scan background.
    Magick::Color border_color{image.pixelColor(0, 0)};
    logger->debug << "Set border color to (" << rgb_value(border_color.quantumRed()) << ", " << rgb_value(border_color.quantumGreen()) << ", " << rgb_value(border_color.quantumBlue()) << ")\n";

    Magick::Image canny_image{image};
    canny_image.colorSpace(Magick::LinearGRAYColorspace);
    canny_image.artifact("morphology:compose", "lighten");
    canny_image.morphology(Magick::ConvolveMorphology, Magick::RobertsKernel, "@");
    canny_image.negate();
    canny_image.threshold(80_quantum_percent);
    canny_image.deskew(80_quantum_percent);

    image.backgroundColor(border_color);
    double deskew_angle = std::stod(canny_image.formatExpression("%[deskew:angle]"), nullptr);
    image.rotate(deskew_angle);
    image.artifact("trim:background-color", border_color);
    image.artifact("trim:percent-background", "0%");
    image.trim();
    image.repage();
    logger->debug << "Deskewing by " << deskew_angle << " degrees" << '\n';

    dump_image(image, std::to_string(3) + "deskewed" + std::to_string(rand()));
    dump_image(canny_image, std::to_string(3) + "canny-deskew" + std::to_string(rand()));
}

void trim_edges(Magick::Image& image)
{
    logger->info << "Trimming edges" << '\n';

    // magick \( in.png -alpha off -brightness-contrast 0x10 \) \( -clone 0 -morphology close rectangle:1x50 -negate +write tmp1.png \) \( -clone 0 -morphology close rectangle:50x1 -negate +write tmp2.png \) \( -clone 1 -clone 2 -evaluate-sequence add +write tmp3.png \) -delete 1,2 -compose plus -composite result.png && magick result.png -threshold 95% -define trim:percent-background=95% -trim +repage result-trimed.png

    Magick::Image image_no_alpha{image};
    image_no_alpha.alpha(false);
    image_no_alpha.brightnessContrast(0, 10);

    dump_image(image_no_alpha, std::to_string(4) + "before_line_remove" + std::to_string(rand()));

    Magick::Image image_vertical_lines{image_no_alpha};
    image_vertical_lines.morphology(Magick::CloseMorphology, Magick::RectangleKernel, "1x50");
    image_vertical_lines.negate();

    dump_image(image_vertical_lines, std::to_string(4) + "vertical_lines" + std::to_string(rand()));

    Magick::Image image_horizontal_lines{image_no_alpha};
    image_horizontal_lines.morphology(Magick::CloseMorphology, Magick::RectangleKernel, "50x1");
    image_horizontal_lines.negate();

    dump_image(image_horizontal_lines, std::to_string(4) + "horizontal_lines" + std::to_string(rand()));

    // we combine both vertical and horizontal lines into one image
    Magick::Image image_lines;
    std::vector line_images{image_vertical_lines, image_horizontal_lines};
    Magick::evaluateImages(&image_lines, line_images.begin(), line_images.end(), Magick::AddEvaluateOperator);

    dump_image(image_lines, std::to_string(4) + "combined_lines" + std::to_string(rand()));

    // now we place the origional image over the inverted lines to remove them
    image_lines.composite(image, "0x0", Magick::PlusCompositeOp);

    dump_image(image_lines, std::to_string(4) + "lines_removed" + std::to_string(rand()));

    image_lines.threshold(95_quantum_percent);
    image_lines.artifact("trim:background-color", "white");
    image_lines.artifact("trim:percent-background", "95");
    image_lines.trim();
    image.crop(image_lines.formatExpression("%wx%h%X+%Y"));
    image.repage();

    dump_image(image, std::to_string(4) + "normal" + std::to_string(rand()));
}

void transform_to_bw(Magick::Image& image)
{
    logger->debug << "Converting to black and white" << '\n';

    image.brightnessContrast(0, 30);
    image.autoThreshold(Magick::KapurThresholdMethod);
    image.resize("200%");
}

void transform_with_text_to_bw(Magick::Image& image)
{
    logger->debug << "Converting to black and white" << '\n';

    // magick in.png -auto-level -unsharp 0x2+1.5+0.05 -resize 200% -auto-threshold otsu out.pdf

    image.autoLevel();
    image.unsharpmask(0, 2, 1.5, 0.05);
    image.resize("200%");
    image.autoThreshold(Magick::OTSUThresholdMethod);
}

void transform_to_grayscale(Magick::Image& image)
{
    logger->debug << "Converting to greyscale" << '\n';

    image.brightnessContrast(0, 30);
    image.opaque(Magick::Color("white"), Magick::Color("white"));
    image.colorSpace(Magick::LinearGRAYColorspace);
    image.resize("200%");
}

int get_orientation(tesseract::TessBaseAPI* tess_api, PIX* pimage)
{
    logger->info << "Getting orientation" << '\n';
    tess_api->SetImage(pimage);

    int ori_deg;
    float ori_conf;
    const char* script_name;
    float script_conf;
    tess_api->DetectOrientationScript(&ori_deg, &ori_conf, &script_name, &script_conf);

    logger->debug << "Orientation off by " << ori_deg << " degrees" << '\n';

    return ori_deg;
}

bool is_grayscale(Magick::Image& image)
{
    logger->push_prefix("Grayscale Check");

    Magick::Image gray_image_test{image};
    gray_image_test.colorSpace(Magick::HSLColorspace);
    auto gray_factor{std::stod(gray_image_test.formatExpression("%[fx:mean.g]"))};
    auto gray_peak{std::stod(gray_image_test.formatExpression("%[fx:maxima.g]"))};

    logger->debug << "Gray mean: " << gray_factor << '\n';
    logger->debug << "Gray peak: " << gray_peak << '\n';

    logger->pop_prefix();

    if (gray_factor < 0.05 && gray_peak < 0.1) {
        return true;
    }

    return false;
}

bool is_bw(Magick::Image& image)
{
    logger->push_prefix("BW Check");

    // magick in.png -solarize 50% -colorspace gray -identify -verbose info:

    Magick::Image bw_image_test{image};
    bw_image_test.solarize(50_quantum_percent);
    bw_image_test.colorSpace(Magick::GRAYColorspace);
    auto image_stats{bw_image_test.statistics().channel(Magick::PixelChannel::GrayPixelChannel)};

    auto mean_gray{rgb_value(image_stats.mean())};
    auto std_diff_mean_gray{std::abs(rgb_value(image_stats.standardDeviation() - image_stats.mean()))};

    logger->debug << "Gray mean: " << mean_gray << '\n';
    logger->debug << "Gray std-dev-mean-diff: " << std_diff_mean_gray << '\n';

    logger->pop_prefix();

    if (mean_gray < (255.0 / 6.0) && std_diff_mean_gray < 15) {
        return true;
    }

    return false;
}

bool is_spot_colored(Magick::Image& image)
{
    logger->push_prefix("Spot Color Check");

    Magick::Image spot_image_test{image};
    spot_image_test.colorSpace(Magick::HCLColorspace);
    spot_image_test.resize("1:50");
    auto gray_factor{std::stod(spot_image_test.formatExpression("%[fx:mean.g]"))};
    auto gray_peak{std::stod(spot_image_test.formatExpression("%[fx:maxima.g]"))};

    logger->debug << "Color mean: " << gray_factor << '\n';
    logger->debug << "Color peak: " << gray_peak << '\n';

    logger->pop_prefix();

    if (gray_factor < 0.05 && gray_peak > 0.3) {
        return true;
    }

    return false;
}

bool is_white(Magick::Image& image)
{
    logger->push_prefix("All White Check");

    Magick::Image percent_white_image{image};
    percent_white_image.whiteThreshold("90%");
    auto percent_white = std::stod(percent_white_image.formatExpression("%[fx:mean]"));

    logger->debug << "Percent white: " << percent_white << '\n';

    logger->pop_prefix();

    if (percent_white > 0.98) {
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
