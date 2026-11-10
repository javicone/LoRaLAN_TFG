#pragma once
// Molde de los casos: una ficha por caso y un veredicto por propiedad.

#include <stdint.h>

#include <string>
#include <vector>

enum class Verdict : uint8_t { PASS, FAIL, INCONCLUSIVE };
const char* verdictName(Verdict v);

struct Property {
    std::string requirement;
    std::string text;
    std::string knownFail;              // motivo si es un fallo ya conocido
    int checks = 0;
    std::vector<std::string> failures;  // lo observado en cada asercion incumplida
    Verdict verdict = Verdict::INCONCLUSIVE;
};

class Ctx {
public:
    explicit Ctx(uint32_t seed = 42) : seed_(seed) {}
    uint32_t seed() const { return seed_; }

    void prop(const char* requirement, const char* text);
    void knownFail(const char* reason);
    void expectEq(long actual, long expected, const char* what);
    void expectTrue(bool ok, const char* what);

    // Lo que anota World: reglas que no actuaron y precondiciones no alcanzadas.
    void addNotProduced(const std::string& what) { notProduced_.push_back(what); }
    void addTrace(const std::string& csv) { trace_ += csv; }
    int nextWorldIndex() { return worldCount_++; }

    // Reglas cerradas: un estimulo que no se produjo da No concluyente; si se
    // produjo, cualquier asercion incumplida da Fallida.
    void finish();
    const std::vector<Property>& properties() const { return props_; }
    const std::vector<std::string>& notProduced() const { return notProduced_; }
    const std::string& trace() const { return trace_; }

private:
    uint32_t seed_;
    int worldCount_ = 0;
    std::vector<Property> props_;
    std::vector<std::string> notProduced_;
    std::string trace_;
};

struct CaseInfo {
    const char* id;
    const char* family;
    const char* title;
    const char* expectedDate;   // fecha en que se escribio lo esperado
    const char* preconditions;
    const char* stimulus;
    void (*run)(Ctx&);
};

std::vector<CaseInfo>& caseRegistry();

struct CaseRegistrar {
    explicit CaseRegistrar(const CaseInfo& info) { caseRegistry().push_back(info); }
};

#define SIM_CONCAT_(a, b) a##b
#define SIM_CONCAT(a, b) SIM_CONCAT_(a, b)
#define SIM_CASE_IMPL(fn, id, family, title, date, pre, stim)                                    \
    static void fn(Ctx& ctx);                                                                    \
    static const CaseRegistrar SIM_CONCAT(fn, _reg)(CaseInfo{id, family, title, date, pre, stim, fn}); \
    static void fn([[maybe_unused]] Ctx& ctx)
#define CASE(id, family, title, date, pre, stim) \
    SIM_CASE_IMPL(SIM_CONCAT(simCase_, __LINE__), id, family, title, date, pre, stim)

#define PROP(requirement, text) ctx.prop(requirement, text)
#define KNOWN_FAIL(reason) ctx.knownFail(reason)
#define EXPECT_EQ(actual, expected, what) ctx.expectEq(static_cast<long>(actual), static_cast<long>(expected), what)
#define EXPECT_TRUE(cond, what) ctx.expectTrue((cond), what)
