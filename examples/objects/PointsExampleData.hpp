#ifndef THREEPP_EXAMPLES_OBJECTS_POINTSEXAMPLEDATA_HPP
#define THREEPP_EXAMPLES_OBJECTS_POINTSEXAMPLEDATA_HPP

#include "threepp/core/BufferGeometry.hpp"
#include "threepp/math/MathUtils.hpp"

#include <algorithm>
#include <vector>

namespace threepp::examples::points {

    struct PointCloudData {
        std::vector<float> positions;
        std::vector<float> colors;
    };

    /**
     * 生成 points 示例使用的位置和颜色数据。
     *
     * @param pointCount 要生成的点数量。
     * @param extent 点云每个坐标轴的分布宽度。
     * @param random01 返回 [0, 1] 范围随机值的可调用对象。
     * @return 每个点三个 float 的 position/color buffer。
     */
    template<class Random01>
    PointCloudData makePointCloudData(int pointCount, float extent, Random01&& random01) {

        const auto count = std::max(0, pointCount);
        PointCloudData data{
                std::vector<float>(static_cast<std::size_t>(count) * 3),
                std::vector<float>(static_cast<std::size_t>(count) * 3)};

        const auto halfExtent = extent * 0.5f;
        for (int point = 0; point < count; ++point) {
            const auto i = point * 3;

            data.positions[i] = random01() * extent - halfExtent;
            data.positions[i + 1] = random01() * extent - halfExtent;
            data.positions[i + 2] = random01() * extent - halfExtent;

            data.colors[i] = data.positions[i] / extent + 0.5f;
            data.colors[i + 1] = data.positions[i + 1] / extent + 0.5f;
            data.colors[i + 2] = data.positions[i + 2] / extent + 0.5f;
        }

        return data;
    }

    /**
     * 使用 threepp 随机数生成 points 示例点云数据。
     *
     * @param pointCount 要生成的点数量。
     * @param extent 点云每个坐标轴的分布宽度。
     * @return 每个点三个 float 的 position/color buffer。
     */
    inline PointCloudData makeRandomPointCloudData(int pointCount, float extent) {

        return makePointCloudData(pointCount, extent, [] {
            return math::randFloat();
        });
    }

    /**
     * 设置 points 示例的可见点数量。
     *
     * @param geometry 要更新的几何体。
     * @param pointCount 请求显示的点数量。
     * @param maxPointCount buffer 中可用的最大点数量。
     */
    inline void setActivePointCount(BufferGeometry& geometry, int pointCount, int maxPointCount) {

        geometry.setDrawRange(0, std::clamp(pointCount, 0, maxPointCount));
    }

}// namespace threepp::examples::points

#endif//THREEPP_EXAMPLES_OBJECTS_POINTSEXAMPLEDATA_HPP
