/*
 * fortress-version-check: compare Nether fortress generation across
 * Minecraft/cubiomes versions.
 *
 * For every structure region in a square and every requested version this
 * tool records:
 *   - the fortress structure position and isViableStructurePos(Fortress)
 *   - the bastion structure position and isViableStructurePos(Bastion)
 *     (1.18+ fortress viability is "bastion is not viable")
 *   - the bastion StructureVariant (rotation/start/size)
 *   - a hash of every generated fortress piece (type + bounding box)
 *
 * Records are keyed by region, so any version drift is reported with the
 * exact seed/region/coordinates where it happens.
 *
 * Exit status is 0 when every version matches the baseline, 1 on mismatch
 * (unless --report-only is given).
 */

#include "cubiomes/finders.h"
#include "cubiomes/generator.h"
#include "cubiomes/biomes.h"
#include "cubiomes/features/fortress.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Options {
    std::vector<uint64_t> seeds{0ULL, 1ULL, 12345ULL, ~0ULL, 4294967296ULL};
    std::vector<int> versions{MC_1_18, MC_1_19, MC_1_20, MC_1_21, MC_26_1, MC_26_2, MC_26_3};
    int regionRadius = 40; // regions in each direction: [-R, R]
    bool reportOnly = false;
};

struct Record {
    int fposX = 0, fposZ = 0;
    bool fpos = false;
    bool fviable = false;
    int bposX = 0, bposZ = 0;
    bool bpos = false;
    bool bviable = false;
    uint64_t bvariant = 0;
    uint64_t pieces = 0;
    int pieceCount = 0;
};

uint64_t fnv1a(uint64_t hash, const void *data, size_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

uint64_t hashBastionVariant(int mc, uint64_t seed, int x, int z) {
    StructureVariant sv{};
    getVariant(&sv, Bastion, mc, seed, x, z, -1);
    uint64_t h = 0xcbf29ce484222325ULL;
    h = fnv1a(h, &sv.start, sizeof(sv.start));
    h = fnv1a(h, &sv.rotation, sizeof(sv.rotation));
    h = fnv1a(h, &sv.mirror, sizeof(sv.mirror));
    h = fnv1a(h, &sv.x, sizeof(sv.x));
    h = fnv1a(h, &sv.z, sizeof(sv.z));
    h = fnv1a(h, &sv.sx, sizeof(sv.sx));
    h = fnv1a(h, &sv.sz, sizeof(sv.sz));
    return h;
}

uint64_t hashFortressPieces(int mc, uint64_t seed, int x, int z, int &count) {
    Piece pieces[400];
    const int n = getFortressPieces(pieces, 400, mc, seed, x >> 4, z >> 4);
    count = n > 0 ? n : 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < n; i++) {
        const Piece &p = pieces[i];
        h = fnv1a(h, &p.type, sizeof(p.type));
        h = fnv1a(h, &p.bb0.x, sizeof(p.bb0.x));
        h = fnv1a(h, &p.bb0.y, sizeof(p.bb0.y));
        h = fnv1a(h, &p.bb0.z, sizeof(p.bb0.z));
        h = fnv1a(h, &p.bb1.x, sizeof(p.bb1.x));
        h = fnv1a(h, &p.bb1.y, sizeof(p.bb1.y));
        h = fnv1a(h, &p.bb1.z, sizeof(p.bb1.z));
    }
    return h;
}

bool parseSeed(const std::string &text, uint64_t &out) {
    char *end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0')
        return false;
    out = static_cast<uint64_t>(value);
    return true;
}

std::vector<uint64_t> parseSeeds(const std::string &text) {
    std::vector<uint64_t> seeds;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        uint64_t seed = 0;
        if (!item.empty()) {
            if (!parseSeed(item, seed)) {
                std::fprintf(stderr, "error: invalid seed '%s'\n", item.c_str());
                std::exit(2);
            }
            seeds.push_back(seed);
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return seeds;
}

std::vector<int> parseVersions(const std::string &text) {
    std::vector<int> versions;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!item.empty()) {
            char *end = nullptr;
            const long value = std::strtol(item.c_str(), &end, 10);
            if (end == item.c_str() || *end != '\0' || value < 0 || value > MC_NEWEST) {
                std::fprintf(stderr, "error: invalid MC version id '%s'\n", item.c_str());
                std::exit(2);
            }
            versions.push_back(static_cast<int>(value));
        }
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return versions;
}

