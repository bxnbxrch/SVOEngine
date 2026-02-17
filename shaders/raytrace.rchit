#version 460
#extension GL_EXT_ray_tracing : require

layout(binding = 1, set = 0, std430) readonly buffer NodesBuffer {
    uint nodes[];
};

layout(binding = 2, set = 0, std430) readonly buffer ColorsBuffer {
    uint colors[];
};

layout(push_constant) uniform PushConstants {
    float time;
    uint debugMask;
    float distance;
    float yaw;
    float pitch;
    float fov;
} pc;

layout(location = 0) rayPayloadInEXT vec3 hitColor;

const uint LEAF_BIT = 0x80000000u;
const uint OCTREE_DEPTH = 8u;
const float GRID_SIZE = 256.0;

vec4 unpackColor(uint packed) {
    float r = float((packed >> 16) & 0xFFu) / 255.0;
    float g = float((packed >> 8)  & 0xFFu) / 255.0;
    float b = float(packed         & 0xFFu) / 255.0;
    return vec4(r, g, b, 1.0);
}

vec2 intersectAABB(vec3 origin, vec3 invDir, vec3 boxMin, vec3 boxMax) {
    vec3 t0 = (boxMin - origin) * invDir;
    vec3 t1 = (boxMax - origin) * invDir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tNear = max(max(tmin.x, tmin.y), tmin.z);
    float tFar  = min(min(tmax.x, tmax.y), tmax.z);
    return vec2(tNear, tFar);
}

vec3 computeAABBNormal(vec3 hitPoint, vec3 boxMin, vec3 boxMax) {
    const float eps = 0.0001;
    vec3 center = (boxMin + boxMax) * 0.5;
    vec3 size = (boxMax - boxMin) * 0.5;
    vec3 localPos = (hitPoint - center) / (size + eps);
    
    vec3 absPos = abs(localPos);
    vec3 normal = vec3(0.0);
    
    if (absPos.x >= absPos.y && absPos.x >= absPos.z) {
        normal = vec3(sign(localPos.x), 0.0, 0.0);
    } else if (absPos.y >= absPos.z) {
        normal = vec3(0.0, sign(localPos.y), 0.0);
    } else {
        normal = vec3(0.0, 0.0, sign(localPos.z));
    }
    
    return normalize(normal);
}

void main() {
    vec3 origin = gl_WorldRayOriginEXT;
    vec3 direction = gl_WorldRayDirectionEXT;
    
    direction = normalize(direction);
    vec3 invDir = 1.0 / max(abs(direction), vec3(1e-8)) * sign(direction);
    
    vec2 tRoot = intersectAABB(origin, invDir, vec3(0.0), vec3(GRID_SIZE));
    if (tRoot.x > tRoot.y || tRoot.y < 0.0) {
        hitColor = vec3(0.0);
        return;
    }
    
    float t = max(tRoot.x, 0.0) + 0.0001;
    
    // Octree traversal
    for (int iter = 0; iter < 256; ++iter) {
        vec3 pos = origin + direction * t;
        
        if (pos.x < 0.0 || pos.x >= GRID_SIZE ||
            pos.y < 0.0 || pos.y >= GRID_SIZE ||
            pos.z < 0.0 || pos.z >= GRID_SIZE) {
            break;
        }
        
        pos = clamp(pos, vec3(0.0), vec3(GRID_SIZE - 0.0001));
        
        uint nodeIdx = 0;
        vec3 nodeMin = vec3(0.0);
        float nodeSize = GRID_SIZE;
        
        for (uint depth = 0u; depth < OCTREE_DEPTH; ++depth) {
            if (nodeIdx >= nodes.length()) break;
            uint nodeData = nodes[nodeIdx];
            
            if ((nodeData & LEAF_BIT) != 0u) {
                uint colorIdx = nodeData & 0x7FFFFFFFu;
                if (colorIdx < colors.length()) {
                    vec3 nodeMax = nodeMin + vec3(nodeSize);
                    vec2 tVoxel = intersectAABB(origin, invDir, nodeMin, nodeMax);
                    vec3 hitPoint = origin + direction * max(tVoxel.x, 0.0);
                    
                    vec4 color = unpackColor(colors[colorIdx]);
                    vec3 normal = computeAABBNormal(hitPoint, nodeMin, nodeMax);
                    
                    // Simple lighting
                    vec3 keyLight = normalize(vec3(0.6, 0.8, 0.4));
                    vec3 fillLight = normalize(vec3(-0.3, -0.5, -0.2));
                    
                    float keyDiffuse = max(dot(normal, keyLight), 0.0);
                    float fillDiffuse = max(dot(normal, fillLight), 0.0);
                    float ambient = 0.3;
                    
                    float lighting = ambient + keyDiffuse * 0.6 + fillDiffuse * 0.2;
                    lighting = clamp(lighting, 0.0, 1.0);
                    
                    hitColor = color.rgb * lighting;
                    return;
                }
                hitColor = vec3(0.0);
                return;
            }
            
            uint childPtr = nodeData & 0x7FFFFFFFu;
            if (childPtr == 0u) break;
            
            float halfSize = nodeSize * 0.5;
            vec3 mid = nodeMin + vec3(halfSize);
            
            uint childIdx = 0u;
            vec3 childMin = nodeMin;
            
            if (pos.x >= mid.x) { childIdx += 4u; childMin.x = mid.x; }
            if (pos.y >= mid.y) { childIdx += 2u; childMin.y = mid.y; }
            if (pos.z >= mid.z) { childIdx += 1u; childMin.z = mid.z; }
            
            nodeIdx = childPtr + childIdx;
            nodeMin = childMin;
            nodeSize = halfSize;
        }
        
        vec3 nodeMax = nodeMin + vec3(nodeSize);
        vec2 tNode = intersectAABB(origin, invDir, nodeMin, nodeMax);
        
        float epsilon = max(0.0001, nodeSize * 0.0001);
        t = tNode.y + epsilon;
    }
    
    hitColor = vec3(0.0);
}

