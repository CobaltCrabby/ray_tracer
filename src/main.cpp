#include "SDL_events.h"
#include <render_context.hpp>
#include <renderer.hpp>
#include <camera.hpp>

#include <iostream>

int main() {
    RenderContext renderContext;
    Renderer renderer = Renderer(&renderContext);
    Camera cam = Camera(glm::vec3(0.f), glm::vec3(0.f), 0.1f, (float) renderContext.windowExtent.width / renderContext.windowExtent.height, 50.f);

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            quit = event.type == SDL_QUIT;
        }

        renderer.render();
    }

    return 0;
}