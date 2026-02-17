#include "vox/SparseVoxelOctree.h"
#include <queue>

namespace vox {

SparseVoxelOctree::SparseVoxelOctree(uint32_t depth) 
    : m_depth(depth) {
    // Allocate root node
    m_nodes.push_back({0});
}

void SparseVoxelOctree::setVoxel(glm::uvec3 pos, uint32_t color) {
    uint32_t colorIdx = (uint32_t)m_colors.size();
    m_colors.push_back(color);

    uint32_t nodeIdx = 0;
    for (uint32_t level = m_depth - 1; level-- > 0; ) {
        OctreeNode& node = m_nodes[nodeIdx];
        
        // Compute which child (0-7)
        uint32_t childIdx = ((pos.x >> level) & 1) * 4 + 
                            ((pos.y >> level) & 1) * 2 + 
                            ((pos.z >> level) & 1);

        // Extract children pointer
        uint32_t childPtr = node.data & 0x7FFFFFFFu;
        
        if (!childPtr) {
            // Create 8 empty children
            childPtr = (uint32_t)m_nodes.size();
            node.data = childPtr;
            for (int i = 0; i < 8; ++i) m_nodes.push_back({0});
        }

        nodeIdx = childPtr + childIdx;
    }

    // Leaf: store color index
    m_nodes[nodeIdx].data = OctreeNode::LEAF_BIT | colorIdx;
}

void SparseVoxelOctree::generateTestScene() {
    // Translate the small test clusters so they are centered inside the SVO
    glm::uvec3 base(120u, 120u, 120u);

    // Create a few test voxel blocks at different positions (all offset by base)
    // Red base block
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFF0000);

    // Green block to the side
    for (int x = 8; x < 12; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0x00FF00);

    // Blue block higher
    for (int x = 4; x < 8; ++x)
        for (int y = 8; y < 12; ++y)
            for (int z = 4; z < 8; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0x0000FF);

    // Yellow block
    for (int x = 0; x < 8; ++x)
        for (int y = 12; y < 16; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFFFF00);

    // Magenta pillar
    for (int x = 16; x < 20; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFF00FF);

    // Cyan tower
    for (int x = 20; x < 24; ++x)
        for (int y = 0; y < 20; ++y)
            for (int z = 0; z < 4; ++z)
                setVoxel({(uint32_t)x + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0x00FFFF);

    // Orange stairs
    for (int i = 0; i < 8; ++i)
        for (int x = 24; x < 28; ++x)
            for (int y = i * 2; y < i * 2 + 2; ++y)
                for (int z = 0; z < 4; ++z)
                    setVoxel({(uint32_t)(x + i) + base.x, (uint32_t)y + base.y, (uint32_t)z + base.z}, 0xFF8000);
}

} // namespace vox
