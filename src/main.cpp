// tf2_360_qol_patches - quality-of-life patches for Xbox 360 TF2's
// Client_360.dll (see patch.hpp for the list and patch_*.cpp for what each
// one changes).
//
//   tf2_360_qol_patches <in Client_360.dll> <out Client_360.dll> [--only a,b] [--xextool <path>]
//   tf2_360_qol_patches --check <Client_360.dll>... [--only a,b] [--xextool <path>]
//
// Retail XEXs are encrypted, so xextool (xorloser) is used to decrypt the
// input and re-encrypt the output. It is looked up via --xextool, the XEXTOOL
// environment variable, then PATH.

#include "patch.hpp"
#include "xex.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string g_xextool;
std::vector<const patches::Patch*> g_selected;

bool read_file(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool write_file(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(f);
}

std::string quote(const fs::path& p) { return "\"" + p.string() + "\""; }

// Runs xextool quietly. `args` must already be quoted where needed.
bool xextool(const std::string& args) {
    std::string cmd = quote(g_xextool) + " " + args;
#ifdef _WIN32
    cmd = "\"" + cmd + " >NUL 2>&1\"";  // cmd.exe strips the outer quotes
#else
    cmd += " >/dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

struct TempFile {
    fs::path path;
    explicit TempFile(const char* tag) {
        path = fs::temp_directory_path() /
               ("tf2_360_qol_patches_" + std::to_string(std::rand()) + "_" + tag + ".xex");
    }
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

// Loads `in` and makes sure its image is directly readable, decrypting and
// decompressing through xextool when needed. Returns "" on success.
std::string load(const fs::path& in, xex::File& x, bool& was_encrypted) {
    std::vector<uint8_t> data;
    if (!read_file(in, data)) return "can't read file";
    if (auto err = x.parse(std::move(data)); !err.empty()) return err;
    was_encrypted = x.encryption == xex::kEncrypted;
    if (x.image_accessible()) return "";

    if (g_xextool.empty()) return "file is encrypted/compressed and xextool wasn't found (use --xextool)";
    TempFile tmp("dec");
    if (!xextool("-e u -c u -o " + quote(tmp.path) + " " + quote(in)))
        return "xextool failed to decrypt";
    if (!read_file(tmp.path, data)) return "can't read xextool output";
    if (auto err = x.parse(std::move(data)); !err.empty()) return "after xextool: " + err;
    if (!x.image_accessible()) return "xextool output is still not a plain image";
    return "";
}

int check(const std::vector<fs::path>& files) {
    int worst = 0;
    for (const auto& f : files) {
        xex::File x;
        bool enc = false;
        std::string status;
        if (auto err = load(f, x, enc); !err.empty()) {
            status = "error: " + err;
            worst = 2;
        } else {
            auto image = x.read_image();  // a copy, never written
            for (const auto* p : g_selected) {
                auto r = p->apply(image, x.load_address);
                status += std::string(status.empty() ? "" : ", ") + p->name + " " +
                          (r.status == patches::Status::Patched          ? "patchable"
                           : r.status == patches::Status::AlreadyPatched ? "done"
                                                                         : "n/a");
            }
        }
        std::printf("%-40s %s\n", status.c_str(), f.string().c_str());
    }
    return worst;
}

int patch(const fs::path& in, const fs::path& out) {
    xex::File x;
    bool was_encrypted = false;
    if (auto err = load(in, x, was_encrypted); !err.empty()) {
        std::fprintf(stderr, "%s: %s\n", in.string().c_str(), err.c_str());
        return 2;
    }

    auto image = x.read_image();
    int applied = 0, done = 0;
    for (const auto* p : g_selected) {
        auto r = p->apply(image, x.load_address);
        const char* what = r.status == patches::Status::Patched          ? "applied"
                           : r.status == patches::Status::AlreadyPatched ? "already applied"
                                                                         : "not found, skipped";
        std::printf("[%s] %s\n%s", p->name, what, r.log.c_str());
        applied += r.status == patches::Status::Patched;
        done += r.status == patches::Status::AlreadyPatched;
    }
    if (!applied) {
        if (done) return 0;
        std::fprintf(stderr, "not a supported TF Client_360.dll, nothing written\n");
        return 1;
    }

    x.write_image(image);
    if (x.read_image() != image) {  // a patch landed in a zero-fill gap
        std::fprintf(stderr, "internal error: patched bytes aren't backed by file data\n");
        return 2;
    }

    if (!was_encrypted) {
        if (!write_file(out, x.bytes)) return std::fprintf(stderr, "can't write %s\n", out.string().c_str()), 2;
    } else {
        TempFile tmp("pat");
        if (!write_file(tmp.path, x.bytes)) return std::fprintf(stderr, "can't write temp file\n"), 2;
        if (!xextool("-e e -o " + quote(out) + " " + quote(tmp.path)))
            return std::fprintf(stderr, "xextool failed to re-encrypt\n"), 2;
    }
    std::printf("wrote %s\n", out.string().c_str());
    return 0;
}

std::string find_xextool(std::string given) {
    if (!given.empty()) return given;
    if (const char* env = std::getenv("XEXTOOL")) return env;
#ifdef _WIN32
    const char* exe = "xextool.exe";
    const char sep = ';';
#else
    const char* exe = "xextool";
    const char sep = ':';
#endif
    if (const char* path = std::getenv("PATH")) {
        std::string p = path;
        for (size_t s = 0, e; s <= p.size(); s = e + 1) {
            e = p.find(sep, s);
            if (e == std::string::npos) e = p.size();
            fs::path cand = fs::path(p.substr(s, e - s)) / exe;
            std::error_code ec;
            if (e > s && fs::exists(cand, ec)) return cand.string();
        }
    }
    return "";
}

int usage() {
    std::fputs("usage: tf2_360_qol_patches <in Client_360.dll> <out Client_360.dll> [--only a,b] [--xextool <path>]\n"
               "       tf2_360_qol_patches --check <Client_360.dll>... [--only a,b] [--xextool <path>]\n"
               "patches (all by default):\n", stderr);
    for (const auto& p : patches::all()) std::fprintf(stderr, "  %-12s %s\n", p.name, p.summary);
    return 2;
}

// Fills g_selected from a comma separated list, or every patch if empty.
bool select_patches(const std::string& list) {
    for (const auto& p : patches::all())
        if (list.empty() || ("," + list + ",").find("," + std::string(p.name) + ",") != std::string::npos)
            g_selected.push_back(&p);
    return !g_selected.empty();
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> files;
    std::string xt, only;
    bool check_mode = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--check") check_mode = true;
        else if (a == "--xextool" && i + 1 < argc) xt = argv[++i];
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a.rfind("--", 0) == 0) return usage();
        else files.emplace_back(a);
    }
    g_xextool = find_xextool(xt);
    if (!select_patches(only)) return usage();

    if (check_mode) return files.empty() ? usage() : check(files);
    if (files.size() != 2) return usage();
    return patch(files[0], files[1]);
}
