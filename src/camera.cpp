#include <camera.hpp>

Camera::Camera() {
    position = glm::vec3(0.f);
    eulerAngles = glm::vec3(0.f);
    nearPlane = 0.f;
    aspectRatio = 0.f;
    fov = 0.f;
}

Camera::Camera(glm::vec3 pos, glm::vec3 rot, float near, float aspect, float fov) {
    position = pos;
    eulerAngles = rot;
    nearPlane = near;
    aspectRatio = aspect;
    this->fov = fov;
}