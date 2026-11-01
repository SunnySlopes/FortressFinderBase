/*
 * FortressFinderBase standalone command-line program.
 *
 * The executable links the same static engine and nested cubiomes checkout as
 * the JNI library, so it is self-contained and needs no Java or Minecraft
 * runtime.
 *
 * Contract:
 *   - parameters are --long-options
 *   - the text report is printed to stdout
 *   - --out <file> additionally writes the same report to a UTF-8 txt file
 */

#include "fortress_finder.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr const char *kCliVersion = "1.0.0";
constexpr const char *kEngineVersion = "1.0.0";
constexpr int kMaxThreads = 256;
constexpr int kMaxCoordinate = 30000000;
constexpr int64_t kMaxSearchArea = 30000000LL * 30000000LL;

struct Options {
    long long seed = 0;
    bool seedSet = false;

    int mode = 0; // 0 = cross, 1 = span
    int mc = 35;  // cubiomes MC_26_3: the 1.18~26.3+ generation group

    int centerX = 0;
    int centerZ = 0;
    int radius = 2000;
    bool boundsSet = false;
    int minX = 0, maxX = 0, minZ = 0, maxZ = 0;

    int crossFilter = 2;
    int minLong = 0;
    int minShort = 0;
    int minHeight = 0;
    int threads = 0;

    int limit = 0;
    std::string outPath;

    bool help = false;
    bool version = false;
};

struct VersionAlias {
    const char *name;
    int mc;
};

/*
 * Fortress generation is identical for every release inside a group:
 *
 *   1.16 ~ 1.17   -> MC_1_17_1 (21)
 *   1.18 ~ 26.3+  -> MC_26_3   (35)
 *
 * 1.16/1.17 differ from 1.18+ in both structure placement and viability, so
 * they keep their own generation base.  Aliases are kept one per patch
 * release so existing command lines keep working; every alias in a group
 * resolves to that group's representative cubiomes version.
 */
constexpr VersionAlias kVersionAliases[] = {
    {"1.16", 21},
    {"1.16.1", 21},
    {"1.16.5", 21},
    {"1.17", 21},
    {"1.17.1", 21},
    {"1.18", 35},
    {"1.18.1", 35},
    {"1.18.2", 35},
    {"1.19", 35},
    {"1.19.2", 35},
    {"1.19.4", 35},
    {"1.20", 35},
    {"1.20.1", 35},
    {"1.20.4", 35},
    {"1.20.6", 35},
    {"1.21", 35},
    {"1.21.1", 35},
    {"1.21.3", 35},
    {"1.21.4", 35},
    {"1.21.5", 35},
    {"1.21.6", 35},
    {"1.21.9", 35},
    {"1.21.11", 35},
    {"26.1", 35},
    {"26.2", 35},
    {"26.3", 35},
    {"latest", 35},
};

std::string mcDisplayName(int mc) {
    switch (mc) {
    case 19: return "1.16.1";
    case 20: return "1.16";
    case 21: return "1.17";
    case 22: return "1.18";
    case 23: return "1.19.2";
    case 24: return "1.19";
    case 25: return "1.20";
    case 26: return "1.21";
    case 27: return "1.21.3";
    case 28: return "1.21.4";
    case 29: return "1.21.5";
    case 30: return "1.21.6";
    case 31: return "1.21.9";
    case 32: return "1.21.11";
    case 33: return "26.1";
    case 34: return "26.2";
    case 35: return "26.3";
    default: return "unknown";
    }
}

const char *modeName(int mode) {
    return mode == 1 ? "span" : "cross";
}

const char *filterName(int filter) {
    switch (filter) {
    case 3: return "triple-quad";
    case 4: return "quad";
    default: return "all";
    }
}

std::string shapeName(int shapeCode) {
    switch (shapeCode) {
    case 1: return "double";
    case 2: return "triple";
    case 3: return "quad";
    default: return "none";
    }
}

int crossingsForShape(int shapeCode) {
    switch (shapeCode) {
    case 3: return 4;
    case 2: return 3;
    default: return 2;
    }
}

