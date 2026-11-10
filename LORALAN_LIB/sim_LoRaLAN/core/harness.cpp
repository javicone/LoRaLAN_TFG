#include "harness.h"

const char* verdictName(Verdict v) {
    switch (v) {
        case Verdict::PASS: return "Superada";
        case Verdict::FAIL: return "Fallida";
        case Verdict::INCONCLUSIVE: return "No concluyente";
    }
    return "?";
}

void Ctx::prop(const char* requirement, const char* text) {
    Property p;
    p.requirement = requirement;
    p.text = text;
    props_.push_back(std::move(p));
}

void Ctx::knownFail(const char* reason) {
    if (props_.empty()) prop("?", "(KNOWN_FAIL sin propiedad)");
    props_.back().knownFail = reason;
}

void Ctx::expectEq(long actual, long expected, const char* what) {
    if (props_.empty()) prop("?", "(asercion sin propiedad)");
    Property& p = props_.back();
    p.checks++;
    if (actual != expected) {
        p.failures.push_back(std::string(what) + ": observado " + std::to_string(actual) + ", esperado " +
                             std::to_string(expected));
    }
}

void Ctx::expectTrue(bool ok, const char* what) {
    if (props_.empty()) prop("?", "(asercion sin propiedad)");
    Property& p = props_.back();
    p.checks++;
    if (!ok) p.failures.push_back(what);
}

void Ctx::finish() {
    for (Property& p : props_) {
        if (!notProduced_.empty() || p.checks == 0) p.verdict = Verdict::INCONCLUSIVE;
        else p.verdict = p.failures.empty() ? Verdict::PASS : Verdict::FAIL;
    }
}

std::vector<CaseInfo>& caseRegistry() {
    static std::vector<CaseInfo> registry;
    return registry;
}
