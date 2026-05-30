#include "SnakeScene.hpp"

#include "threepp/canvas/Monitor.hpp"
#include "threepp/renderers/Renderer.hpp"

int main() {

    SnakeGame game(10);

    GlfwWindow canvas("Snake (Metal)", {{"clientAPI", "Metal"}});
    int height = monitor::monitorSize().height() / 2;
    canvas.setSize({height, height});
    auto renderer = Renderer::create(canvas, Backend::Metal);
    renderer->autoClear = false;

    auto scene = SnakeScene(game);
    canvas.addKeyListener(scene);

    OrthographicCamera camera(
            0, static_cast<float>(game.gridSize()),
            0, static_cast<float>(game.gridSize()));
    camera.position.z = 1;

    canvas.onWindowResize([&](WindowSize size) {
        camera.right = static_cast<float>(game.gridSize()) * size.aspect();
        camera.updateProjectionMatrix();
        renderer->setSize(size);
    });

    Clock clock;
    canvas.animate([&]() {
        const auto dt = clock.getDelta();

        if (game.isRunning()) {
            game.update(dt);
            scene.update();
        }

        renderer->clear();
        renderer->render(scene, camera);
    });
}