void printUsage(std::ostream &os) {
    os <<
        "FortressFinder standalone CLI\n"
        "\n"
        "Usage:\n"
        "  fortressfinder-cli --seed <int64> [options]\n"
        "\n"
        "Required:\n"
        "  --seed <int64>          Minecraft world seed (decimal, may be negative)\n"
        "\n"
        "Search mode:\n"
        "  --mode cross|span       cross = double/triple/quad crossings (default)\n"
        "                          span  = large fortress bounding boxes\n"
        "\n"
        "Search area (default: center 0,0 with radius 2000):\n"
        "  --center-x <int>        search center X, default 0\n"
        "  --center-z <int>        search center Z, default 0\n"
        "  --radius <int>          half-side in blocks, default 2000\n"
        "  --min-x/--max-x/--min-z/--max-z <int>\n"
        "                          explicit inclusive rectangle; all four are\n"
        "                          required together and override center/radius\n"
        "\n"
        "Search parameters:\n"
        "  --mc <id|version>       generation base, default 26.3 (1.18~26.3+)\n"
        "  --cross-filter all|triple-quad|quad   default all\n"
        "  --min-long <int>        span mode: minimum long edge, default 0\n"
        "  --min-short <int>       span mode: minimum short edge, default 0\n"
        "                          (0 means use --min-long, matching the GUI)\n"
        "  --min-height <int>      span mode: minimum Y span, default 0\n"
        "  --threads <int>         worker threads, default min(CPU, 256)\n"
        "\n"
        "Output:\n"
        "  --limit <int>           maximum result rows, default 0 = all\n"
        "  --out <file>            also write the text report to <file>\n"
        "  --help, -h              show this help\n"
        "  --version               show CLI version\n"
        "\n"
        "Version groups (patches inside a group generate identical fortresses):\n"
        "  1.16 ~ 1.17  : 1.16, 1.16.1, 1.16.5, 1.17, 1.17.1\n"
        "  1.18 ~ 26.3+ : 1.18, 1.18.1, 1.18.2, 1.19, 1.19.2, 1.19.4,\n"
        "                 1.20, 1.20.1, 1.20.4, 1.20.6, 1.21, 1.21.1,\n"
        "                 1.21.3, 1.21.4, 1.21.5, 1.21.6, 1.21.9, 1.21.11,\n"
        "                 26.1, 26.2, 26.3, latest\n"
        "  A raw cubiomes MCVersion id between 19 and 35 is also accepted.\n"
        "\n"
        "Examples:\n"
        "  fortressfinder-cli --seed 12345 --mode cross --radius 5000 --out cross.txt\n"
        "  fortressfinder-cli --seed 12345 --mode span --min-long 100 --min-short 60 \\\n"
        "      --min-height 20 --center-x 1000 --center-z -1000 --radius 8000 --out span.txt\n"
        "  fortressfinder-cli --seed 12345 --mode cross --min-x -10000 --max-x 10000 \\\n"
        "      --min-z -10000 --max-z 10000 --out cross_rect.txt\n";
}

[[noreturn]] void usageError(const std::string &message) {
    std::cerr << "error: " << message << "\n\n";
    printUsage(std::cerr);
    std::exit(2);
}

[[noreturn]] void runtimeError(const std::string &message) {
    std::cerr << "error: " << message << "\n";
    std::exit(1);
}

bool parseLongLong(std::string_view text, long long &value) {
    if (text.empty())
        return false;
    const char *first = text.data();
    const char *last = text.data() + text.size();
    if (*first == '+')
        ++first;
    if (first == last)
        return false;
    auto result = std::from_chars(first, last, value, 10);
    return result.ec == std::errc() && result.ptr == last;
}

