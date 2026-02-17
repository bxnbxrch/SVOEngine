#include "vox/scene/Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace vox {

Camera::Camera() {
    m_startTime = std::chrono::high_resolution_clock::now();
    m_lastUpdate = m_startTime;
}

void Camera::adjustDistance(float delta) {
    const float GRID_SIZE = 256.0f;
    float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
    float minDist = halfDiag * MIN_DISTANCE_FACTOR;
    m_distance = glm::max(m_distance + delta, minDist);
}

void Camera::adjustYaw(float delta) {
    m_yaw += delta;
}

void Camera::adjustPitch(float delta) {
    m_pitch += delta;
    m_pitch = glm::clamp(m_pitch, -glm::pi<float>(), glm::pi<float>());
}

void Camera::togglePause() {
    m_paused = !m_paused;
    if (m_paused) {
        m_pausedTime = time();
    }
}

float Camera::time() const {
    if (m_paused) return m_pausedTime;
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<float>(now - m_startTime).count();
}

void Camera::updateTime() {
    m_lastUpdate = std::chrono::high_resolution_clock::now();
}

glm::vec3 Camera::getWorldPosition() const {
    glm::vec3 target(GRID_SIZE * 0.5f);
    
    float cy = cos(m_yaw);
    float sy = sin(m_yaw);
    float cp = cos(m_pitch);
    float sp = sin(m_pitch);
    
    return target + m_distance * glm::vec3(cy * cp, sp, sy * cp);
}

glm::mat4 Camera::getViewMatrix() const {
    glm::vec3 pos = getWorldPosition();
    glm::vec3 target(GRID_SIZE * 0.5f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    
    return glm::lookAt(pos, target, up);
}

glm::mat4 Camera::getProjectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(m_fov), aspect, 0.1f, 10000.0f);
}

float Camera::getMinDistance() const {
    float halfDiag = std::sqrt(3.0f) * (GRID_SIZE * 0.5f);
    return halfDiag * MIN_DISTANCE_FACTOR;
}

} // namespace vox
