// Image Brightness Controller
//
// Loads jpeg / png / bmp / gif / tga / ppm / pgm / psd / hdr / pic images,
// changes their brightness and saves the result.
//
//   One-shot:    ./brightness_controller photo.jpg -b 1.5 -o bright.png
//   Interactive: ./brightness_controller            (prompts for everything)

#include "BrightnessController.h"
#include "Image.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kVersion = "1.0.0";

void printUsage(const char* program) {
    std::cout
        << "Image Brightness Controller " << kVersion << "\n\n"
        << "Usage:\n"
        << "  " << program << " <input-image> [options]\n"
        << "  " << program << "                 (interactive mode)\n\n"
        << "Accepted input formats: jpeg, png, bmp, gif, tga, psd, hdr, pic,\n"
        << "                        ppm / pgm / pnm (binary or plain-text)\n"
        << "Output formats:         png, jpg, bmp, tga, ppm, pgm\n\n"
        << "Options:\n"
        << "  -o, --output <file>     Output file path\n"
        << "                          (default: <input>_bright.<same ext>)\n"
        << "  -b, --brightness <val>  Brightness factor 0.0 - 3.0, or a\n"
        << "                          percentage like 140%  (1.0 = unchanged)\n"
        << "  -a, --adjust <n>        Brightness offset -255 - 255\n"
        << "  -i, --interactive       Adjust brightness interactively\n"
        << "  -h, --help              Show this help\n"
        << "  -v, --version           Show version\n\n"
        << "Examples:\n"
        << "  " << program << " photo.jpg -b 1.5 -o brighter.jpg\n"
        << "  " << program << " scan.ppm -b 120% -o dimmed.png\n"
        << "  " << program << " dark.png -a -40 -o darker.png\n"
        << "  " << program << " photo.png            (interactive slider)\n";
}

bool fileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

