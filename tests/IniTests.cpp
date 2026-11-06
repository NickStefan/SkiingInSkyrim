#include "Check.h"

#include "core/Ini.h"

using namespace SkiingInSkyrim::Core;

TEST_CASE("ini reads sections, keys and numbers")
{
    const auto ini = Ini::Parse(
        "; header comment\n"
        "[Controls]\n"
        "ToggleKeyDevice = 0\n"
        "ToggleKey=37\r\n"
        "\n"
        "[Physics]\n"
        "  SnowFriction = 0.05   ; inline comment\n"
        "MaxSpeed=30\n");
    CHECK(ini.GetInt("Controls", "ToggleKeyDevice") == 0);
    CHECK(ini.GetInt("Controls", "ToggleKey") == 37);
    CHECK(ini.GetFloat("Physics", "SnowFriction") == 0.05F);
    CHECK(ini.GetFloat("Physics", "MaxSpeed") == 30.0F);
}

TEST_CASE("ini lookups are case-insensitive and section-scoped")
{
    const auto ini = Ini::Parse("[Physics]\nMaxSpeed=12\n[Other]\nMaxSpeed=99\n");
    CHECK(ini.GetFloat("physics", "maxspeed") == 12.0F);
    CHECK(ini.GetFloat("OTHER", "MAXSPEED") == 99.0F);
    CHECK(!ini.GetFloat("Controls", "MaxSpeed"));
}

TEST_CASE("ini rejects malformed numbers instead of guessing")
{
    const auto ini = Ini::Parse("[A]\nx=abc\ny=1.5m\nz=\nw=nan\nn=-3\n");
    CHECK(!ini.GetFloat("A", "x"));
    CHECK(!ini.GetFloat("A", "y"));
    CHECK(!ini.GetFloat("A", "z"));
    CHECK(!ini.GetFloat("A", "w"));
    CHECK(ini.GetInt("A", "n") == -3);
    CHECK(!ini.GetInt("A", "missing"));
}
