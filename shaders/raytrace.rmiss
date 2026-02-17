#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitColor;

void main() {
    // Background color on miss to match swapchain clear
    hitColor = vec3(0.05, 0.05, 0.08);
}
