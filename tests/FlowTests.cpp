#include "Check.h"

#include "core/Flow.h"

using namespace SkiingInSkyrim::Core;

namespace
{
    const FlowSettings kSettings{ .fillMetres = 100.0F, .reserveMetres = 20.0F };

    FlowState Full()
    {
        FlowState state;
        FlowDescend(state, 100.0F, kSettings);
        return state;
    }
}

TEST_CASE("descending fills the bar, and it's full once at the fill height")
{
    FlowState state;
    int       filled = 0;
    for (int i = 0; i < 99; ++i) {
        filled += FlowDescend(state, 1.0F, kSettings) ? 1 : 0;
    }
    CHECK(!state.Full());
    CHECK_NEAR(FlowShown(state, kSettings), 0.99F, 1e-3F);
    filled += FlowDescend(state, 1.0F, kSettings) ? 1 : 0;
    CHECK(state.Full());
    CHECK(filled == 1);
    CHECK_NEAR(state.reserve, 20.0F, 1e-4F);
    CHECK_NEAR(FlowShown(state, kSettings), 1.0F, 1e-4F);
    CHECK(!FlowDescend(state, 50.0F, kSettings));
    CHECK_NEAR(state.reserve, 20.0F, 1e-4F);
}

TEST_CASE("climbing and nonsense heights don't fill or empty the bar")
{
    FlowState state;
    FlowDescend(state, 30.0F, kSettings);
    FlowDescend(state, -10.0F, kSettings);
    FlowDescend(state, std::nanf(""), kSettings);
    CHECK_NEAR(state.descended, 30.0F, 1e-4F);
}

TEST_CASE("a tumble while filling starts the bar over")
{
    FlowState state;
    FlowDescend(state, 60.0F, kSettings);
    FlowFall(state);
    CHECK_NEAR(FlowShown(state, kSettings), 0.0F, 1e-4F);
}

TEST_CASE("a filling bar lets hits through")
{
    FlowState state;
    FlowDescend(state, 60.0F, kSettings);
    CHECK(!FlowAbsorb(state, 10.0F));
    CHECK_NEAR(state.descended, 60.0F, 1e-4F);
}

TEST_CASE("a full bar takes hits from its reserve")
{
    auto state = Full();
    CHECK(FlowAbsorb(state, 8.0F));
    CHECK(state.Full());
    CHECK_NEAR(FlowShown(state, kSettings), 0.6F, 1e-4F);
    CHECK(FlowAbsorb(state, 8.0F));
    CHECK_NEAR(state.reserve, 4.0F, 1e-4F);
}

TEST_CASE("a hit bigger than what's left is let off and empties the bar")
{
    auto state = Full();
    FlowAbsorb(state, 15.0F);
    CHECK(FlowAbsorb(state, 30.0F));
    CHECK(!state.Full());
    CHECK_NEAR(FlowShown(state, kSettings), 0.0F, 1e-4F);
    CHECK(!FlowAbsorb(state, 5.0F));
    CHECK(!FlowDescend(state, 99.0F, kSettings));
    CHECK(FlowDescend(state, 1.0F, kSettings));
}

TEST_CASE("a tumble doesn't empty a full bar")
{
    auto state = Full();
    FlowFall(state);
    CHECK(state.Full());
}
