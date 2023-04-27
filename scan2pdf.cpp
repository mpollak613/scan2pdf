/**
 * @file scan2pdf.cpp
 * @copyright
 * Copyright 2022-2023 Michael Pollak.
 * All rights reserved.
 */

#include <Magick++.h> // non-standard
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <hytrax/hyx_filesystem.h>
#include <hytrax/hyx_logger.h>
#include <hytrax/hyx_parser.h>
#include <hytrax/hyx_sane.h>
#include <hytrax/hyx_tesseract.h>
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
#include <variant>
#include <vector>

#ifdef HAVE_LIBTIFF
#include <tiffio.h>   // non-standard
#include <tiffio.hxx> // non-standard
#endif                // !HAVE_LIBTIFF

constexpr static std::string_view version{"2.0.c.0"};

constexpr long double rgb_value(long double quantum_val);

long double operator""_quantum_percent(long double percent);

hyx::logger& logger{hyx::logger::get_instance()};
hyx::sane_init& sane{hyx::sane_init::get_instance()};

static std::string tmp_path{std::filesystem::temp_directory_path() / "scan2pdf/"};
static std::unordered_map<std::string, std::variant<SANE_String_Const, SANE_Word>> sane_options{
    {SANE_NAME_SCAN_SOURCE, "ADF Duplex"},
    {SANE_NAME_SCAN_MODE, SANE_VALUE_SCAN_MODE_COLOR},
    {SANE_NAME_SCAN_RESOLUTION, 300},
    {SANE_NAME_PAGE_HEIGHT, std::numeric_limits<SANE_Word>::max()},
    {SANE_NAME_PAGE_WIDTH, std::numeric_limits<SANE_Word>::max()},
    {"ald", true}};

void progress_meter(int perc_time, const std::string& prefix = "", const std::string& postfix = "");

std::string get_curent_date();

Magick::Image get_next_image(hyx::sane_device* device);

void print_help();

void print_version();

void exit_all();

static void dump_image(Magick::Image& img, const std::string& name, bool should_stop = false)
{
#ifdef DEBUG
    img.write(hyx::get_log_path() / "scan2pdf" / std::string("debugged_image_" + name + ".jpg"));
    if (should_stop) {
        throw std::runtime_error("output debug image and exiting!");
    }
#endif
}