const char *versionName(int mc) {
    switch (mc) {
    case MC_1_16_1: return "1.16.1";
    case MC_1_16_5: return "1.16.5";
    case MC_1_17_1: return "1.17.1";
    case MC_1_18_2: return "1.18.2";
    case MC_1_19_2: return "1.19.2";
    case MC_1_19_4: return "1.19.4";
    case MC_1_20_6: return "1.20.6";
    case MC_1_21_1: return "1.21.1";
    case MC_1_21_3: return "1.21.3";
    case MC_1_21_4: return "1.21.4";
    case MC_1_21_5: return "1.21.5";
    case MC_1_21_6: return "1.21.6";
    case MC_1_21_9: return "1.21.9";
    case MC_1_21_11: return "1.21.11";
    case MC_26_1: return "26.1";
    case MC_26_2: return "26.2";
    case MC_26_3: return "26.3";
    default: return "?";
    }
}

void printUsage() {
    std::printf(
        "Usage: fortress-version-check [options]\n"
        "\n"
        "  --seeds <a,b,...>      world seeds (default 0,1,12345,-1,4294967296)\n"
        "  --versions <id,...>    cubiomes MCVersion ids (default 22,24,25,26,33,34,35)\n"
        "  --regions <n>          structure-region radius (default 40)\n"
        "  --report-only          never fail; only report differences\n"
        "  --help                 show this help\n");
}

} // namespace

