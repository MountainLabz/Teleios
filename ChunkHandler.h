#pragma once
#ifndef CHUNK_HANDLER_H
#define CHUNK_HANDLER_H

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono> // For high-resolution timer
#include <glm/vec3.hpp>
#include <glad/glad.h>
#include "UniversalPool.h"
#include <glm/ext/vector_int4.hpp>
#include <random>  // For random number generation
#include <glm/gtx/norm.hpp>  // For glm::distance, glm::distance2, etc. (often needed)
#include "FastNoiseLite.h"
#include "mesher.h"
#include "ChunkBufferManager.h"



// ----------------------------------------------------------------------------
// Base SDF Edit Interface
// ----------------------------------------------------------------------------
struct ISDFEdit {
    // Pure virtual function to get the signed distance from a world point to the SDF shape
    virtual float getSignedDistance(glm::vec3 point) const = 0;
    // Pure virtual function to get the material type for the SDF shape
    virtual uint8_t getMaterial() const = 0;

    // NEW: Pure virtual function to get the approximate world-space bounding box of the SDF shape.
    // This is crucial for the generic `addSDFEditAtWorldPos` to determine affected chunks.
    // Returns a pair of glm::vec3: first is min, second is max.
    virtual std::pair<glm::vec3, glm::vec3> getApproximateWorldBounds() const = 0;

    // NEW: Pure virtual function to clone the object polymorphically.
    // This is necessary because addSDFEditAtWorldPos needs to make copies
    // of the ISDFEdit object for each chunk it affects.
    virtual std::unique_ptr<ISDFEdit> clone() const = 0;

    // Virtual destructor for proper polymorphic deletion
    virtual ~ISDFEdit() = default;
};

// ----------------------------------------------------------------------------
// SDF Sphere Edit Structure
// Inherits from ISDFEdit
// ----------------------------------------------------------------------------
struct SDFSphereEdit : public ISDFEdit {
    glm::vec3 center;  // World coordinates of the sphere's center
    float radius;      // Radius of the sphere
    uint8_t material;  // Material type (0 for air, non-zero for solid)

    // Constructor
    SDFSphereEdit(glm::vec3 c, float r, uint8_t m) : center(c), radius(r), material(m) {}

    // Implement getSignedDistance for sphere
    float getSignedDistance(glm::vec3 point) const override {
        return glm::distance(point, center) - radius;
    }

    // Implement getMaterial for sphere
    uint8_t getMaterial() const override {
        return material;
    }

    // Implement getApproximateWorldBounds for sphere
    std::pair<glm::vec3, glm::vec3> getApproximateWorldBounds() const override {
        glm::vec3 worldMin = center - glm::vec3(radius);
        glm::vec3 worldMax = center + glm::vec3(radius);
        return { worldMin, worldMax };
    }

    // Implement clone for SDFSphereEdit
    std::unique_ptr<ISDFEdit> clone() const override {
        return std::make_unique<SDFSphereEdit>(*this); // Use copy constructor
    }
};

// ----------------------------------------------------------------------------
// SDF Cube Edit Structure
// Inherits from ISDFEdit
// ----------------------------------------------------------------------------
struct SDFCubeEdit : public ISDFEdit {
    glm::vec3 center;     // World coordinates of the cube's center
    glm::vec3 halfExtents;  // Half-extents of the cube along each axis
    uint8_t material;     // Material type (0 for air, non-zero for solid)

    // Constructor
    SDFCubeEdit(glm::vec3 c, glm::vec3 he, uint8_t m) : center(c), halfExtents(he), material(m) {}

    // Implement getSignedDistance for cube (based on standard SDF for AABB)
    float getSignedDistance(glm::vec3 p) const override {
        glm::vec3 q = glm::abs(p - center) - halfExtents;
        return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
    }

    // Implement getMaterial for cube
    uint8_t getMaterial() const override {
        return material;
    }

    // Implement getApproximateWorldBounds for cube
    std::pair<glm::vec3, glm::vec3> getApproximateWorldBounds() const override {
        glm::vec3 worldMin = center - halfExtents;
        glm::vec3 worldMax = center + halfExtents;
        return { worldMin, worldMax };
    }

    // Implement clone for SDFCubeEdit
    std::unique_ptr<ISDFEdit> clone() const override {
        return std::make_unique<SDFCubeEdit>(*this); // Use copy constructor
    }
};



// Chunks


// ----------------------------------------------------------------------------
// Per-chunk metadata: stores SSBO-offset + quad-count + chunk-coords
// ----------------------------------------------------------------------------
struct ChunkMetadata {
    glm::ivec3 chunkCoords;
    int poolNodeID;
    uint32_t quadCount;
    uint32_t ssboSlotOffset;
    std::vector<std::unique_ptr<ISDFEdit>> sdfEdits; // Now stores unique_ptrs to base interface
};