// Parses "1.5" or "150%" into a factor.
bool parseFactor(const std::string& text, float& out) {
    std::string s = text;
    double multiplier = 1.0;
    if (!s.empty() && s.back() == '%') {
        multiplier = 0.01;
        s.pop_back();
    }
    try {
        std::size_t used = 0;
        const double v = std::stod(s, &used);
        if (used != s.size()) return false;
        out = static_cast<float>(v * multiplier);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseInt(const std::string& text, int& out) {
    try {
        std::size_t used = 0;
        const int v = std::stoi(text, &used);
        if (used != text.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

std::string currentSettings(const BrightnessController& bc) {
    std::ostringstream os;
    os << "factor " << bc.factor() << ", offset " << bc.offset();
    return os.str();
}

// Interactive loop: the user types commands, the result is re-saved each time
// so the change can be previewed in any image viewer.
int runInteractive(Image& original, BrightnessController& controller,
                   const std::string& outputPath) {
    std::cout << "\nLoaded " << original.describe() << "\n"
              << "Result is saved to: " << outputPath << "\n"
              << "Type a brightness factor (0.0 - 3.0), or one of:\n"
              << "  o <n>   set offset (-255 - 255)\n"
              << "  s       save with current settings\n"
              << "  r       reset to the original image\n"
              << "  i       show image info\n"
              << "  h       help        q  quit\n\n";

    auto save = [&](const BrightnessController& bc) -> bool {
        const Image adjusted = bc.apply(original);
        std::string error;
        if (!adjusted.save(outputPath, &error)) {
            std::cerr << "Error: " << error << "\n";
            return false;
        }
        std::cout << "Saved " << outputPath << " (" << currentSettings(bc) << ")\n";
        return true;
    };

    std::string line;
    while (true) {
        std::cout << "brightness> " << std::flush;
        if (!std::getline(std::cin, line)) break;  // EOF

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "q" || cmd == "quit" || cmd == "exit") break;
        if (cmd == "h" || cmd == "help") {
            std::cout << "  <number> or <percent>  set factor, e.g. 1.4 or 140%\n"
                         "  o <n>                  set offset (-255 - 255)\n"
                         "  s                      save now\n"
                         "  r                      reset\n"
                         "  i                      image info\n"
                         "  q                      quit\n";
            continue;
        }
        if (cmd == "i" || cmd == "info") {
            std::cout << original.describe() << "\n";
            continue;
        }
        if (cmd == "r" || cmd == "reset") {
            controller.reset();
            save(controller);
            continue;
        }
        if (cmd == "s" || cmd == "save") {
            save(controller);
            continue;
        }
        if (cmd == "o" || cmd == "offset") {
            std::string arg;
            iss >> arg;
            int value = 0;
            if (arg.empty() || !parseInt(arg, value)) {
                std::cout << "Usage: o <integer between -255 and 255>\n";
                continue;
            }
            if (!controller.setOffset(value)) {
                std::cout << "Offset clamped to " << controller.offset() << "\n";
            }
            save(controller);
            continue;
        }

        float factor = 0.f;
        if (parseFactor(cmd, factor)) {
            if (!controller.setFactor(factor)) {
                std::cout << "Factor clamped to " << controller.factor() << "\n";
            }
            save(controller);
            continue;
        }
        std::cout << "Unknown command '" << cmd << "'. Type h for help, q to quit.\n";
    }

    std::cout << "Bye.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string inputPath;
    std::string outputPath;
    std::string brightnessArg;
    std::string offsetArg;
    bool brightnessGiven = false;
    bool offsetGiven = false;
    bool interactiveFlag = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](std::string& value) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                return false;
            }
            value = argv[++i];
            return true;
        };

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "Image Brightness Controller " << kVersion << "\n";
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (!next(outputPath)) return 1;
        } else if (arg == "-b" || arg == "--brightness") {
            if (!next(brightnessArg)) return 1;
            brightnessGiven = true;
        } else if (arg == "-a" || arg == "--adjust") {
            if (!next(offsetArg)) return 1;
            offsetGiven = true;
        } else if (arg == "-i" || arg == "--interactive") {
            interactiveFlag = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: unknown option '" << arg << "'\n"
                      << "Run " << argv[0] << " --help for usage.\n";
            return 1;
        } else if (inputPath.empty()) {
            inputPath = arg;
        } else {
            std::cerr << "Error: unexpected argument '" << arg << "'\n";
            return 1;
        }
    }

    // No input file given? Ask for it (works both from a terminal and when
    // the path is piped in).
    if (inputPath.empty()) {
        std::cout << "Image Brightness Controller " << kVersion << "\n"
                  << "Supported inputs: jpeg, png, bmp, gif, tga, psd, hdr, pic, "
                     "ppm, pgm, pnm\n"
                  << "Enter image path: " << std::flush;
        if (!std::getline(std::cin, inputPath)) return 0;
        while (!inputPath.empty() &&
               (inputPath.front() == ' ' || inputPath.front() == '"')) {
            inputPath.erase(inputPath.begin());
        }
        while (!inputPath.empty() &&
               (inputPath.back() == ' ' || inputPath.back() == '"')) {
            inputPath.pop_back();
        }
        if (inputPath.empty()) {
            std::cerr << "No image path given.\n";
            return 1;
        }
    }

    if (!fileExists(inputPath)) {
        std::cerr << "Error: file not found: " << inputPath << "\n";
        return 1;
    }

    std::string error;
    Image original = Image::load(inputPath, &error);
    if (original.empty()) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }

    if (outputPath.empty()) outputPath = Image::defaultOutputPath(inputPath);

    const std::string outExt = Image::extensionOf(outputPath);
    if (!Image::isSupportedOutputExtension(outExt)) {
        std::cerr << "Error: cannot write '" << outputPath
                  << "' (unsupported output format"
                  << (outExt.empty() ? ", no file extension" : " '." + outExt + "'")
                  << ")\nSupported output formats: png, jpg, bmp, tga, ppm, pgm\n";
        return 1;
    }

    BrightnessController controller;
    if (brightnessGiven) {
        float factor = 1.f;
        if (!parseFactor(brightnessArg, factor)) {
            std::cerr << "Error: invalid brightness value '" << brightnessArg
                      << "' (expected e.g. 1.5 or 140%)\n";
            return 1;
        }
        if (!controller.setFactor(factor)) {
            std::cerr << "Note: brightness factor clamped to " << controller.factor()
                      << " (allowed range " << BrightnessController::kMinFactor << " - "
                      << BrightnessController::kMaxFactor << ")\n";
        }
    }
    if (offsetGiven) {
        int value = 0;
        if (!parseInt(offsetArg, value)) {
            std::cerr << "Error: invalid offset '" << offsetArg
                      << "' (expected an integer like -40)\n";
            return 1;
        }
        if (!controller.setOffset(value)) {
            std::cerr << "Note: offset clamped to " << controller.offset() << "\n";
        }
    }

    const bool interactive = interactiveFlag || (!brightnessGiven && !offsetGiven);
    if (interactive) {
        return runInteractive(original, controller, outputPath);
    }

    const Image adjusted = controller.apply(original);
    if (!adjusted.save(outputPath, &error)) {
        std::cerr << "Error: " << error << "\n";
        return 1;
    }
    std::cout << "Loaded  " << inputPath << ": " << original.describe() << "\n"
              << "Saved   " << outputPath << ": " << adjusted.describe() << "\n"
              << "Applied " << currentSettings(controller) << "\n";
    return 0;
}