int main(int argc, char **argv) {
    Options opt;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--report-only") {
            opt.reportOnly = true;
            continue;
        }
        if (arg == "--seeds" && i + 1 < argc) {
            opt.seeds = parseSeeds(argv[++i]);
            continue;
        }
        if (arg == "--versions" && i + 1 < argc) {
            opt.versions = parseVersions(argv[++i]);
            continue;
        }
        if (arg == "--regions" && i + 1 < argc) {
            opt.regionRadius = std::atoi(argv[++i]);
            continue;
        }
        std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
        printUsage();
        return 2;
    }
    if (opt.versions.size() < 2 || opt.seeds.empty() || opt.regionRadius < 0) {
        std::fprintf(stderr, "error: need at least two versions, one seed and regions >= 0\n");
        return 2;
    }

    const int baseMc = opt.versions.front();
    const int side = 2 * opt.regionRadius + 1;
    const size_t recordCount = static_cast<size_t>(side) * side;
    std::vector<Record> base;
    std::vector<Record> current;

    uint64_t totalChecked = 0;
    uint64_t totalFortresses = 0;
    uint64_t totalBastions = 0;
    uint64_t grandMismatches = 0;

    std::printf("baseline: %s (%d), region radius %d (%dx%d regions), %zu seed(s)\n",
                versionName(baseMc), baseMc, opt.regionRadius, side, side, opt.seeds.size());
    std::printf("[fortress generation: positions, viability, bastion viability, pieces]\n\n");

    for (size_t si = 0; si < opt.seeds.size(); si++) {
        const uint64_t seed = opt.seeds[si];
        std::printf("seed %" PRIu64 "\n", seed);
        uint64_t seedMismatches = 0;

        for (size_t vi = 0; vi < opt.versions.size(); vi++) {
            const int mc = opt.versions[vi];
            Generator g;
            setupGenerator(&g, mc, 0);
            applySeed(&g, DIM_NETHER, seed);
            std::vector<Record> &records = vi == 0 ? base : current;
            records.assign(recordCount, Record{});
            uint64_t fortresses = 0;
            uint64_t bastions = 0;

            for (int rz = -opt.regionRadius; rz <= opt.regionRadius; rz++) {
                for (int rx = -opt.regionRadius; rx <= opt.regionRadius; rx++) {
                    Record &rec = records[static_cast<size_t>(rz + opt.regionRadius) * side
                                         + (rx + opt.regionRadius)];
                    Pos pos;
                    if (getStructurePos(Fortress, mc, seed, rx, rz, &pos)) {
                        rec.fpos = true;
                        rec.fposX = pos.x;
                        rec.fposZ = pos.z;
                        rec.fviable = isViableStructurePos(Fortress, &g, pos.x, pos.z, 0) != 0;
                        if (rec.fviable) {
                            fortresses++;
                            rec.pieces = hashFortressPieces(mc, seed, pos.x, pos.z, rec.pieceCount);
                        }
                    }
                    if (getStructurePos(Bastion, mc, seed, rx, rz, &pos)) {
                        rec.bpos = true;
                        rec.bposX = pos.x;
                        rec.bposZ = pos.z;
                        rec.bviable = isViableStructurePos(Bastion, &g, pos.x, pos.z, 0) != 0;
                        if (rec.bviable) {
                            bastions++;
                            rec.bvariant = hashBastionVariant(mc, seed, pos.x, pos.z);
                        }
                    }
                }
            }
            totalChecked += recordCount;
            totalFortresses += fortresses;
            totalBastions += bastions;

            if (vi == 0) {
                std::printf("  %-8s (%-2d): %5zu regions, %5" PRIu64 " fortresses, %5" PRIu64 " bastions  [baseline]\n",
                            versionName(mc), mc, recordCount, fortresses, bastions);
                continue;
            }

            uint64_t diffFortressPos = 0, diffFortressViable = 0, diffBastionPos = 0;
            uint64_t diffBastionViable = 0, diffBastionVariant = 0, diffPieces = 0;
            int shown = 0;
            for (size_t k = 0; k < recordCount; k++) {
                const Record &a = base[k];
                const Record &b = current[k];
                const int rz = static_cast<int>(k / side) - opt.regionRadius;
                const int rx = static_cast<int>(k % side) - opt.regionRadius;
                bool differ = false;

                if (a.fpos != b.fpos || (a.fpos && (a.fposX != b.fposX || a.fposZ != b.fposZ))) {
                    diffFortressPos++;
                    differ = true;
                }
                if (a.fviable != b.fviable) {
                    diffFortressViable++;
                    differ = true;
                }
                if (a.bpos != b.bpos || (a.bpos && (a.bposX != b.bposX || a.bposZ != b.bposZ))) {
                    diffBastionPos++;
                    differ = true;
                }
                if (a.bviable != b.bviable) {
                    diffBastionViable++;
                    differ = true;
                }
                if (a.bviable && b.bviable && a.bvariant != b.bvariant) {
                    diffBastionVariant++;
                    differ = true;
                }
                if (a.fviable && b.fviable && (a.pieces != b.pieces || a.pieceCount != b.pieceCount)) {
                    diffPieces++;
                    differ = true;
                }

                if (differ && shown < 8) {
                    std::printf("    ! seed=%" PRIu64 " region=(%d,%d) fortress %s%d,%d->%s%d,%d viable %d->%d "
                                "bastion %s%d,%d->%s%d,%d viable %d->%d pieces %d->%d\n",
                                seed, rx, rz,
                                a.fpos ? "" : "none ", a.fposX, a.fposZ, b.fpos ? "" : "none ", b.fposX, b.fposZ,
                                a.fviable, b.fviable,
                                a.bpos ? "" : "none ", a.bposX, a.bposZ, b.bpos ? "" : "none ", b.bposX, b.bposZ,
                                a.bviable, b.bviable, a.pieceCount, b.pieceCount);
                    shown++;
                }
            }

            const uint64_t mismatches = diffFortressPos + diffFortressViable + diffBastionPos
                                        + diffBastionViable + diffBastionVariant + diffPieces;
            seedMismatches += mismatches;
            if (mismatches == 0) {
                std::printf("  %-8s (%-2d): %5zu regions, %5" PRIu64 " fortresses, %5" PRIu64 " bastions  [identical]\n",
                            versionName(mc), mc, recordCount, fortresses, bastions);
            } else {
                std::printf("  %-8s (%-2d): %5zu regions, %5" PRIu64 " fortresses, %5" PRIu64 " bastions  "
                            "[DIFF fortressPos=%" PRIu64 " fortressViable=%" PRIu64
                            " bastionPos=%" PRIu64 " bastionViable=%" PRIu64
                            " bastionVariant=%" PRIu64 " pieces=%" PRIu64 "]\n",
                            versionName(mc), mc, recordCount, fortresses, bastions,
                            diffFortressPos, diffFortressViable, diffBastionPos,
                            diffBastionViable, diffBastionVariant, diffPieces);
            }
        }
        grandMismatches += seedMismatches;
        std::printf("  seed %" PRIu64 ": %" PRIu64 " difference(s)\n\n", seed, seedMismatches);
    }

    std::printf("total: %" PRIu64 " region checks, %" PRIu64 " viable fortresses, %" PRIu64 " viable bastions\n",
                totalChecked, totalFortresses, totalBastions);
    if (grandMismatches == 0) {
        std::printf("result: all versions identical to baseline %s\n", versionName(baseMc));
        return 0;
    }
    std::printf("result: %" PRIu64 " difference(s) from baseline %s\n", grandMismatches, versionName(baseMc));
    return opt.reportOnly ? 0 : 1;
}
