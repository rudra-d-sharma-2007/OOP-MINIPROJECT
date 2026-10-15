// HTTP wrapper for the Image Brightness Controller, so the project can run
// as a web service (Render, Freebuff preview, ...).
//
//   GET  /          browser UI (upload + brightness sliders)
//   POST /process   multipart form: file, factor, offset, format
//                   -> responds with the processed image bytes
//   GET  /healthz   liveness probe
//
// The server binds 0.0.0.0 on the port given as the first argument, or the
// PORT environment variable (Render and Freebuff inject it), or 10000.

#include "BrightnessController.h"
#include "Image.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "httplib.h"

namespace {

const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Image Brightness Controller</title>
<style>
  :root {
    --bg: #12100c; --panel: #1d1a14; --panel-2: #262218;
    --text: #f3ecdd; --muted: #a2967e; --accent: #f5a524; --accent-2: #ffcf6b;
    --err-bg: #3a1d1d; --err-text: #ffb4a8; --ok: #7dd3a8;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; min-height: 100vh; color: var(--text);
    background:
      radial-gradient(1200px 600px at 80% -10%, rgba(245,165,36,.18), transparent 60%),
      radial-gradient(900px 500px at -10% 110%, rgba(245,165,36,.10), transparent 60%),
      var(--bg);
    font: 15px/1.5 ui-sans-serif, system-ui, "Segoe UI", Roboto, sans-serif;
    display: flex; justify-content: center; padding: 40px 16px;
  }
  main { width: 100%; max-width: 760px; }
  h1 { font-size: 26px; margin: 0 0 4px; letter-spacing: .3px; }
  h1 .sun { color: var(--accent); }
  .sub { color: var(--muted); margin: 0 0 20px; }
  .card {
    background: var(--panel); border: 1px solid rgba(245,165,36,.18);
    border-radius: 14px; padding: 20px; margin-bottom: 18px;
  }
  label { display: block; font-size: 13px; color: var(--muted); margin-bottom: 6px; }
  input[type=file] { color: var(--muted); width: 100%; margin-bottom: 16px; }
  .row { display: flex; gap: 14px; flex-wrap: wrap; }
  .row > div { flex: 1 1 200px; }
  .val { color: var(--accent-2); font-variant-numeric: tabular-nums; }
  input[type=range] { width: 100%; accent-color: var(--accent); }
  select {
    width: 100%; padding: 8px 10px; border-radius: 8px; border: 1px solid #3d372a;
    background: var(--panel-2); color: var(--text);
  }
  button {
    margin-top: 18px; width: 100%; padding: 12px; font-size: 15px; font-weight: 600;
    color: #211a08; background: linear-gradient(180deg, var(--accent-2), var(--accent));
    border: 0; border-radius: 10px; cursor: pointer;
  }
  button:hover { filter: brightness(1.07); }
  button:disabled { opacity: .55; cursor: wait; }
  #error {
    display: none; background: var(--err-bg); color: var(--err-text);
    border: 1px solid rgba(255,120,100,.35); border-radius: 10px;
    padding: 10px 14px; margin-bottom: 18px; white-space: pre-wrap;
  }
  #result { display: none; text-align: center; }
  #result img {
    max-width: 100%; max-height: 420px; border-radius: 10px;
    background: repeating-conic-gradient(#2a2620 0 25%, #201d17 0 50%) 0 0/20px 20px;
    border: 1px solid #3d372a;
  }
  .meta { color: var(--muted); font-size: 13px; margin-top: 10px; }
  .dl {
    display: inline-block; margin-top: 12px; padding: 9px 18px; border-radius: 9px;
    background: var(--panel-2); border: 1px solid var(--accent); color: var(--accent-2);
    text-decoration: none; font-weight: 600;
  }
  .dl:hover { background: rgba(245,165,36,.12); }
  .hints { color: var(--muted); font-size: 13px; margin-top: 26px; }
  .hints code { background: var(--panel-2); padding: 1px 6px; border-radius: 6px; }
