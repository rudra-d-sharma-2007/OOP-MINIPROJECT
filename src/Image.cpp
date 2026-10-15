#include "Image.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool isWritableFormat(const std::string& ext) {
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
           ext == "tga" || ext == "ppm" || ext == "pgm";
}

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

// ---- minimal PNM helpers ------------------------------------------------

// Reads the next whitespace/comment-delimited token from an ASCII stream.
bool nextToken(std::istream& in, std::string& token) {
    token.clear();
    int c = in.get();
    while (c != EOF) {
        if (c == '#') {  // comment until end of line
            while (c != EOF && c != '\n') c = in.get();
        } else if (std::isspace(c)) {
            c = in.get();
        } else {
            break;
        }
    }
    if (c == EOF) return false;
    while (c != EOF && !std::isspace(c) && c != '#') {
        token.push_back(static_cast<char>(c));
        c = in.get();
    }
    return true;
}

bool parseAsciiSamples(std::istream& in, unsigned char* dst, std::size_t count,
                       int maxValue) {
    std::string token;
    for (std::size_t i = 0; i < count; ++i) {
        if (!nextToken(in, token)) return false;
        long value = std::strtol(token.c_str(), nullptr, 10);
        if (value < 0) value = 0;
        if (maxValue > 0 && maxValue != 255) {
            value = value * 255 / maxValue;
        } else if (value > 255) {
            value = 255;
        }
        dst[i] = static_cast<unsigned char>(value);
    }
    return true;
}

}  // namespace

// ---- loading -----------------------------------------------------------

Image Image::load(const std::string& path, std::string* error) {
    Image img;

    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 0);
    if (data) {
        img.w_ = w;
        img.h_ = h;
        img.comp_ = comp;
        img.pixels_.assign(data, data + static_cast<std::size_t>(w) * h * comp);
        stbi_image_free(data);
        return img;
    }

    std::string reason = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
    std::string ext = extensionOf(path);
    if (ext == "ppm" || ext == "pgm" || ext == "pnm") {
        std::string fallbackError;
        if (loadPnmFallback(path, img, &fallbackError)) return img;
        reason += "; " + fallbackError;
    }

    setError(error, "Could not load '" + path + "': " + reason);
    return Image();
}

bool Image::loadPnmFallback(const std::string& path, Image& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setError(error, "file not readable");
        return false;
    }

    std::string magic;
    std::string token;
    if (!nextToken(in, magic) || magic.size() != 2 || magic[0] != 'P' ||
        (magic[1] != '2' && magic[1] != '3' && magic[1] != '5' && magic[1] != '6')) {
        setError(error, "not a valid PNM file (expected P2/P3/P5/P6)");
        return false;
    }
    if (!nextToken(in, token)) return false;
    const int w = std::atoi(token.c_str());
    if (!nextToken(in, token)) return false;
    const int h = std::atoi(token.c_str());
    if (!nextToken(in, token)) return false;
    const int maxValue = std::atoi(token.c_str());
    if (w <= 0 || h <= 0 || maxValue <= 0 || maxValue > 65535) {
        setError(error, "bad PNM header");
        return false;
    }

    const bool isColor = (magic[1] == '3' || magic[1] == '6');
    const bool isAscii = (magic[1] == '2' || magic[1] == '3');
    const int comp = isColor ? 3 : 1;
    const std::size_t count = static_cast<std::size_t>(w) * h * comp;

    Image img;
    img.w_ = w;
    img.h_ = h;
    img.comp_ = comp;
    img.pixels_.resize(count);

    if (isAscii) {
        if (!parseAsciiSamples(in, img.pixels_.data(), count, maxValue)) {
            setError(error, "truncated ASCII PNM data");
            return false;
        }
    } else {
        // Binary P5/P6: consume the single whitespace byte after the header.
        in.get();
        const int bytesPerSample = maxValue > 255 ? 2 : 1;
        std::vector<unsigned char> raw(count * bytesPerSample);
        in.read(reinterpret_cast<char*>(raw.data()),
                static_cast<std::streamsize>(raw.size()));
        if (static_cast<std::size_t>(in.gcount()) != raw.size()) {
            setError(error, "truncated binary PNM data");
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (bytesPerSample == 1) {
                img.pixels_[i] = raw[i];
            } else {  // big-endian 16 bit
                const unsigned v = (raw[i * 2] << 8) | raw[i * 2 + 1];
                img.pixels_[i] = static_cast<unsigned char>(v * 255 / maxValue);
            }
        }
    }

    out = std::move(img);
    return true;
}

