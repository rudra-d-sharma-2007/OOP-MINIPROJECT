#pragma once

#include <string>
#include <vector>

// A simple 8-bit raster image (grayscale, gray+alpha, RGB or RGBA).
// Loading uses stb_image (jpeg, png, bmp, gif, tga, psd, hdr, pic, ppm/pgm)
// with a small built-in fallback parser for plain-text (ASCII) PNM files.
class Image {
public:
    Image() = default;

    // Loads an image from disk. Returns an empty image on failure and
    // writes a human-readable message into *error (if provided).
    static Image load(const std::string& path, std::string* error = nullptr);

    // Loads an image from a memory buffer (e.g. an HTTP upload). The format
    // is detected from the file's magic bytes.
    static Image loadFromMemory(const unsigned char* data, std::size_t size,
                                std::string* error = nullptr);

    // Saves the image. The format is chosen from the file extension:
    // png, jpg/jpeg, bmp, tga, ppm, pgm. Returns false on failure.
    bool save(const std::string& path, std::string* error = nullptr) const;

    // Encodes the image into a memory buffer instead of a file.
    // ext must be one of: png, jpg, bmp, tga, ppm, pgm.
    bool saveToMemory(const std::string& ext, std::vector<unsigned char>& out,
                      std::string* error = nullptr) const;

    bool empty() const { return w_ <= 0 || h_ <= 0 || pixels_.empty(); }
    int width() const { return w_; }
    int height() const { return h_; }
    int channels() const { return comp_; }
    std::size_t sizeBytes() const { return pixels_.size(); }

    const unsigned char* pixels() const { return pixels_.data(); }
    unsigned char* pixels() { return pixels_.data(); }

    std::string describe() const;

    // File extension in lower case without the dot ("" if none).
    static std::string extensionOf(const std::string& path);
    // True for formats save()/saveToMemory() can write:
    // png, jpg/jpeg, bmp, tga, ppm, pgm.
    static bool isSupportedOutputExtension(const std::string& ext);
    // Default output path: "<name>_bright.<ext>", falling back to png for
    // formats we cannot write (gif, psd, pic, ...).
    static std::string defaultOutputPath(const std::string& inputPath);

private:
    int w_ = 0;
    int h_ = 0;
    int comp_ = 0;  // 1 = gray, 2 = gray+alpha, 3 = rgb, 4 = rgba
    std::vector<unsigned char> pixels_;

    // Returns a copy of the pixels with exactly targetComp channels
    // (1 = luminance, 3 = rgb) for formats that cannot store alpha.
    std::vector<unsigned char> flattened(int targetComp) const;
};
