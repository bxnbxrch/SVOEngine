#version 460
#extension GL_EXT_ray_tracing : require

struct Payload {
    vec3 albedo;
    vec3 normal;
    vec3 position;
    float emissive;
    uint hit;
};

layout(location = 0) rayPayloadInEXT Payload payload;

void main() {
    payload.hit = 0u;
}