// ---- saving ------------------------------------------------------------

bool Image::save(const std::string& path, std::string* error) const {
    if (empty()) {
        setError(error, "nothing to save (image is empty)");
        return false;
    }

    const std::string ext = extensionOf(path);
    const int w = w_, h = h_;

    // Formats that only accept a specific channel count get a converted copy.
    auto flatten = [&](int targetComp) -> std::vector<unsigned char> {
        std::vector<unsigned char> out(static_cast<std::size_t>(w) * h * targetComp);
        const unsigned char* src = pixels_.data();
        const std::size_t n = static_cast<std::size_t>(w) * h;
        if (targetComp == comp_) {
            std::copy(src, src + n * comp_, out.begin());
            return out;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char* p = src + i * comp_;
            if (targetComp == 1) {
                out[i] = comp_ >= 3
                             ? static_cast<unsigned char>((p[0] * 299 + p[1] * 587 +
                                                           p[2] * 114) / 1000)
                             : p[0];
            } else {  // targetComp == 3
                out[i * 3 + 0] = p[0];
                out[i * 3 + 1] = comp_ >= 3 ? p[1] : p[0];
                out[i * 3 + 2] = comp_ >= 3 ? p[2] : p[0];
            }
        }
        return out;
    };

    int ok = 0;
    if (ext == "png") {
        ok = stbi_write_png(path.c_str(), w, h, comp_, pixels_.data(), w * comp_);
    } else if (ext == "jpg" || ext == "jpeg") {
        const std::vector<unsigned char> rgb = flatten(comp_ == 1 ? 1 : 3);
        ok = stbi_write_jpg(path.c_str(), w, h, comp_ == 1 ? 1 : 3, rgb.data(), 92);
    } else if (ext == "bmp") {
        const std::vector<unsigned char> rgb = flatten(comp_ == 1 ? 1 : 3);
        ok = stbi_write_bmp(path.c_str(), w, h, comp_ == 1 ? 1 : 3, rgb.data());
    } else if (ext == "tga") {
        const std::vector<unsigned char> data =
            (comp_ == 2 || comp_ == 4) ? flatten(comp_ == 2 ? 1 : 3)
                                       : std::vector<unsigned char>(
                                             pixels_.begin(), pixels_.end());
        ok = stbi_write_tga(path.c_str(), w, h, comp_ == 2 ? 1 : (comp_ == 4 ? 3 : comp_),
                            data.data());
    } else if (ext == "ppm" || ext == "pgm") {
        const int targetComp = ext == "pgm" ? 1 : 3;
        const std::vector<unsigned char> data = flatten(targetComp);
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            setError(error, "Cannot open '" + path + "' for writing");
            return false;
        }
        out << (targetComp == 1 ? "P5\n" : "P6\n")
            << w << " " << h << "\n255\n";
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        ok = static_cast<bool>(out);
    } else {
        setError(error, "Unsupported output format '." + ext +
                            "'. Use one of: png, jpg, jpeg, bmp, tga, ppm, pgm");
        return false;
    }

    if (!ok) {
        setError(error, "Failed to write '" + path + "'");
        return false;
    }
    return true;
}

// ---- helpers -----------------------------------------------------------

std::string Image::extensionOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return "";
    }
    return lower(path.substr(dot + 1));
}

bool Image::isSupportedOutputExtension(const std::string& ext) {
    return isWritableFormat(lower(ext));
}

std::string Image::defaultOutputPath(const std::string& inputPath) {
    std::string ext = extensionOf(inputPath);
    if (ext == "jpeg") ext = "jpg";
    if (!isWritableFormat(ext)) ext = "png";
    const std::size_t slash = inputPath.find_last_of("/\\");
    const std::string dir =
        slash == std::string::npos ? "" : inputPath.substr(0, slash + 1);
    return dir + baseName(inputPath) + "_bright." + ext;
}

std::string Image::describe() const {
    if (empty()) return "empty image";
    static const char* names[] = {"unknown", "grayscale", "gray+alpha", "rgb", "rgba"};
    const char* kind = comp_ >= 1 && comp_ <= 4 ? names[comp_] : "unknown";
    std::ostringstream os;
    os << w_ << "x" << h_ << " " << kind << " (" << comp_
       << (comp_ == 1 ? " channel)" : " channels)");
    return os.str();
}
