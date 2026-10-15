# OOP-MINIPROJECT

## Image Brightness Controller (C++)

A small, dependency-free C++ command-line tool that loads an image
(jpeg / png / bmp / gif / tga / ppm / pgm / psd / hdr / pic), controls its
brightness, and saves the result in the format you want.

```
out = clamp(in × factor + offset, 0 … 255)
```

* **factor** — `1.0` = unchanged, `0.0` = pure black, `2.0` = twice as bright.
  Range `0.0 – 3.0` (percentages like `140%` also work).
* **offset** — additive brightness, range `-255 … 255`.
* The alpha/transparency channel is never touched.

### Features

* Reads **jpeg, jpg, png, bmp, gif, tga, psd, hdr, pic** and **ppm / pgm / pnm**
  (binary *and* plain-text P3/P2, comments included).
* Writes **png, jpg, bmp, tga, ppm, pgm** (picked from the output file extension).
* **One-shot mode** for scripts and an **interactive mode** that re-saves the
  result after every change so you can preview it in any image viewer.
* A **web UI + HTTP API** (`bin/brightness_server`) with upload, brightness
  sliders and download — deployable to any container/host that runs a command.
* No external libraries to install — decoding/encoding uses the bundled
  public-domain [stb](https://github.com/nothings/stb) headers and the bundled
  [cpp-httplib](https://github.com/yhirose/cpp-httplib) header in `third_party/`.

### Build

Requirements: `g++` (C++17) and `make`. `python3` is only needed for `make sample`.

```sh
make            # builds bin/brightness_controller (CLI) + bin/brightness_server (web)
make run        # start the interactive CLI
make serve      # start the web UI on $PORT (default 10000)
make sample     # regenerates the demo images in samples/
make clean
```

### Usage

```sh
# brighten a photo by 50% and save as png
./bin/brightness_controller photo.jpg -b 1.5 -o brighter.png

# percentage syntax works too, and ppm input is fine
./bin/brightness_controller scan.ppm -b 120% -o dimmed.png

# additive adjustment instead of a factor
./bin/brightness_controller dark.png -a 40 -o lighter.png
./bin/brightness_controller too_bright.jpg -a -60 -o darker.jpg

# interactive mode (also the default when no -b/-a is given)
./bin/brightness_controller photo.png
```

If you run it without arguments it asks for the image path, so nothing needs
to be typed on the command line. If `-o` is omitted the result is written next
to the input as `<name>_bright.<ext>` (falling back to `.png` for formats that
cannot be written).

#### Options

| Option | Meaning |
| --- | --- |
| `-o, --output <file>` | Output file path (format comes from the extension) |
| `-b, --brightness <val>` | Brightness factor `0.0 – 3.0`, or a percentage like `140%` |
| `-a, --adjust <n>` | Brightness offset `-255 … 255` |
| `-i, --interactive` | Force interactive mode |
| `-h, --help`, `-v, --version` | Help / version |

#### Interactive commands

```
brightness> 1.4        set factor (a number or a percentage like 140%)
brightness> o -30      set offset (-255 … 255)
brightness> s          save with the current settings
brightness> r          reset to the original image
brightness> i          show image info
brightness> h          help
brightness> q          quit
```

The result is saved to the output file after every change — open it in an
image viewer next to the terminal to see the effect live.

### Try it right away

```sh
make sample
./bin/brightness_controller samples/gradient.ppm -b 1.5 -o out.png
./bin/brightness_controller samples/gradient.jpg       # interactive
./bin/brightness_controller samples/checker_ascii.ppm  # plain-text PPM
```

### Web UI & API

```sh
make serve                     # or: ./bin/brightness_server 9090
```

The server binds `0.0.0.0` on the port from the first argument, or the `PORT`
environment variable (Render and Freebuff inject one), or `10000`.

| Route | Description |
| --- | --- |
| `GET /` | Browser UI: upload an image, drag the brightness sliders, download the result |
| `POST /process` | `multipart/form-data`: `file` (required), `factor` (0–3), `offset` (−255…255), `format` (png/jpg/bmp/tga/ppm/pgm) → responds with the processed image |
| `GET /healthz` | Liveness probe (`ok`) |

```sh
# API example
 curl -F file=@photo.jpg -F factor=1.5 -F offset=-20 -F format=png \
      http://localhost:10000/process -o brighter.png
```

### Deploying to Render

Because the project ships a web server, it runs on Render (or any similar
host) as a plain web service:

| Render field | Value |
| --- | --- |
| **Environment** | Docker or Native (C++17 toolchain) |
| **Build Command** | `make` |
| **Start Command** | `./bin/brightness_server` |

The server reads Render's injected `$PORT` and binds `0.0.0.0`, which is all
Render needs. The root page doubles as Render's health-check target.

### Project layout

```
src/main.cpp                    CLI, argument parsing, interactive loop
src/server.cpp                  HTTP server: web UI + /process API
src/Image.h/.cpp                Image class: load/save (disk + memory) via stb, PNM fallback
src/BrightnessController.h/.cpp BrightnessController class: factor/offset + apply()
third_party/                    stb_image.h, stb_image_write.h, httplib.h
samples/                        ready-made test images (ppm, png, jpg, ascii ppm)
tools/make_samples.py           regenerates the samples (python stdlib only)
```

The two classes keep it object-oriented: `Image` owns the pixel buffer and all
format I/O, while `BrightnessController` stores the current factor/offset,
clamps them to valid ranges, and produces the adjusted copy of an `Image`
using a 256-entry lookup table.