// ----------------------------------------------------------------------------
// Hash & equality for glm::ivec3 so we can use it as key in std::unordered_map
// ----------------------------------------------------------------------------
struct IVec3Hash {
    size_t operator()(glm::ivec3 const& v) const noexcept {
        uint64_t x = static_cast<uint64_t>(v.x);
        uint64_t y = static_cast<uint64_t>(v.y);
        uint64_t z = static_cast<uint64_t>(v.z);
        uint64_t h = (x * 73856093u) ^ (y * 19349663u) ^ (z * 83492791u);
        return static_cast<size_t>(h);
    }
};
struct IVec3Eq {
    bool operator()(glm::ivec3 const& a, glm::ivec3 const& b) const noexcept {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

// ----------------------------------------------------------------------------
// Struct for GPU-side per-chunk metadata
// Must match this layout exactly in GLSL:
//
// struct ChunkData {
//     ivec3 offset; // chunk coordinates
//     int   first;  // first vertex index in draw
//     int   count;  // vertex count for draw
// };
//
// layout(std430, binding = 2) buffer ChunkInfo {
//     uint       chunkCount;
//     ChunkData  data[];
// };
// ----------------------------------------------------------------------------
struct ChunkData {
    glm::ivec4 offset;  // Change to glm::ivec4 to match GLSL's 16-byte alignment
    int        first;
    int        count;
    int        padding1; // Add padding to make struct 32 bytes
    int        padding2; // Add padding to make struct 32 bytes
};

// ----------------------------------------------------------------------------
// ChunkHandler
//
// - Manages one large SSBO of uint64_t-encoded quads.
// - Sub-allocates per-chunk via UniversalPool<uint64_t> (1 slot = 1 quad).
// - Tracks per-chunk metadata (coords, ssboOffset, quadCount, poolNodeID).
// - Prepares & uploads a second SSBO of per-chunk metadata for MultiDraw.
// - Exposes methods to retrieve CPU-side arrays for firsts and counts.
// ----------------------------------------------------------------------------
class ChunkHandler {
public:
    
    static FastNoiseLite sharedNoise; // This is now non-static in concept if passed by ref, but still declared here.
    static bool       noiseInitialized;
    static const float voxel_scale; // Declaration in .h


    ChunkHandler();
    ~ChunkHandler();

    // Initialize CPU pool and GPU ring buffer
    bool init(uint32_t maxTotalQuads);
    void destroy();

    // Add or update a chunk's quad data
    bool addOrUpdateChunk(const glm::ivec3& coords,
        const std::vector<uint64_t>& quads);
    void removeChunk(const glm::ivec3& coords);
    void clearAll();

    // Bind SSBOs for rendering
    void bindQuadsSSBO(GLuint bindingPoint) const;
    void bindMetadataSSBO(GLuint bindingPoint);

    // Info
    size_t getLoadedChunkCount() const;
    size_t retrieveFirstsAndCounts(std::vector<GLint>& firsts,
        std::vector<GLsizei>& counts) const;











    // Helper to generate voxel data, now accepts the base ISDFEdit vector
    void generateVoxelsWithSDF(
        std::vector<uint8_t>& voxels,
        int cs_p_val,
        int cs_p2_val,
        int cs_p3_val,
        glm::ivec3 chunkOffsetInVoxels,
        FastNoiseLite& noise,
        const std::vector<std::unique_ptr<ISDFEdit>>& sdfEdits // Changed to base class pointer
    );



    bool addSDFEditAtWorldPos(const ISDFEdit& edit, int chunkSizeInVoxels, FastNoiseLite& noise);


    bool addSDFEditToChunk(glm::ivec3 chunkCoords, std::unique_ptr<ISDFEdit> edit,
        int chunkSizeInVoxels, FastNoiseLite& noise);
    // NEW: Add SDF modification to a specific chunk and regenerate its mesh
    

    // NEW: Add SDF modification based on world position, converting to a single chunk
    /*bool addSDFSphereEditAtWorldPos(glm::vec3 worldPos, float radius, uint8_t material,
        int chunkSizeInVoxels, FastNoiseLite& noise);*/

    // Helper to generate mesh data from voxels, now accepts the base ISDFEdit vector
    MeshData generateVoxelMesh(
        int option,
        glm::ivec3 chunkOffsetInVoxels,
        FastNoiseLite& noise,
        const std::vector<std::unique_ptr<ISDFEdit>>& sdfEdits // Changed to base class pointer
    );

    void profileOpaqueMaskGeneration(
        const std::vector<uint8_t>& voxels,
        int CS_P,
        int CS_P2,
        MeshData& meshData
    );

    MeshData generateMeshData(const std::vector<uint8_t>& voxels);

private:
    bool prepareChunkMesh(const glm::ivec3& coords);
    void prepareMetadataBuffer();

    UniversalPool<uint64_t, true>* pool = nullptr;
    ChunkBufferManager bufferMgr;
    GLuint metadataSSBO = 0;

    std::unordered_map<glm::ivec3, ChunkMetadata, IVec3Hash, IVec3Eq> chunkMap;
    std::vector<ChunkData> tempChunkData;
};




#endif // CHUNK_HANDLER_H




/*
Measure once, batch updates.
Instead of mapping / unmapping your SSBO per - chunk, accumulate all modified quad arrays in a single contiguous upload and do one glMapBufferRange(or glBufferSubData) call.

Avoid full‐volume regen when editing a small sphere.
Right now generateVoxelsWithSDF always loops over all 64³ voxels.Instead, only update voxels inside the sphere’s bounding box(and its immediate neighbors), then incrementally remesh just that sub‐region.

Reuse your voxel and vertex buffers.

Keep a single, preallocated std::vector<uint8_t> voxels(64 * 64 * 64) and just fill() instead of reallocating each time.

Reserve capacity on your BM_VECTOR<uint64_t> so it never needs to new[] or shrink_to_fit() on every mesh.

Cut down on unordered_map overhead.
If you’re frequently doing chunkMap.find() + erase / insert, consider a flat hash or even a 3D array / hash - grid for small worlds.That can shave tens of microseconds per lookup.
*/