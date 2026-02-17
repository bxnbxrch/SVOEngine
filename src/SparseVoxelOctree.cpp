#include "vox/SparseVoxelOctree.h"
#include <queue>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>

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
    // Descend 7 levels (bits 7 down to 1), then mark bit-0 node as leaf
    for (int level = m_depth - 1; level > 0; --level) {  
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

bool SparseVoxelOctree::loadFromVoxFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open .vox file: " << filepath << std::endl;
        return false;
    }

    // Read header
    char magic[4];
    file.read(magic, 4);
    if (std::memcmp(magic, "VOX ", 4) != 0) {
        std::cerr << "Invalid .vox file magic" << std::endl;
        return false;
    }

    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), 4);
    std::cout << "Loading MagicaVoxel file version " << version << std::endl;

    // Read MAIN chunk
    char chunkId[4];
    file.read(chunkId, 4);
    if (std::memcmp(chunkId, "MAIN", 4) != 0) {
        std::cerr << "Expected MAIN chunk" << std::endl;
        return false;
    }

    uint32_t mainChunkSize, mainChildrenSize;
    file.read(reinterpret_cast<char*>(&mainChunkSize), 4);
    file.read(reinterpret_cast<char*>(&mainChildrenSize), 4);

    // Read SIZE chunk
    file.read(chunkId, 4);
    if (std::memcmp(chunkId, "SIZE", 4) != 0) {
        std::cerr << "Expected SIZE chunk" << std::endl;
        return false;
    }

    uint32_t sizeChunkSize, sizeChildrenSize;
    file.read(reinterpret_cast<char*>(&sizeChunkSize), 4);
    file.read(reinterpret_cast<char*>(&sizeChildrenSize), 4);

    uint32_t sizeX, sizeY, sizeZ;
    file.read(reinterpret_cast<char*>(&sizeX), 4);
    file.read(reinterpret_cast<char*>(&sizeY), 4);
    file.read(reinterpret_cast<char*>(&sizeZ), 4);

    std::cout << "Model size: " << sizeX << " x " << sizeY << " x " << sizeZ << std::endl;

    // Read XYZI chunk
    file.read(chunkId, 4);
    if (std::memcmp(chunkId, "XYZI", 4) != 0) {
        std::cerr << "Expected XYZI chunk" << std::endl;
        return false;
    }

    uint32_t xyziChunkSize, xyziChildrenSize;
    file.read(reinterpret_cast<char*>(&xyziChunkSize), 4);
    file.read(reinterpret_cast<char*>(&xyziChildrenSize), 4);

    uint32_t numVoxels;
    file.read(reinterpret_cast<char*>(&numVoxels), 4);
    std::cout << "Loading " << numVoxels << " voxels..." << std::endl;

    // Store voxel data temporarily
    struct VoxelData { uint8_t x, y, z, colorIndex; };
    std::vector<VoxelData> voxelData(numVoxels);
    file.read(reinterpret_cast<char*>(voxelData.data()), numVoxels * 4);

    // Search for RGBA palette chunk (may have other chunks in between)
    std::vector<uint32_t> palette(256);
    bool paletteLoaded = false;
    
    while (!file.eof()) {
        char searchChunkId[4];
        file.read(searchChunkId, 4);
        if (file.eof()) break;
        
        if (std::memcmp(searchChunkId, "RGBA", 4) == 0) {
            uint32_t rgbaChunkSize, rgbaChildrenSize;
            file.read(reinterpret_cast<char*>(&rgbaChunkSize), 4);
            file.read(reinterpret_cast<char*>(&rgbaChildrenSize), 4);
            
            // Read 256 RGBA colors (each 4 bytes)
            for (int i = 0; i < 256; ++i) {
                uint8_t r, g, b, a;
                file.read(reinterpret_cast<char*>(&r), 1);
                file.read(reinterpret_cast<char*>(&g), 1);
                file.read(reinterpret_cast<char*>(&b), 1);
                file.read(reinterpret_cast<char*>(&a), 1);
                palette[i] = (r << 16) | (g << 8) | b;
            }
            paletteLoaded = true;
            std::cout << "Loaded RGBA palette" << std::endl;
            break;
        } else {
            // Skip this chunk
            uint32_t chunkSize, childrenSize;
            file.read(reinterpret_cast<char*>(&chunkSize), 4);
            file.read(reinterpret_cast<char*>(&childrenSize), 4);
            file.seekg(chunkSize + childrenSize, std::ios::cur);
        }
    }
    
    if (!paletteLoaded) {
        // Default palette if RGBA chunk not found
        for (int i = 0; i < 256; ++i) {
            uint8_t r = (i * 127) % 256;
            uint8_t g = (i * 191) % 256;
            uint8_t b = (i * 223) % 256;
            palette[i] = (r << 16) | (g << 8) | b;
        }
        std::cout << "Using default palette" << std::endl;
    }

    // Calculate offset to center the model in [0, 256)^3 grid
    uint32_t gridSize = 1u << m_depth; // 256
    int32_t offsetX = (gridSize - sizeX) / 2;
    int32_t offsetY = (gridSize - sizeY) / 2;
    int32_t offsetZ = (gridSize - sizeZ) / 2;

    // Read voxel data
    int voxelsAdded = 0;
    for (uint32_t i = 0; i < numVoxels; ++i) {
        uint8_t x = voxelData[i].x;
        uint8_t y = voxelData[i].y;
        uint8_t z = voxelData[i].z;
        uint8_t colorIndex = voxelData[i].colorIndex;

        // MagicaVoxel uses 1-based color indices (0 = empty), so use colorIndex-1
        if (colorIndex == 0) continue;  // Skip empty voxels

        // MagicaVoxel uses Z-up, convert to Y-up and center
        // Flip Z to fix upside-down issue
        int32_t voxX = x + offsetX;
        int32_t voxY = (sizeZ - 1 - z) + offsetZ;  // Z becomes Y, flipped
        int32_t voxZ = y + offsetY;  // Y becomes Z

        // Bounds check
        if (voxX >= 0 && voxX < (int32_t)gridSize &&
            voxY >= 0 && voxY < (int32_t)gridSize &&
            voxZ >= 0 && voxZ < (int32_t)gridSize) {
            
            uint32_t color = palette[colorIndex - 1];  // Adjust for 1-based indexing
            setVoxel({(uint32_t)voxX, (uint32_t)voxY, (uint32_t)voxZ}, color);
            voxelsAdded++;
        }
    }

    std::cout << "Successfully loaded " << filepath << " (" << voxelsAdded << " voxels added)" << std::endl;
    std::cout << "Octree has " << m_nodes.size() << " nodes and " << m_colors.size() << " colors" << std::endl;
    return true;
}

} // namespace vox
