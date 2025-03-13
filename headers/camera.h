#pragma once

#include <glm/glm.hpp>

class Camera {
    public:
        glm::vec3 position;
        glm::vec3 eulerAngles;
        float nearPlane;
        float aspectRatio;
        float fov;

        Camera();
        Camera(glm::vec3 pos, glm::vec3 rot, float near, float aspect, float fov);
};