int main(int argc, char** argv)
{
    std::string filename;
    std::string outpath{"./"};

    std::span args(argv, argc);
    // empty
    if (args.size() == 1) {
        print_help();
        return 0;
    }
    for (auto arg = args.begin() + 1; arg != args.end(); ++arg) {
        // -h | --help
        if (std::strncmp(*arg, "-h", 2) == 0 || std::strncmp(*arg, "--help", 6) == 0) {
            print_help();
            return 0;
        }
        // -v | --version
        else if (std::strncmp(*arg, "-v", 2) == 0 || std::strncmp(*arg, "--version", 9) == 0) {
            print_version();
            return 0;
        }
        // -r resolution | --resolution
        else if ((arg + 1 != args.end()) && (std::strncmp(*arg, "-r", 2) == 0 || std::strncmp(*arg, "--resolution", 12) == 0)) {
            ++arg;
            sane_options.at(SANE_NAME_SCAN_RESOLUTION) = std::stoi(*arg);
        }
        // -o outpath | --outpath outpath
        else if ((arg + 1 != args.end()) && (std::strncmp(*arg, "-o", 2) == 0 || std::strncmp(*arg, "--outpath", 13) == 0)) {
            ++arg;
            if (std::filesystem::exists(*arg)) {
                outpath = std::filesystem::absolute(*arg);
            }
            else {
                std::cout << "Path " << *arg << " does not exist!\n";
                return 1;
            }
        }
        // bad option
        else if (*arg[0] == '-') {
            std::cout << "Unkown option \'" << *arg << "\'!\n";
            return 1;
        }
        // stop at filename
        else {
            filename = *arg;
            break;
        }
    }

    if (filename.empty()) {
        std::cout << "No filename detected!\n";
        return 1;
    }

    try {
        logger.open(hyx::get_log_path() / "scan2pdf" / "scan2pdf.log");
        logger.info << "======Starting Program======\n";

        logger.info << "Initializing components\n";
        hyx::tess_base_api api(nullptr, "eng");
        api.get()->SetVariable("debug_file", hyx::get_log_path().append("scan2pdf").append("tess.log").c_str());

        Magick::InitializeMagick(args.front());
        MagickCore::SetLogName(hyx::get_log_path().append("magick.log").c_str());
        logger.info << "All components initialized\n";

        hyx::sane_device* device{sane.open_device()};

        for (auto opt : device->get_options()) {
            if (sane_options.contains(opt->name)) {
                if (auto bop{dynamic_cast<hyx::sane_device::bool_option*>(opt)}) {
                    device->set_option(bop, std::get<SANE_Bool>(sane_options.at(opt->name)));
                }
                else if (auto fop{dynamic_cast<hyx::sane_device::fixed_option*>(opt)}) {
                    device->set_option(fop, std::get<SANE_Fixed>(sane_options.at(opt->name)));
                }
                else if (auto iop{dynamic_cast<hyx::sane_device::int_option*>(opt)}) {
                    device->set_option(iop, std::get<SANE_Int>(sane_options.at(opt->name)));
                }
                else if (auto sop{dynamic_cast<hyx::sane_device::string_option*>(opt)}) {
                    device->set_option(sop, std::get<SANE_String_Const>(sane_options.at(opt->name)));
                }
            }
        }

        if (!std::filesystem::exists(tmp_path)) {
            std::filesystem::create_directory(tmp_path);
        }

        // we start processing images
        logger.info << "Starting processing with filename: \'" << filename << "\'" << '\n';
        std::vector<Magick::Image> images;
        std::string document_text = "";
        for (size_t i = 0; device->start(); ++i) {
            Magick::Image image{get_next_image(device)};

            dump_image(image, std::to_string(i) + std::to_string(1) + "initial");

            logger.info << "Image " << i << ": Digesting image" << '\n';
            std::cout << "Digesting Page(" << i + 1 << "):\n";

            // Set image settings
            image.compressType(Magick::LZWCompression);
            image.density(Magick::Point(std::get<SANE_Word>(sane_options.at(SANE_NAME_SCAN_RESOLUTION))));

            progress_meter(0, "  Processing image: ");

            // Find the color of the scan background.
            Magick::Color border_color{image.pixelColor(0, 0)};
            logger.debug << "Image " << i << ": Setting border color as R: " << rgb_value(border_color.quantumRed()) << " G: " << rgb_value(border_color.quantumGreen()) << " B: " << rgb_value(border_color.quantumBlue()) << '\n';
            progress_meter(10, "  Processing image: ");

            // Trim the extra blank scan area on the top.
            logger.info << "Image " << i << ": Removing top scan area" << '\n';
            image.colorFuzz(10.0_quantum_percent);
            image.splice(Magick::Geometry(0, 1), Magick::Color("black"));
            image.trim();
            image.repage();
            image.chop(Magick::Geometry(0, 1));
            image.repage();
            image.colorFuzz(0.0);
            progress_meter(20, "  Processing image: ");

            dump_image(image, std::to_string(i) + std::to_string(2) + "top-trim");

            // Attempt to deskew image.
            logger.info << "Image " << i << ": Deskewing image" << '\n';
            Magick::Image canny_image{image};
            canny_image.colorSpace(Magick::LinearGRAYColorspace);
            canny_image.artifact("morphology:compose", "lighten");
            canny_image.morphology(Magick::ConvolveMorphology, Magick::RobertsKernel, "@");
            canny_image.negate();
            canny_image.threshold(80.0_quantum_percent);
            canny_image.deskew(80.0_quantum_percent);
            image.backgroundColor(border_color);
            double deskew_angle = std::stod(canny_image.formatExpression("%[deskew:angle]"), nullptr);
            image.rotate(deskew_angle);
            image.artifact("trim:background-color", border_color);
            image.artifact("trim:percent-background", "0%");
            image.trim();
            image.repage();
            progress_meter(50, "  Processing image: ");
            logger.debug << "Image " << i << ": Deskewing by " << deskew_angle << " degrees" << '\n';

            dump_image(image, std::to_string(i) + std::to_string(3) + "deskewed");
            dump_image(canny_image, std::to_string(i) + std::to_string(3) + "canny-deskew");

            canny_image = image;
            canny_image.colorSpace(Magick::LinearGRAYColorspace);
            canny_image.artifact("morphology:compose", "lighten");
            canny_image.morphology(Magick::ConvolveMorphology, Magick::RobertsKernel, "@");
            canny_image.negate();
            canny_image.threshold(87.0_quantum_percent);
            canny_image.shave("1x1"); // remove black border
            dump_image(canny_image, std::to_string(i) + std::to_string(4) + "canny-before-trim");
            canny_image.artifact("trim:background-color", "white");
            canny_image.artifact("trim:percent-background", "99.7%");
            canny_image.trim();
            image.crop(canny_image.formatExpression("%wx%h%X+%Y"));
            image.repage();
            progress_meter(70, "  Processing image: ");

            // Remove 1% from the top (and more as-neeed) to remove any shadow left by the first trim.
            logger.info << "Image " << i << ": Trimming shadow" << '\n';
            image.rotate(180);
            image.chop(Magick::Geometry(0, 10));
            // image.chop(Magick::Geometry(0, std::stoul(image.formatExpression("%h")) * 0.01));
            image.rotate(180);
            image.repage();

            dump_image(canny_image, std::to_string(i) + std::to_string(4) + "canny");
            dump_image(image, std::to_string(i) + std::to_string(4) + "normal");

            progress_meter(100, "  Processing image: ");

            std::cout << "  Attempting to reduce file size:\n";
            // Check if the images contain little to no color (fmw42: https://legacy.imagemagick.org/discourse-server/viewtopic.php?t=19580).
            logger.info << "Image " << i << ": Checking if blank" << '\n';
            Magick::Image gray_factor_image{image};
            gray_factor_image.colorSpace(Magick::HSLColorspace);
            double gray_factor = std::stod(gray_factor_image.formatExpression("%[fx:mean.g]"));

            Magick::Image pure_bw_image{image};
            pure_bw_image.gamma(2.2);
            pure_bw_image.solarize(50.0_quantum_percent);
            pure_bw_image.colorSpace(Magick::GRAYColorspace);
            auto image_stats{pure_bw_image.statistics().channel(Magick::PixelChannel::GrayPixelChannel)};
            logger.debug << "Image " << i << ": Gray mean: " << rgb_value(image_stats.mean()) << '\n';
            logger.debug << "Image " << i << ": Gray std-dev: " << rgb_value(image_stats.standardDeviation()) << '\n';
            if (rgb_value(image_stats.mean()) < (255.0 / 4.0) && rgb_value(image_stats.standardDeviation() - image_stats.mean()) < 15) {
                gray_factor = 0.0000005;
            }
            logger.debug << "Image " << i << ": Gray Factor: " << gray_factor << '\n';

            // Adjust gamma to correct values.
            image.gamma(2.2);
            image.enhance();

            dump_image(image, std::to_string(i) + std::to_string(7) + "enhanced");

            if (gray_factor < 0.1) {
                // Adjust contrast and fill background.
                image.brightnessContrast(0, 30);
                image.opaque(Magick::Color("white"), Magick::Color("white"));
                logger.debug << "Image " << i << ": increasing contrast" << '\n';
            }

            if (gray_factor < 0) {
                // Additionally, convert to monocrome.
                // TODO: brighten image and replace whites before thresholding
                // image.brightnessContrast(0, 20);
                image.autoThreshold(Magick::KapurThresholdMethod);
                logger.debug << "Image " << i << ": applying threshold" << '\n';
            }
            else if (gray_factor < 0.00055) {
                // Additionally, convert to grayscale.
                image.colorSpace(Magick::LinearGRAYColorspace);
                logger.debug << "Image " << i << ": converting to greyscale color space" << '\n';
            }

            dump_image(image, std::to_string(i) + std::to_string(8) + "reduced");

            // Find mostly white images using mean 'whiteness'
            Magick::Image perc_white_image{image};
            perc_white_image.whiteThreshold("90%");
            double perc_white = std::stod(perc_white_image.separate(Magick::GreenChannel).formatExpression("%[fx:mean]"));

            // Ask user if almost white images should be deleted.
            bool keep_image{true};
            logger.debug << "Image " << i << ": Percent white: " << perc_white << '\n';
            if (perc_white > 0.98) {
                logger.info << "Image " << i << ": May be blank" << '\n';

                std::string uans;
                std::cout << "  Possible blank page found (pg. " << i + 1 << "). Keep the page? [Y/n] ";
                std::getline(std::cin, uans);

                if (!uans.empty() && (uans.front() == 'N' || uans.front() == 'n')) {
                    // Although we don't really 'remove' the image here, it is still implicitly destroyed.
                    std::cout << "  Removing page: " << i + 1 << '\n';
                    logger.info << "Image " << i << ": Removing image" << '\n';
                    keep_image = false;
                }
                else {
                    std::cout << "  Keeping page: " << i + 1 << '\n';
                    logger.info << "Image " << i << ": Keeping image" << '\n';
                }
            }
            else {
                std::cout << "  page not blank (" << perc_white << ")\n";
            }

            if (keep_image) {
                Magick::Blob bimage;
                image.write(&bimage, "tiff");

                // Attempt to orient using tesseract.
                std::cout << "  Attempting to re-orient image: ";
                Pix* pimage = pixReadMemTiff(static_cast<const unsigned char*>(bimage.data()), bimage.length(), 0);
                api.get()->SetImage(pimage);

                int ori_deg;
                float ori_conf;
                const char* script_name;
                float script_conf;
                logger.info << "Image " << i << ": Reorienting" << '\n';
                api.get()->DetectOrientationScript(&ori_deg, &ori_conf, &script_name, &script_conf);

                logger.debug << "Image " << i << ": Rotating by " << ori_deg << " degrees" << '\n';
                image.rotate(360 - ori_deg);
                std::cout << "Rotated image " << ori_deg << " degrees.\n";

                logger.info << "Image " << i << ": Collecting text" << '\n';
                pimage = pixRotateOrth(pimage, ori_deg / 90);
                api.get()->SetImage(pimage);
                document_text += api.get()->GetUTF8Text();

                pixDestroy(&pimage);

                logger.info << "Image " << i << ": Adding to list of images" << '\n';
                images.push_back(image);
            }
        }

        if (images.size() == 0) {
            throw std::runtime_error("Too few images to output a pdf.");
        }

        logger.info << "Starting to process pdf" << '\n';
        std::cout << "Converting to a searchable PDF: ";
        Magick::writeImages(images.begin(), images.end(), tmp_path + filename + ".tiff");

        std::string filedate{hyx::parser::parse_date(document_text, get_curent_date())};
        logger.debug << "Setting date to \'" << filedate << "\'" << '\n';

        std::string filestore{hyx::parser::parse_store(document_text, "<store>")};
        logger.debug << "Setting store to \'" << filestore << "\'" << '\n';

        std::string filetransaction{hyx::parser::parse_transaction(document_text, "<transaction>")};
        logger.debug << "Setting transaction to \'" << filetransaction << "\'" << '\n';

        std::unique_ptr<tesseract::TessPDFRenderer> renderer{new tesseract::TessPDFRenderer(std::string(tmp_path + filename).c_str(), api.get()->GetDatapath(), false)};
        if (!api.get()->ProcessPages(std::string(tmp_path + filename + ".tiff").c_str(), nullptr, 5000, renderer.get())) {
            throw std::runtime_error("Could not write output file.");
        }

        std::string newFileName = filedate + "_" + filename + "_" + filestore + "_" + filetransaction + ".pdf";

        std::error_code ecode;
        std::filesystem::rename(tmp_path + filename + ".pdf", outpath + newFileName, ecode);
        if (ecode.value() != 0) {
            logger.warning << "Failed to move output file by renaming: \'" << ecode.message() << "\' -> Trying copy instead" << '\n';
            if (!std::filesystem::copy_file(tmp_path + filename + ".pdf", outpath + newFileName, std::filesystem::copy_options::update_existing, ecode)) {
                throw std::runtime_error("Failed to move or copy output file: \'" + ecode.message() + "\'");
            }
        }
        logger.debug << "Moved file successfully" << '\n';

        std::cout << "Document ready!\n";
    }
    catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        logger.error << e.what() << '\n';

        exit_all();
        return 1;
    }

    exit_all();
    return 0;
}