bool parseInt(std::string_view text, int &value) {
    long long parsed = 0;
    if (!parseLongLong(text, parsed))
        return false;
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool parseMc(std::string_view text, int &mc) {
    int numeric = 0;
    if (parseInt(text, numeric)) {
        if (numeric < 19 || numeric > 35)
            return false;
        mc = numeric;
        return true;
    }
    for (const VersionAlias &alias: kVersionAliases) {
        if (text == alias.name) {
            mc = alias.mc;
            return true;
        }
    }
    return false;
}

bool parseMode(std::string_view text, int &mode) {
    int numeric = 0;
    if (parseInt(text, numeric)) {
        if (numeric != 0 && numeric != 1)
            return false;
        mode = numeric;
        return true;
    }
    if (text == "cross") {
        mode = 0;
        return true;
    }
    if (text == "span") {
        mode = 1;
        return true;
    }
    return false;
}

bool parseCrossFilter(std::string_view text, int &filter) {
    int numeric = 0;
    if (parseInt(text, numeric)) {
        if (numeric < 2 || numeric > 4)
            return false;
        filter = numeric;
        return true;
    }
    if (text == "all") {
        filter = 2;
        return true;
    }
    if (text == "triple-quad" || text == "triple_quad") {
        filter = 3;
        return true;
    }
    if (text == "quad") {
        filter = 4;
        return true;
    }
    return false;
}

std::string_view requireValue(int argc, char **argv, int &index, std::string_view option) {
    if (index + 1 >= argc)
        usageError("missing value for " + std::string(option));
    ++index;
    return argv[index];
}

Options parseOptions(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--version") {
            options.version = true;
        } else if (arg == "--seed") {
            const std::string_view value = requireValue(argc, argv, i, arg);
            long long seed = 0;
            if (!parseLongLong(value, seed))
                usageError("invalid --seed: " + std::string(value));
            options.seed = seed;
            options.seedSet = true;
        } else if (arg == "--mode") {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseMode(value, options.mode))
                usageError("invalid --mode: " + std::string(value));
        } else if (arg == "--mc") {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseMc(value, options.mc))
                usageError("invalid --mc: " + std::string(value));
        } else if (arg == "--center-x") {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.centerX))
                usageError("invalid --center-x: " + std::string(value));
        } else if (arg == "--center-z") {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.centerZ))
                usageError("invalid --center-z: " + std::string(value));
        } else if (arg == "--radius") {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseInt(value, options.radius))
                usageError("invalid --radius: " + std::string(value));
            if (options.radius < 0)
                usageError("--radius must not be negative");
        } else if (arg == "--min-x" || arg == "--max-x" || arg == "--min-z" || arg == "--max-z") {
            int parsed = 0;
            if (!parseInt(requireValue(argc, argv, i, arg), parsed))
                usageError("invalid " + std::string(arg));
            options.boundsSet = true;
            if (arg == "--min-x")
                options.minX = parsed;
            else if (arg == "--max-x")
                options.maxX = parsed;
            else if (arg == "--min-z")
                options.minZ = parsed;
            else
                options.maxZ = parsed;
        } else if (arg == "--cross-filter") {
            const std::string_view value = requireValue(argc, argv, i, arg);
            if (!parseCrossFilter(value, options.crossFilter))
                usageError("invalid --cross-filter: " + std::string(value));
        } else if (arg == "--min-long") {
            if (!parseInt(requireValue(argc, argv, i, arg), options.minLong) || options.minLong < 0)
                usageError("invalid --min-long");
        } else if (arg == "--min-short") {
            if (!parseInt(requireValue(argc, argv, i, arg), options.minShort) || options.minShort < 0)
                usageError("invalid --min-short");
        } else if (arg == "--min-height") {
            if (!parseInt(requireValue(argc, argv, i, arg), options.minHeight) || options.minHeight < 0)
                usageError("invalid --min-height");
        } else if (arg == "--threads") {
            if (!parseInt(requireValue(argc, argv, i, arg), options.threads) || options.threads < 1
                || options.threads > kMaxThreads)
                usageError("--threads must be between 1 and " + std::to_string(kMaxThreads));
        } else if (arg == "--limit") {
            if (!parseInt(requireValue(argc, argv, i, arg), options.limit) || options.limit < 0)
                usageError("--limit must not be negative");
        } else if (arg == "--out") {
            options.outPath = std::string(requireValue(argc, argv, i, arg));
            if (options.outPath.empty())
                usageError("--out requires a non-empty path");
        } else {
            usageError("unknown option: " + std::string(arg));
        }
    }
    return options;
}

void validateOptions(Options &options) {
    if (!options.seedSet)
        usageError("--seed is required");

    if (options.boundsSet) {
        // FortressSearchRunner.rectToCenterAndRadius() uses exactly this rule.
        if (options.minX > options.maxX)
            usageError("--min-x must be <= --max-x");
        if (options.minZ > options.maxZ)
            usageError("--min-z must be <= --max-z");
        options.centerX = (int) (((long long) options.minX + options.maxX) / 2);
        options.centerZ = (int) (((long long) options.minZ + options.maxZ) / 2);
        options.radius = (int) std::max(
            std::max(options.centerX - options.minX, options.maxX - options.centerX),
            std::max(options.centerZ - options.minZ, options.maxZ - options.centerZ));
    }

    if (std::abs((long long) options.centerX) > kMaxCoordinate
        || std::abs((long long) options.centerZ) > kMaxCoordinate)
        usageError("search center must be within +/-30000000");
    if (options.radius < 0 || options.radius > kMaxCoordinate)
        usageError("--radius must be between 0 and 30000000");

    const long long side = 2LL * options.radius + 1;
    if (side > 0 && side * side > kMaxSearchArea)
        usageError("search area is too large (maximum 30000000 x 30000000 blocks)");

    if (options.threads <= 0) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        options.threads = static_cast<int>(std::min<unsigned int>(
            static_cast<unsigned int>(kMaxThreads), hardware == 0 ? 1u : hardware));
    }
}