</style>
</head>
<body>
<main>
  <h1><span class="sun">&#9728;</span> Image Brightness Controller</h1>
  <p class="sub">Upload a jpeg, png, bmp, tga or ppm image, adjust its brightness and download the result.</p>

  <div class="card">
    <div id="error"></div>
    <form id="form">
      <label for="file">Image file</label>
      <input id="file" name="file" type="file" required
             accept=".jpg,.jpeg,.png,.bmp,.tga,.ppm,.pgm,.pnm,.gif,.psd,.hdr,.pic,image/*">

      <div class="row">
        <div>
          <label for="factor">Brightness factor <span class="val" id="factorVal">1.00</span></label>
          <input id="factor" name="factor" type="range" min="0" max="3" step="0.05" value="1">
        </div>
        <div>
          <label for="offset">Offset <span class="val" id="offsetVal">0</span></label>
          <input id="offset" name="offset" type="range" min="-255" max="255" step="1" value="0">
        </div>
        <div>
          <label for="format">Output format</label>
          <select id="format" name="format">
            <option value="png">png</option>
            <option value="jpg">jpg</option>
            <option value="bmp">bmp</option>
            <option value="tga">tga</option>
            <option value="ppm">ppm</option>
            <option value="pgm">pgm</option>
          </select>
        </div>
      </div>

      <button id="go" type="submit">Adjust brightness</button>
    </form>
  </div>

  <div class="card" id="result">
    <img id="preview" alt="processed image">
    <div class="meta" id="meta"></div>
    <a class="dl" id="download" download href="#">Download result</a>
  </div>

  <p class="hints">
    API: <code>curl -F file=@photo.jpg -F factor=1.5 -F offset=-20 -F format=png
    /process</code> &middot; factor 1.0 = unchanged, 0.0 = black, 3.0 = very bright;
    offset ranges from -255 (black) to +255 (white).
  </p>
</main>

<script>
  const $ = (id) => document.getElementById(id);
  $('factor').addEventListener('input', (e) => { $('factorVal').textContent = Number(e.target.value).toFixed(2); });
  $('offset').addEventListener('input', (e) => { $('offsetVal').textContent = e.target.value; });

  $('form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const file = $('file').files[0];
    if (!file) return;
    const errBox = $('error');
    errBox.style.display = 'none';
    $('go').disabled = true;
    try {
      const fd = new FormData();
      fd.append('file', file, file.name);
      fd.append('factor', $('factor').value);
      fd.append('offset', $('offset').value);
      fd.append('format', $('format').value);
      const res = await fetch('/process', { method: 'POST', body: fd });
      if (!res.ok) throw new Error((await res.text()) || ('HTTP ' + res.status));
      const blob = await res.blob();
      const url = URL.createObjectURL(blob);
      $('preview').src = url;
      $('download').href = url;
      $('download').download = file.name.replace(/\.[^.]+$/, '') + '_bright.' + $('format').value;
      $('meta').textContent = file.name + ' \u2192 ' + $('download').download +
        ' \u00b7 ' + (blob.size / 1024).toFixed(1) + ' KB';
      $('result').style.display = 'block';
    } catch (err) {
      errBox.textContent = err.message || String(err);
      errBox.style.display = 'block';
      $('result').style.display = 'none';
    } finally {
      $('go').disabled = false;
    }
  });
</script>
</body>
</html>
)HTML";

