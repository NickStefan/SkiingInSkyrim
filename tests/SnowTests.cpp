#include "Check.h"

#include "core/Snow.h"

#include <array>

using namespace SkiingInSkyrim::Core;

TEST_CASE("powder is the share of snowy points off roads, paths and trails")
{
    const std::array<SnowSample, 4> samples{ {
        { .snow = true },
        { .snow = true, .packed = true },
        { .snow = false },
        { .snow = true },
    } };
    CHECK_NEAR(PowderCover(samples), 0.5F, 1e-4F);
    CHECK(PowderCover({}) == 0.0F);
}

TEST_CASE("a value follows its target over the distance travelled")
{
    CHECK_NEAR(FollowOverDistance(0.0F, 1.0F, 1.5F, 3.0F), 0.5F, 1e-4F);
    CHECK_NEAR(FollowOverDistance(0.0F, 1.0F, 10.0F, 3.0F), 1.0F, 1e-4F);
    CHECK(FollowOverDistance(0.4F, 1.0F, 0.0F, 3.0F) == 0.4F);
    CHECK(FollowOverDistance(0.4F, 1.0F, 0.0F, 0.0F) == 1.0F);
}
