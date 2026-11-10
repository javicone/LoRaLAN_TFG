// Ejecutor del catalogo.
//   sim_tests --list
//   sim_tests --all [--seed N] [--out DIR] [--verbose]
//   sim_tests --case U6 [--seed N] [--verbose]
//   sim_tests --family PUSH
// Codigo de salida: 0 si no hay nada inesperado. Un fallo conocido sigue siendo
// Fallida en los resultados, pero no rompe la regresion; un fallo nuevo, un
// No concluyente o un fallo conocido que deja de fallar si la rompen.

#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "harness.h"
#include "trace.h"

namespace {

std::string csvField(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::string joinText(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) out += (out.empty() ? "" : " | ") + item;
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    bool list = false, all = false;
    std::string caseId, family, outDir;
    uint32_t seed = 42;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") list = true;
        else if (a == "--all") all = true;
        else if (a == "--verbose") sim_serial_quiet = false;
        else if (a == "--case" && i + 1 < argc) caseId = argv[++i];
        else if (a == "--family" && i + 1 < argc) family = argv[++i];
        else if (a == "--seed" && i + 1 < argc) seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else {
            std::printf("Uso: sim_tests --list | --all | --case ID | --family F [--seed N] [--out DIR] [--verbose]\n");
            return 2;
        }
    }
    if (!list && !all && caseId.empty() && family.empty()) all = true;

    std::vector<CaseInfo> cases = caseRegistry();
    std::sort(cases.begin(), cases.end(), [](const CaseInfo& a, const CaseInfo& b) {
        const int f = std::strcmp(a.family, b.family);
        return f != 0 ? f < 0 : std::strcmp(a.id, b.id) < 0;
    });

    if (list) {
        for (const CaseInfo& c : cases) std::printf("%-6s %-10s %s\n", c.id, c.family, c.title);
        return 0;
    }

    std::ofstream csv;
    if (!outDir.empty()) {
        std::filesystem::create_directories(outDir + "/trazas");
        csv.open(outDir + "/casos.csv", std::ios::binary);
        csv << "id,familia,titulo,fecha_esperado,precondiciones,estimulo,requisito,propiedad,veredicto,conocido,observado,semilla\n";
    }

    int ran = 0, passed = 0, failed = 0, inconclusive = 0, known = 0, unexpected = 0;
    for (const CaseInfo& c : cases) {
        if (!all && caseId != c.id && family != c.family) continue;
        ++ran;
        Ctx ctx(seed);
        c.run(ctx);
        ctx.finish();

        std::printf("%-6s %s\n", c.id, c.title);
        if (ctx.properties().empty()) {
            std::printf("  [No concluyente] el caso no declara propiedades\n");
            ++inconclusive;
            ++unexpected;
        }
        for (const Property& p : ctx.properties()) {
            std::string observed = joinText(p.failures);
            if (!ctx.notProduced().empty()) observed = "estimulo no producido: " + joinText(ctx.notProduced());
            const bool isKnown = !p.knownFail.empty();
            std::printf("  [%s] %s: %s%s\n", verdictName(p.verdict), p.requirement.c_str(), p.text.c_str(),
                        isKnown ? "  (fallo conocido)" : "");
            if (!observed.empty()) std::printf("      observado: %s\n", observed.c_str());

            switch (p.verdict) {
                case Verdict::PASS:
                    ++passed;
                    if (isKnown) {
                        std::printf("      INESPERADO: estaba marcado como fallo conocido y se supera\n");
                        ++unexpected;
                    }
                    break;
                case Verdict::FAIL:
                    ++failed;
                    if (isKnown) ++known;
                    else ++unexpected;
                    break;
                case Verdict::INCONCLUSIVE:
                    ++inconclusive;
                    ++unexpected;
                    break;
            }
            if (csv.is_open()) {
                csv << c.id << ',' << c.family << ',' << csvField(c.title) << ',' << c.expectedDate << ','
                    << csvField(c.preconditions) << ',' << csvField(c.stimulus) << ',' << p.requirement << ','
                    << csvField(p.text) << ',' << verdictName(p.verdict) << ',' << csvField(p.knownFail) << ','
                    << csvField(observed) << ',' << seed << '\n';
            }
        }
        if (!outDir.empty()) {
            std::ofstream trace(outDir + "/trazas/" + c.id + ".csv", std::ios::binary);
            trace << Trace::csvHeader() << ctx.trace();
        }
    }

    std::printf("\nCasos: %d | Propiedades: %d superadas, %d fallidas (%d conocidas), %d no concluyentes | Inesperado: %d\n",
                ran, passed, failed, known, inconclusive, unexpected);
    if (ran == 0) {
        std::printf("Ningun caso coincide con el filtro\n");
        return 2;
    }
    return unexpected == 0 ? 0 : 1;
}