std::string formatReport(const Options &options, const std::vector<FortressHit> &hits,
                         size_t totalHits, int threads) {
    const int minX = options.centerX - options.radius;
    const int maxX = options.centerX + options.radius;
    const int minZ = options.centerZ - options.radius;
    const int maxZ = options.centerZ + options.radius;

    std::ostringstream out;
    out << "# FortressFinder CLI " << kCliVersion << "\n";
    out << "# engine=FortressFinderBase " << kEngineVersion
        << " seed=" << options.seed
        << " mc=" << mcDisplayName(options.mc) << "(" << options.mc << ")"
        << " mode=" << modeName(options.mode)
        << " center=" << options.centerX << "," << options.centerZ
        << " radius=" << options.radius
        << " search=[" << minX << "," << maxX << "]x[" << minZ << "," << maxZ << "]"
        << " crossFilter=" << filterName(options.crossFilter)
        << " minLong=" << options.minLong
        << " minShort=" << options.minShort
        << " minHeight=" << options.minHeight
        << " threads=" << threads
        << " hits=" << totalHits;
    if (hits.size() != totalHits)
        out << " shown=" << hits.size();
    out << "\n";

    if (options.mode == 1) {
        out << "chunkX\tchunkZ\tx\ty\tz\tspanX\tspanY\tspanZ\tteleport\n";
        for (const FortressHit &hit: hits) {
            out << hit.chunkX << '\t' << hit.chunkZ << '\t'
                << hit.outX << '\t' << hit.outY << '\t' << hit.outZ << '\t'
                << hit.longEdge << '\t' << hit.heightEdge << '\t' << hit.shortEdge << '\t'
                << "/tp " << hit.outX << ' ' << hit.outY << ' ' << hit.outZ << ' '
                << hit.longEdge << '*' << hit.heightEdge << '*' << hit.shortEdge << '\n';
        }
    } else {
        out << "chunkX\tchunkZ\tx\ty\tz\tcrossings\tshape\tteleport\n";
        for (const FortressHit &hit: hits) {
            const int crossings = crossingsForShape(hit.shapeCode);
            out << hit.chunkX << '\t' << hit.chunkZ << '\t'
                << hit.outX << '\t' << hit.outY << '\t' << hit.outZ << '\t'
                << crossings << '\t' << shapeName(hit.shapeCode) << '\t'
                << "/tp " << hit.outX << ' ' << hit.outY << ' ' << hit.outZ << ' '
                << crossings << "Crossings" << '\n';
        }
    }
    return out.str();
}

int64_t distanceFromOriginSquared(const FortressHit &hit) {
    return static_cast<int64_t>(hit.outX) * hit.outX + static_cast<int64_t>(hit.outZ) * hit.outZ;
}

} // namespace

int run(int argc, char **argv) {
    Options options = parseOptions(argc, argv);

    if (options.help) {
        printUsage(std::cout);
        return 0;
    }
    if (options.version) {
        std::cout << "fortressfinder-cli " << kCliVersion
                  << " (FortressFinderBase " << kEngineVersion << ")\n";
        return 0;
    }

    validateOptions(options);

    SearchConfig cfg;
    cfg.mode = options.mode;
    cfg.seed = static_cast<uint64_t>(options.seed);
    cfg.centerX = options.centerX;
    cfg.centerZ = options.centerZ;
    cfg.r = options.radius;
    cfg.mc = options.mc;
    cfg.crossFilter = options.crossFilter;
    cfg.minLong = options.minLong;
    cfg.minShort = options.minShort;
    cfg.minHeight = options.minHeight;

    Progress progress{};
    ThreadSafeResults<FortressHit> results;

    std::cerr << "searching seed=" << options.seed
              << " mc=" << mcDisplayName(options.mc)
              << " mode=" << modeName(options.mode)
              << " center=" << options.centerX << "," << options.centerZ
              << " radius=" << options.radius
              << " threads=" << options.threads << "\n";

    const auto started = std::chrono::steady_clock::now();
    runFortressSearch(cfg, &progress, results, options.threads);
    auto hits = results.getAllResults();

    std::sort(hits.begin(), hits.end(), [](const FortressHit &left, const FortressHit &right) {
        const int64_t leftDistance = distanceFromOriginSquared(left);
        const int64_t rightDistance = distanceFromOriginSquared(right);
        if (leftDistance != rightDistance)
            return leftDistance < rightDistance;
        if (left.outX != right.outX)
            return left.outX < right.outX;
        return left.outZ < right.outZ;
    });

    const size_t totalHits = hits.size();
    if (options.limit > 0 && hits.size() > static_cast<size_t>(options.limit))
        hits.resize(static_cast<size_t>(options.limit));

    const std::string report = formatReport(options, hits, totalHits, options.threads);
    std::cout << report;

    if (!options.outPath.empty()) {
        // Open the native narrow path directly; on Windows this keeps
        // ANSI/GBK command-line paths working without a filesystem codecvt
        // conversion. The report itself is plain ASCII.
        std::ofstream outFile(options.outPath, std::ios::out | std::ios::trunc);
        if (!outFile)
            runtimeError("cannot open --out file: " + options.outPath);
        outFile << report;
        if (!outFile)
            runtimeError("failed to write --out file: " + options.outPath);
        std::cerr << "report written to " << options.outPath << "\n";
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    std::cerr << "done in " << elapsed.count() << " ms, " << hits.size() << " hit(s)\n";
    return 0;
}

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "error: unknown failure\n";
        return 1;
    }
}