constexpr long double rgb_value(long double quantum_val)
{
    return quantum_val / QuantumRange * 255;
}

long double operator""_quantum_percent(long double percent)
{
    return (percent / 100) * QuantumRange;
}

void progress_meter(int perc_time, const std::string& prefix, const std::string& postfix)
{
    int max_time{40};

    if (perc_time < 0 || perc_time > 100) {
        throw std::invalid_argument("Percentage must be between 0% and 100%");
    }

    int scaled_time{perc_time * max_time / 100};

    std::cout << '\r' << prefix << "[";

    for (int i = 0; i < max_time; ++i) {
        if (i < scaled_time) {
            std::cout << '#';
        }
        else {
            std::cout << '-';
        }
    }

    std::cout << ']' << " " << perc_time << '%' << postfix;

    if (scaled_time == max_time) {
        std::cout << '\n';
    }

    // flush to ensure each progress line prints all at once
    std::cout.flush();
}

std::string get_curent_date()
{
    char date[13];
    time_t raw_time{std::time(nullptr)};
    std::strftime(date, 13, "%Y-%m-%d", std::localtime(&raw_time));

    return date;
}

Magick::Image get_next_image(hyx::sane_device* device)
{
    SANE_Parameters sane_params{device->get_parameters()};

    std::ostringstream tiff_ostream;
    TIFF* tifffile = TIFFStreamOpen("tiff_frame", &tiff_ostream);
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

    SANE_Int buf_size = sane_params.bytes_per_line;
    SANE_Byte* data = static_cast<SANE_Byte*>(_TIFFmalloc(buf_size));
    for (int y = 0; device->read(data, buf_size); ++y) {
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
    std::cout << "Copyright (C) 2022 Michael Pollak\n";
    std::cout << "License Apache-2.0: Apache License, Version 2.0 <https://www.apache.org/licenses/LICENSE-2.0>\n";
    std::cout << '\n';
    std::cout << "Unless required by applicable law or agreed to in writing, softwareme distributed under the License is distributed on an \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.\n";
}

void exit_all()
{
    std::uintmax_t ndeleted = std::filesystem::remove_all(tmp_path);
    if (ndeleted == 0) {
        logger.warning << "Could not delete temporary files" << '\n';
    }
    else {
        logger.debug << "Deleted " << ndeleted << " temporary files" << '\n';
    }
}
