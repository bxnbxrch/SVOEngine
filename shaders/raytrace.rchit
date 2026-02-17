#version 460
#extension GL_EXT_ray_tracing : require

layout(binding = 1, set = 0, std430) readonly buffer NodesBuffer {
    uint nodes[];
};

layout(binding = 2, set = 0, std430) readonly buffer ColorsBuffer {
    uint colors[];
};

layout(std140, binding = 5, set = 0) uniform ShaderParams {
    vec4 bgColor;
    vec4 keyDir;
    vec4 fillDir;
    vec4 params0; // ambient, emissiveSelf, emissiveDirect, attenFactor
    vec4 params1; // attenBias, maxLights, debugMode, ddaEps
    vec4 params2; // ddaEpsScale, reserved, reserved, reserved
};

layout(push_constant) uniform PushConstants {
    float time;
    uint debugMask;
    float distance;
    float yaw;
    float pitch;
    float fov;
    float gridSize;
    uint pad;
} pc;

struct Payload {
    vec3 albedo;
    vec3 normal;
    vec3 position;
    float emissive;
    uint hit;
};

layout(location = 0) rayPayloadInEXT Payload payload;

const uint LEAF_BIT = 0x80000000u;
const uint HOMOGENEOUS_BIT = 0x40000000u;
const uint OCTREE_DEPTH = 11u;

vec4 unpackColor(uint packed) {
    float e = float((packed >> 24) & 0xFFu) / 255.0;
    float r = float((packed >> 16) & 0xFFu) / 255.0;
    float g = float((packed >> 8)  & 0xFFu) / 255.0;
    float b = float(packed         & 0xFFu) / 255.0;
    return vec4(r, g, b, e);
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

bool isFiniteVec2(vec2 v) {
    return !any(isnan(v)) && !any(isinf(v));
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
    
    vec2 tRoot = intersectAABB(origin, invDir, vec3(0.0), vec3(pc.gridSize));
    if (tRoot.x > tRoot.y || tRoot.y < 0.0) {
        payload.hit = 0u;
        return;
    }
    
    float tEps = params1.w;
    float t = max(tRoot.x, 0.0) + tEps;
    
    // Octree traversal
    for (int iter = 0; iter < 256; ++iter) {
        vec3 pos = origin + direction * t;
        
        if (pos.x < 0.0 || pos.x >= pc.gridSize ||
            pos.y < 0.0 || pos.y >= pc.gridSize ||
            pos.z < 0.0 || pos.z >= pc.gridSize) {
            break;
        }
        
        pos = clamp(pos, vec3(0.0), vec3(pc.gridSize - tEps));
        
        uint nodeIdx = 0;
        vec3 nodeMin = vec3(0.0);
        float nodeSize = pc.gridSize;
        
        for (uint depth = 0u; depth < OCTREE_DEPTH; ++depth) {
            if (nodeIdx >= nodes.length()) break;
            uint nodeData = nodes[nodeIdx];
            
            if ((nodeData & LEAF_BIT) != 0u) {
                uint colorIdx = nodeData & 0x3FFFFFFFu;
                if (colorIdx < colors.length()) {
                    vec3 nodeMax = nodeMin + vec3(nodeSize);
                    vec2 tVoxel = intersectAABB(origin, invDir, nodeMin, nodeMax);
                    if (!isFiniteVec2(tVoxel)) {
                        payload.hit = 0u;
                        return;
                    }
                    vec3 hitPoint = origin + direction * max(tVoxel.x, 0.0);
                    
                    vec4 color = unpackColor(colors[colorIdx]);
                    vec3 normal = computeAABBNormal(hitPoint, nodeMin, nodeMax);
                    
                    payload.albedo = color.rgb;
                    payload.normal = normal;
                    payload.position = hitPoint;
                    payload.emissive = color.a;
                    payload.hit = 1u;
                    return;
                }
                payload.hit = 0u;
                return;
            }
            
            // Skip old homogeneous check - compressed nodes are now leaves
            
            uint childPtr = nodeData & 0x3FFFFFFFu;
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
        if (!isFiniteVec2(tNode)) {
            break;
        }
        
        float epsilon = max(tEps, nodeSize * params2.x);
        float nextT = tNode.y + epsilon;
        if (nextT <= t) {
            nextT = t + epsilon;
        }
        t = nextT;
    }
    
    payload.hit = 0u;
}

