// Fast365 — fast, dependency-free DOCX to HTML converter for Windows.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "docx.h"

namespace {

constexpr const char* kVersion = "0.2.0";

void printUsage() {
    std::fprintf(stderr,
        "Fast365 v%s - fast DOCX to HTML converter (no dependencies)\n"
        "\n"
        "Usage: fast365 <input.docx> [options]\n"
        "\n"
        "Options:\n"
        "  -o <file>      output path (default: input name with .html;\n"
        "                 use \"-\" for stdout)\n"
        "  --fragment     emit body content only, without the <html> wrapper\n"
        "  --no-images    do not embed images\n"
        "  --title <t>    override the document title\n"
        "  --quiet        suppress the timing summary\n"
        "  --version      print version and exit\n",
        kVersion);
}

bool readFile(const char* path, std::vector<uint8_t>& buf) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    if (size < 0) {
        std::fclose(f);
        return false;
    }
    std::fseek(f, 0, SEEK_SET);
    buf.resize(static_cast<size_t>(size));
    size_t got = size ? std::fread(buf.data(), 1, buf.size(), f) : 0;
    std::fclose(f);
    return got == buf.size();
}

bool writeFile(const char* path, const std::string& data) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    size_t put = std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return put == data.size();
}

std::string defaultOutputPath(const std::string& input) {
    size_t dot = input.rfind('.');
    size_t sep = input.find_last_of("/\\");
    if (dot != std::string::npos &&
        (sep == std::string::npos || dot > sep))
        return input.substr(0, dot) + ".html";
    return input + ".html";
}

std::string baseName(const std::string& path) {
    size_t sep = path.find_last_of("/\\");
    std::string name =
        (sep == std::string::npos) ? path : path.substr(sep + 1);
    size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name.resize(dot);
    return name;
}

} // namespace

int main(int argc, char** argv) {
    std::string input, output;
    ConvertOptions opts;
    bool quiet = false;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0) {
            std::printf("fast365 %s\n", kVersion);
            return 0;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            printUsage();
            return 0;
        } else if (std::strcmp(a, "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strcmp(a, "--title") == 0 && i + 1 < argc) {
            opts.title = argv[++i];
        } else if (std::strcmp(a, "--fragment") == 0) {
            opts.fragment = true;
        } else if (std::strcmp(a, "--no-images") == 0) {
            opts.embedImages = false;
        } else if (std::strcmp(a, "--quiet") == 0 || std::strcmp(a, "-q") == 0) {
            quiet = true;
        } else if (a[0] == '-' && std::strcmp(a, "-") != 0) {
            std::fprintf(stderr, "fast365: unknown option '%s'\n\n", a);
            printUsage();
            return 2;
        } else if (input.empty()) {
            input = a;
        } else {
            std::fprintf(stderr, "fast365: unexpected argument '%s'\n", a);
            return 2;
        }
    }

    if (input.empty()) {
        printUsage();
        return 2;
    }
    if (output.empty()) output = defaultOutputPath(input);
    if (opts.title.empty()) opts.title = baseName(input);

    auto t0 = std::chrono::steady_clock::now();

    std::vector<uint8_t> docx;
    if (!readFile(input.c_str(), docx)) {
        std::fprintf(stderr, "fast365: cannot read '%s'\n", input.c_str());
        return 1;
    }

    std::string html, error;
    bool ok = false;
    try {
        ok = convertDocxToHtml(docx.data(), docx.size(), opts, html, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory (file too large or corrupt)";
    } catch (const std::exception& e) {
        error = std::string("internal error: ") + e.what();
    }
    if (!ok) {
        std::fprintf(stderr, "fast365: %s: %s\n", input.c_str(), error.c_str());
        return 1;
    }

    if (output == "-") {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::fwrite(html.data(), 1, html.size(), stdout);
    } else if (!writeFile(output.c_str(), html)) {
        std::fprintf(stderr, "fast365: cannot write '%s'\n", output.c_str());
        return 1;
    }

    if (!quiet) {
        auto t1 = std::chrono::steady_clock::now();
        double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::fprintf(stderr, "fast365: %s (%zu KB) -> %s (%zu KB) in %.1f ms\n",
                     input.c_str(), docx.size() / 1024, output.c_str(),
                     html.size() / 1024, ms);
    }
    return 0;
}
