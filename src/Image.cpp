#include "Image.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

// ---- PNM parsing over a memory buffer ----------------------------------

struct ByteCursor {
    const unsigned char* p;
    std::size_t n;
    std::size_t i = 0;
    bool atEnd() const { return i >= n; }
};

// Reads the next whitespace/comment-delimited token.
bool nextToken(ByteCursor& c, std::string& token) {
    while (!c.atEnd()) {
        const unsigned char ch = c.p[c.i];
        if (ch == '#') {  // comment until end of line
            while (!c.atEnd() && c.p[c.i] != '\n') ++c.i;
        } else if (std::isspace(ch)) {
            ++c.i;
        } else {
            break;
        }
    }
    token.clear();
    while (!c.atEnd()) {
        const unsigned char ch = c.p[c.i];
        if (std::isspace(ch) || ch == '#') break;
        token.push_back(static_cast<char>(ch));
        ++c.i;
    }
    return !token.empty();
}

bool parseAsciiSamples(ByteCursor& c, unsigned char* dst, std::size_t count,
                       int maxValue) {
    std::string token;
    for (std::size_t i = 0; i < count; ++i) {
        if (!nextToken(c, token)) return false;
        long value = std::strtol(token.c_str(), nullptr, 10);
        if (value < 0) value = 0;
        if (maxValue != 255) {
            value = value * 255 / maxValue;
        }
        if (value > 255) value = 255;
        dst[i] = static_cast<unsigned char>(value);
    }
    return true;
}

bool looksLikePnm(const unsigned char* data, std::size_t size) {
    // P2/P3 = ASCII gray/colour, P5/P6 = binary gray/colour.
    return size >= 2 && data[0] == 'P' && data[1] >= '2' && data[1] <= '6' &&
           data[1] != '4' && (size == 2 || std::isspace(data[2]));
}

bool parsePnm(const unsigned char* data, std::size_t size, int& w, int& h,
              int& comp, std::vector<unsigned char>& pixels) {
    ByteCursor c{data, size};
    std::string token, magic;
    if (!nextToken(c, magic) || magic.size() != 2 || magic[0] != 'P') return false;
    const char kind = magic[1];
    if (kind != '2' && kind != '3' && kind != '5' && kind != '6') return false;

    if (!nextToken(c, token)) return false;
    w = std::atoi(token.c_str());
    if (!nextToken(c, token)) return false;
    h = std::atoi(token.c_str());
    if (!nextToken(c, token)) return false;
    const int maxValue = std::atoi(token.c_str());
    if (w <= 0 || h <= 0 || maxValue <= 0 || maxValue > 65535) return false;

    const bool isColor = (kind == '3' || kind == '6');
    const bool isAscii = (kind == '2' || kind == '3');
    comp = isColor ? 3 : 1;
    const std::size_t count = static_cast<std::size_t>(w) * h * comp;
    pixels.resize(count);

    if (isAscii) {
        return parseAsciiSamples(c, pixels.data(), count, maxValue);
    }

    // Binary P5/P6: exactly one whitespace byte separates header and data.
    if (c.atEnd() || !std::isspace(c.p[c.i])) return false;
    ++c.i;
    const int bytesPerSample = maxValue > 255 ? 2 : 1;
    const std::size_t needed = count * bytesPerSample;
    if (size - c.i < needed) return false;  // truncated
    const unsigned char* raw = data + c.i;
    for (std::size_t i = 0; i < count; ++i) {
        if (bytesPerSample == 1) {
            pixels[i] = raw[i];
        } else {  // big-endian 16 bit
            const unsigned v = (raw[i * 2] << 8) | raw[i * 2 + 1];
            pixels[i] = static_cast<unsigned char>(v * 255 / maxValue);
        }
    }
    return true;
}

// stb_image_write callback that appends to a vector.
void appendBytes(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<unsigned char>*>(context);
    const auto* p = static_cast<const unsigned char*>(data);
    out->insert(out->end(), p, p + size);
}

}  // namespace

// ---- loading -----------------------------------------------------------

Image Image::load(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        setError(error, "Could not load '" + path + "': file not readable");
        return Image();
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    Image img = loadFromMemory(bytes.data(), bytes.size(), error);
    if (!img.empty() && error) error->clear();
    if (!img.empty()) return img;

    // Re-tag the error with the file name for a friendlier message.
    if (error && !error->empty()) {
        const std::size_t colon = error->find(": ");
        *error = "Could not load '" + path + "'" +
                 (colon == std::string::npos ? ": " + *error : error->substr(colon));
    }
    return Image();
}

