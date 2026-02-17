#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitColor;

void main() {
    // Sky gradient
    vec3 rayDir = gl_WorldRayDirectionEXT;
    float sky = max(rayDir.y, 0.0);
    hitColor = mix(vec3(0.15, 0.15, 0.2), vec3(0.4, 0.6, 0.9), sky);
}
