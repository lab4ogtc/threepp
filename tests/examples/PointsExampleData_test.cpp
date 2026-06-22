#include <catch2/catch_test_macros.hpp>

#include "PointsExampleData.hpp"

#include "threepp/core/BufferAttribute.hpp"
#include "threepp/core/BufferGeometry.hpp"

using namespace threepp;
using namespace threepp::examples::points;

TEST_CASE("points example initializes all generated vertices") {

    int calls = 0;
    auto data = makePointCloudData(4, 10.f, [&] {
        ++calls;
        return static_cast<float>(calls * 2 + 1) / 32.f;
    });

    REQUIRE(data.positions.size() == 12);
    REQUIRE(data.colors.size() == 12);
    CHECK(calls == 12);

    for (int point = 0; point < 4; ++point) {
        const auto i = point * 3;
        CHECK(data.positions[i] != 0.f);
        CHECK(data.positions[i + 1] != 0.f);
        CHECK(data.positions[i + 2] != 0.f);
    }
}

TEST_CASE("points example controls visible count through draw range") {

    auto geometry = BufferGeometry::create();
    geometry->setAttribute("position", FloatBufferAttribute::create(std::vector<float>(12), 3));

    setActivePointCount(*geometry, 2, 4);
    CHECK(geometry->drawRange.start == 0);
    CHECK(geometry->drawRange.count == 2);

    setActivePointCount(*geometry, 99, 4);
    CHECK(geometry->drawRange.count == 4);

    setActivePointCount(*geometry, -1, 4);
    CHECK(geometry->drawRange.count == 0);
}
