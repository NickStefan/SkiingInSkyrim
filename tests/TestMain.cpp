#include "Check.h"

int main()
{
    for (const auto& test : Check::Cases()) {
        const int before = Check::Failures();
        test.body();
        std::printf("%s %s\n", Check::Failures() == before ? "pass" : "FAIL", test.name);
    }
    std::printf("%zu tests, %d failed checks\n", Check::Cases().size(), Check::Failures());
    return Check::Failures() == 0 ? 0 : 1;
}
