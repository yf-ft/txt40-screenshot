/**
 * @file screenshot.cpp
 * @brief A command-line tool for capturing screenshots on the fischertechnik TXT 4.0 controller.
 *
 * This program captures the current contents of the screen buffer and saves it as a PNG file.
 *
 * @version 0.1.0
 * @date 2024-07-26
 * @copyright (c) 2024 Yannik Friedrich
 * 
 * @license MIT License
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <png.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

// Config TXT 4.0 display
constexpr auto WIDTH = 240UL;
constexpr auto HEIGHT = 320UL;
constexpr auto FRAME_BUF_PATH = "/dev/fb0";

// String buffer sizes
constexpr auto DATE_STR_SIZE = 20;
constexpr auto COUNTER_STR_SIZE = 10;

// Constants for color conversion (RGB565 to RGB888)
constexpr auto RED_MAX = 31;
constexpr auto GREEN_MAX = 63;
constexpr auto BLUE_MAX = 31;
constexpr auto COLOR_MAX = 255;

struct RGB565 {
    uint16_t blue : 5;
    uint16_t green : 6;
    uint16_t red : 5;
};

struct RGB888 {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

auto getDate() -> std::array<char, DATE_STR_SIZE> {
    auto now = time(nullptr);
    auto* ltm = localtime(&now); // NOLINT (ignore thread unsafety)

    auto date = std::array<char, DATE_STR_SIZE>();
    (void)strftime(date.data(), sizeof(date), "%Y-%m-%d-%H-%M-%S", ltm);
    return date;
}

auto fileExists(const char* path) -> bool {
    struct stat buffer {};
    return (stat(path, &buffer) == 0);
}

auto generateFileName(std::string_view directory, std::string_view baseName, bool includeDate)
    -> std::string {
    auto dateStr = includeDate ? getDate() : std::array<char, DATE_STR_SIZE>();

    // calculate the required size for the file path string
    auto reqSize = (directory.empty() ? 0 : directory.size() + 1) // +1 for the '/'
                   + baseName.size() + 1                          // +1 for the '-'
                   + dateStr.size() + 4;                          // +4 for ".png"

    auto filePath = std::string();
    filePath.reserve(reqSize);

    // construct the initial file path
    if (!directory.empty()) {
        filePath.append(directory);
        filePath.append("/");
    }
    filePath.append(baseName);
    if (includeDate) {
        filePath.append("-");
        filePath.append(dateStr.data());
    }
    filePath.append(".png");

    // size of the base file path (without ".png")
    auto basePathSize = filePath.size() - 4;
    auto counterStr = std::array<char, COUNTER_STR_SIZE>();
    auto counter = 1;

    // Loop to ensure unique file name
    while (fileExists(filePath.c_str())) {
        // convert the counter number into a string
        auto [endPtr, ec] = std::to_chars(counterStr.begin(), counterStr.end(), counter);
        if (ec != std::errc()) {
            throw std::runtime_error("Failed to convert number to string");
        }
        // strip the counter number and the suffix
        filePath.resize(basePathSize);

        // append the new counter & suffix
        filePath.append("-");
        filePath.append(counterStr.begin(), endPtr);
        filePath.append(".png");
        ++counter;
    }

    return filePath;
}

auto convertRgb565ToRgb888(const std::vector<RGB565>& buffer565) -> std::vector<RGB888> {
    auto buffer888 = std::vector<RGB888>(WIDTH * HEIGHT);
    for (size_t i = 0; i < WIDTH * HEIGHT; ++i) {
        buffer888[i].red = buffer565[i].red * COLOR_MAX / RED_MAX;
        buffer888[i].green = buffer565[i].green * COLOR_MAX / GREEN_MAX;
        buffer888[i].blue = buffer565[i].blue * COLOR_MAX / BLUE_MAX;
    }

    return buffer888;
}

// NOLINTBEGIN: libpng is a C library requiring some "unsafe" constructs
auto writePng(const char* filename, const std::vector<RGB888>& buffer) -> void {
    FILE* fp = fopen(filename, "wb");
    if (fp == nullptr) {
        std::cerr << "Failed to open file for writing\n";
        return;
    }

    auto* png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        std::cerr << "Failed to create PNG write struct\n";
        fclose(fp);
        return;
    }

    auto* info = png_create_info_struct(png);
    if (info == nullptr) {
        std::cerr << "Failed to create PNG info struct\n";
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return;
    }

    if (setjmp(png_jmpbuf(png))) {
        std::cerr << "Failed to set PNG jump buffer\n";
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return;
    }

    png_init_io(png, fp);

    png_set_IHDR(
        png,
        info,
        WIDTH,
        HEIGHT,
        8, // bit depth;
        PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    png_write_info(png, info);

    for (auto y = 0U; y < HEIGHT; ++y) {
        png_write_row(png, reinterpret_cast<const unsigned char*>(&buffer[y * WIDTH]));
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
}
// NOLINTEND

auto main(int argc, char* argv[]) -> int {
    auto baseName = std::string("screenshot");
    auto directory = std::string();
    auto outputFile = std::string();
    auto includeDate = true;
    auto showHelp = false;

    auto options = std::array{
        option{"name", required_argument, 0, 'n'},
        option{"directory", required_argument, 0, 'd'},
        option{"no-date", no_argument, 0, 'x'},
        option{"help", no_argument, 0, 'h'},
        option{0, 0, 0, 0}
    };

    auto optIndex = 0;
    auto shortOpt = 0;

    // NOLINTNEXTLINE (ignore getopt_long's thread unsafety)
    while ((shortOpt = getopt_long(argc, argv, "n:d:xh", options.data(), &optIndex)) != -1) {
        switch (shortOpt) {
        case 'n':
            baseName = optarg;
            break;
        case 'd':
            directory = optarg;
            break;
        case 'x':
            includeDate = false;
            break;
        case 'h':
        default:
            showHelp = true;
            break;
        }
    }

    if (showHelp) {
        std::cout << "Usage: " << argv << " [options]\n"
                  << "Options:\n"
                  << "  -n, --name      Base name for the screenshot file "
                     "(default: screenshot)\n"
                  << "  -d, --directory Directory to save the screenshot (default: "
                     "current directory)\n"
                  << "  -x, --no-date   Do not include the date in the filename\n"
                  << "  -h, --help      Show this help message\n";
        return 0;
    }

    outputFile = generateFileName(directory, baseName, includeDate);

    auto frameBuf = std::ifstream(FRAME_BUF_PATH, std::ios::binary);
    if (not frameBuf.is_open()) {
        std::cerr << "Failed to open frame buffer\n";
        return 1;
    }

    auto buffer565 = std::vector<RGB565>(WIDTH * HEIGHT);
    frameBuf.read(
        reinterpret_cast<char*>(buffer565.data()), // NOLINT (reinterpret_cast)
        static_cast<long>(buffer565.size() * sizeof(RGB565))
    );
    if (not frameBuf) {
        std::cerr << "Failed to read frame buffer\n";
        return 1;
    }

    auto buffer888 = convertRgb565ToRgb888(buffer565);
    writePng(outputFile.c_str(), buffer888);

    std::cout << "Screenshot saved as " << outputFile << "\n";
    return 0;
}