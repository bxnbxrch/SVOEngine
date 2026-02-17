#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 hitColor;

void main() {
    // Not used in our octree-traversal approach
    hitColor = vec3(1.0, 1.0, 1.0);
}