std::string contentTypeFor(const std::string& ext) {
    if (ext == "png") return "image/png";
    if (ext == "jpg") return "image/jpeg";
    if (ext == "bmp") return "image/bmp";
    if (ext == "tga") return "image/x-tga";
    if (ext == "ppm") return "image/x-portable-pixmap";
    if (ext == "pgm") return "image/x-portable-graymap";
    return "application/octet-stream";
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Reads a form field (multipart) with a query-string fallback.
std::string fieldOr(const httplib::Request& req, const char* name,
                    const char* fallback) {
    if (req.form.has_field(name)) return req.form.get_field(name);
    if (req.has_param(name)) return req.get_param_value(name);
    return fallback;
}

bool parseFloat(const std::string& text, float& out) {
    try {
        std::size_t used = 0;
        const float v = std::stof(text, &used);
        if (used != text.size()) return false;
        out = v;
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

void replyText(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "text/plain; charset=utf-8");
}

void handleProcess(const httplib::Request& req, httplib::Response& res) {
    if (!req.form.has_file("file")) {
        replyText(res, 400,
                  "Missing image field 'file'. Send multipart/form-data with fields: "
                  "file (required), factor, offset, format.");
        return;
    }
    const httplib::FormData upload = req.form.get_file("file");
    if (upload.content.empty()) {
        replyText(res, 400, "The uploaded file is empty.");
        return;
    }

    BrightnessController controller;
    const std::string factorText = fieldOr(req, "factor", "1.0");
    float factor = 1.0f;
    if (!parseFloat(factorText, factor) || !controller.setFactor(factor)) {
        replyText(res, 400,
                  "Invalid factor '" + factorText +
                      "'. Expected a number between 0.0 and 3.0 (or a percentage "
                      "like 150%).");
        return;
    }
    const std::string offsetText = fieldOr(req, "offset", "0");
    int offset = 0;
    if (!parseInt(offsetText, offset) || !controller.setOffset(offset)) {
        replyText(res, 400,
                  "Invalid offset '" + offsetText + "'. Expected an integer between -255 and 255.");
        return;
    }

    std::string format = toLower(fieldOr(req, "format", "png"));
    if (!format.empty() && format.front() == '.') format.erase(format.begin());
    if (format.empty()) format = "png";
    if (!Image::isSupportedOutputExtension(format)) {
        replyText(res, 400,
                  "Unsupported format '" + format +
                      "'. Use one of: png, jpg, bmp, tga, ppm, pgm.");
        return;
    }

    std::string error;
    Image source = Image::loadFromMemory(
        reinterpret_cast<const unsigned char*>(upload.content.data()),
        upload.content.size(), &error);
    if (source.empty()) {
        replyText(res, 400, "Could not read the uploaded image: " + error);
        return;
    }

    const Image adjusted = controller.apply(source);
    std::vector<unsigned char> bytes;
    if (!adjusted.saveToMemory(format, bytes, &error)) {
        replyText(res, 500, "Failed to encode the result: " + error);
        return;
    }

    res.set_content(reinterpret_cast<const char*>(bytes.data()), bytes.size(),
                    contentTypeFor(format).c_str());
}

int resolvePort(int argc, char** argv) {
    auto parse = [](const char* s) -> int {
        if (!s || !*s) return 0;
        char* end = nullptr;
        const long v = std::strtol(s, &end, 10);
        if (end == s || *end != '\0' || v <= 0 || v > 65535) return 0;
        return static_cast<int>(v);
    };
    if (argc > 1) {
        const int p = parse(argv[1]);
        if (p) return p;
    }
    if (const char* env = std::getenv("PORT")) {
        const int p = parse(env);
        if (p) return p;
    }
    return 10000;
}

}  // namespace

int main(int argc, char** argv) {
    const int port = resolvePort(argc, argv);

    httplib::Server svr;
    svr.set_payload_max_length(64 * 1024 * 1024);  // 64 MB uploads

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kIndexHtml, "text/html; charset=utf-8");
    });
    svr.Get("/healthz", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ok", "text/plain; charset=utf-8");
    });
    svr.Post("/process", handleProcess);

    svr.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404) {
            res.set_content("Not found. Try GET / for the UI.\n",
                            "text/plain; charset=utf-8");
        }
    });

    std::cout << "Image Brightness Controller web UI on http://0.0.0.0:" << port
              << " (open / for the UI, POST /process to adjust images)\n";
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Error: could not bind to 0.0.0.0:" << port << "\n";
        return 1;
    }
    return 0;
}
