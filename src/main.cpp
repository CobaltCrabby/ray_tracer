#include <render_context.h>
#include <renderer.h>
#include <camera.h>

#include <iostream>

int main() {
    RenderContext renderContext;
    Renderer renderer = Renderer(&renderContext);
    Camera cam = Camera(glm::vec3(0.f), glm::vec3(0.f), 0.1f, (float) renderContext.windowExtent.width / renderContext.windowExtent.height, 50.f);

    while (false) {
        renderer.render();
    }

    return 0;
}