Image Image::loadFromMemory(const unsigned char* data, std::size_t size,
                            std::string* error) {
    Image img;
    if (!data || size == 0) {
        setError(error, "empty image data");
        return img;
    }

    int w = 0, h = 0, comp = 0;
    unsigned char* raw =
        stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 0);
    if (raw) {
        img.w_ = w;
        img.h_ = h;
        img.comp_ = comp;
        img.pixels_.assign(raw, raw + static_cast<std::size_t>(w) * h * comp);
        stbi_image_free(raw);
        return img;
    }

    std::string reason = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
    if (looksLikePnm(data, size)) {
        int pw = 0, ph = 0, pcomp = 0;
        std::vector<unsigned char> ppixels;
        if (parsePnm(data, size, pw, ph, pcomp, ppixels)) {
            img.w_ = pw;
            img.h_ = ph;
            img.comp_ = pcomp;
            img.pixels_ = std::move(ppixels);
            return img;
        }
        reason += "; PNM data is malformed";
    }

    setError(error, "unsupported or corrupt image data: " + reason);
    return img;
}

// ---- pixel conversion --------------------------------------------------

std::vector<unsigned char> Image::flattened(int targetComp) const {
    const std::size_t n = static_cast<std::size_t>(w_) * h_;
    if (targetComp == comp_) {
        return pixels_;
    }
    std::vector<unsigned char> out(n * targetComp);
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char* p = pixels_.data() + i * comp_;
        if (targetComp == 1) {
            out[i] = comp_ >= 3 ? static_cast<unsigned char>(
                                      (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000)
                                : p[0];
        } else {  // targetComp == 3
            out[i * 3 + 0] = p[0];
            out[i * 3 + 1] = comp_ >= 3 ? p[1] : p[0];
            out[i * 3 + 2] = comp_ >= 3 ? p[2] : p[0];
        }
    }
    return out;
}

// ---- saving ------------------------------------------------------------

bool Image::saveToMemory(const std::string& extIn, std::vector<unsigned char>& out,
                         std::string* error) const {
    if (empty()) {
        setError(error, "nothing to save (image is empty)");
        return false;
    }
    std::string ext = extIn == "jpeg" ? "jpg" : lower(extIn);
    out.clear();

    const int w = w_, h = h_;
    int ok = 0;
    if (ext == "png") {
        ok = stbi_write_png_to_func(appendBytes, &out, w, h, comp_, pixels_.data(),
                                    w * comp_);
    } else if (ext == "jpg") {
        const std::vector<unsigned char> data = flattened(comp_ == 1 ? 1 : 3);
        ok = stbi_write_jpg_to_func(appendBytes, &out, w, h, comp_ == 1 ? 1 : 3,
                                    data.data(), 92);
    } else if (ext == "bmp") {
        const std::vector<unsigned char> data = flattened(comp_ == 1 ? 1 : 3);
        ok = stbi_write_bmp_to_func(appendBytes, &out, w, h, comp_ == 1 ? 1 : 3,
                                    data.data());
    } else if (ext == "tga") {
        const int target = (comp_ == 2 || comp_ == 4) ? (comp_ == 2 ? 1 : 3) : comp_;
        const std::vector<unsigned char> data = flattened(target);
        ok = stbi_write_tga_to_func(appendBytes, &out, w, h, target, data.data());
    } else if (ext == "ppm" || ext == "pgm") {
        const int target = ext == "pgm" ? 1 : 3;
        const std::vector<unsigned char> data = flattened(target);
        std::ostringstream header;
        header << (target == 1 ? "P5\n" : "P6\n") << w << " " << h << "\n255\n";
        const std::string headerStr = header.str();
        out.insert(out.end(), headerStr.begin(), headerStr.end());
        out.insert(out.end(), data.begin(), data.end());
        ok = 1;
    } else {
        setError(error, "Unsupported output format '." + ext +
                            "'. Use one of: png, jpg, jpeg, bmp, tga, ppm, pgm");
        return false;
    }

    if (!ok || out.empty()) {
        setError(error, "Failed to encode image as '." + ext + "'");
        out.clear();
        return false;
    }
    return true;
}

bool Image::save(const std::string& path, std::string* error) const {
    if (empty()) {
        setError(error, "nothing to save (image is empty)");
        return false;
    }
    std::vector<unsigned char> bytes;
    if (!saveToMemory(extensionOf(path), bytes, error)) return false;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        setError(error, "Cannot open '" + path + "' for writing");
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
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
