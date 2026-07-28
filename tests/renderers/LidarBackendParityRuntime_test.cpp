#include "threepp/threepp.hpp"

#include "threepp/helpers/LidarSensor.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

using namespace threepp;

int main(int argc, char** argv) {
    const std::string_view backend = argc > 1 ? argv[1] : "vulkan";
    Canvas canvas(Canvas::Parameters().title("LidarBackendParityRuntime_test").size({320, 180}).vsync(false));

    std::unique_ptr<Renderer> renderer;
    try {
        if (backend == "gl") {
            renderer = std::make_unique<GLRenderer>(canvas);
        } else {
            renderer = std::make_unique<VulkanRenderer>(canvas);
        }
    } catch (const std::exception& e) {
        std::printf("[skip] %.*s renderer unavailable: %s\n",
                    static_cast<int>(backend.size()), backend.data(), e.what());
        return 42;
    }

    Scene emptyScene;
    LidarSensor emptyLidar(64, 0.1f, 10.f);
    std::vector<LidarReturn> emptyCloud;
    emptyLidar.scan(*renderer, emptyScene, emptyCloud);

    Scene cubeScene;
    auto cubeMaterial = MeshBasicMaterial::create(
            MeshBasicMaterial::Params{}.color(Color::white).side(Side::Back));
    cubeScene.add(Mesh::create(BoxGeometry::create(6.f, 6.f, 6.f), cubeMaterial));
    LidarSensor cubeLidar(64, 0.1f, 10.f);
    std::vector<LidarReturn> cubeCloud;
    cubeLidar.scan(*renderer, cubeScene, cubeCloud);
    float cubeMaxError = 0.f;
    for (const auto& hit : cubeCloud) {
        cubeMaxError = std::max(cubeMaxError,
                                std::abs(std::max({std::abs(hit.position.x),
                                                   std::abs(hit.position.y),
                                                   std::abs(hit.position.z)}) -
                                         3.f));
    }
    std::printf("cubePoints=%zu cubeMaxError=%.6f\n", cubeCloud.size(), cubeMaxError);

    Scene groundScene;
    groundScene.add(Mesh::create(
            BoxGeometry::create(30.f, 0.2f, 30.f),
            MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white))));
    LidarSensor groundLidar(128, 0.5f, 20.f);
    groundLidar.position.set(0.f, 2.f, 0.f);
    std::vector<LidarReturn> groundCloud;
    groundLidar.scan(*renderer, groundScene, groundCloud);
    std::size_t groundOutliers = 0;
    std::size_t beyondFar = 0;
    std::size_t beyond25 = 0;
    float maxDistance = 0.f;
    Vector3 maxPoint;
    for (const auto& hit : groundCloud) {
        const auto& p = hit.position;
        const bool inside = std::abs(p.x) <= 15.01f &&
                            p.y >= -0.11f && p.y <= 0.11f &&
                            std::abs(p.z) <= 15.01f;
        const auto surfaceError = std::min({std::abs(std::abs(p.x) - 15.f),
                                            std::abs(std::abs(p.y) - 0.1f),
                                            std::abs(std::abs(p.z) - 15.f)});
        groundOutliers += !inside || surfaceError > 0.01f;
        beyondFar += hit.distance > groundLidar.far();
        beyond25 += hit.distance > 25.f;
        if (hit.distance > maxDistance) {
            maxDistance = hit.distance;
            maxPoint.copy(hit.position);
        }
    }
    std::printf("groundPoints=%zu groundOutliers=%zu beyondFar=%zu beyond25=%zu maxDistance=%.6f maxPoint=(%.6f,%.6f,%.6f)\n",
                groundCloud.size(), groundOutliers, beyondFar, beyond25, maxDistance,
                maxPoint.x, maxPoint.y, maxPoint.z);

    Scene scene;
    auto plane = Mesh::create(
            PlaneGeometry::create(6.f, 6.f),
            MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white)));
    plane->position.z = -3.f;
    scene.add(plane);
    auto upperPlane = Mesh::create(
            PlaneGeometry::create(1.f, 1.f),
            MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white)));
    upperPlane->position.set(0.f, 1.f, -2.5f);
    scene.add(upperPlane);

    LidarSensor lidar(64, 0.1f, 10.f);
    lidar.rangeNoise = 0.f;
    std::vector<LidarReturn> cloud;
    lidar.scan(*renderer, scene, cloud);

    double sum = 0.0;
    double sqErrorSum = 0.0;
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    double upperYSum = 0.0;
    std::size_t upperCount = 0;
    for (const auto& hit : cloud) {
        sum += hit.position.z;
        const auto error = static_cast<double>(hit.position.z + 3.f);
        sqErrorSum += error * error;
        minZ = std::min(minZ, hit.position.z);
        maxZ = std::max(maxZ, hit.position.z);
        if (hit.position.z > -2.75f) {
            upperYSum += hit.position.y;
            ++upperCount;
        }
    }
    const auto mean = cloud.empty() ? 0.0 : sum / static_cast<double>(cloud.size());
    const auto rmsError = cloud.empty() ? 0.0 : std::sqrt(sqErrorSum / static_cast<double>(cloud.size()));
    const auto upperYMean = upperCount == 0 ? 0.0 : upperYSum / static_cast<double>(upperCount);
    std::printf("backend=%.*s emptyPoints=%zu points=%zu zMin=%.6f zMax=%.6f zMean=%.6f zRmsError=%.6f upperCount=%zu upperYMean=%.6f\n",
                static_cast<int>(backend.size()), backend.data(), emptyCloud.size(), cloud.size(), minZ, maxZ, mean, rmsError,
                upperCount, upperYMean);
    return emptyCloud.empty() && cubeCloud.size() == 6u * 64u * 64u && cubeMaxError < 0.1f &&
                   beyond25 == 0 && cloud.size() >= 4000 &&
                   upperCount >= 100 && upperYMean > 0.4
           ? 0
           : 1;
}
