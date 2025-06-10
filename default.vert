#version 460 core
#extension GL_ARB_gpu_shader_int64 : require

// -------------------------------------------------
// Uniforms
// -------------------------------------------------
uniform mat4 view;
uniform mat4 projection;

// how big each unit‐voxel is, in world‐space units
uniform float u_voxelScale;    // e.g. 0.1

// how many voxels along each axis in one chunk
// (THIS MUST EXACTLY MATCH “CS_P = 64” on your CPU side)
uniform ivec3 u_chunkSize;

// -------------------------------------------------
// Quad data SSBO (binding = 1)
// -------------------------------------------------
layout(std430, binding = 1) readonly buffer QuadBuffer {
    uint64_t quads[];
};

// -------------------------------------------------
// ChunkInfo SSBO (binding = 2)
//   [ uint chunkCount; (12 bytes padding) ChunkData data[chunkCount] ]
//   struct ChunkData { ivec4 offset; int first; int count; int padding; };
// -------------------------------------------------
struct ChunkData {
    ivec4 offset;   // (x,y,z,unused) in chunk‐space
    int   first;    // gl_VertexID start = ssboOffset * 6
    int   count;    // vertex‐count      = quadCount  * 6
    int   padding1;  // pad to 28 bytes
    int   padding2;  // pad to 32 bytes
};

layout(std430, binding = 2) buffer ChunkInfo {
    uint       chunkCount;  // number of loaded chunks
    ChunkData  data[];      // array of length = chunkCount
};

// Output to fragment shader
flat out vec3 v_Normal;
flat out float v_BlockType;

// Lookup tables for normals / face‐flipping
const vec3 normalLookup[6] = vec3[6](
    vec3( 0,  1,  0), // TOP
    vec3( 0, -1,  0), // BOTTOM
    vec3( 1,  0,  0), // RIGHT
    vec3(-1,  0,  0), // LEFT
    vec3( 0,  0,  1), // FRONT
    vec3( 0,  0, -1)  // BACK
);
const int flipLookup[6] = int[6](1, -1, -1, 1, -1, 1);

// Map each of the 6 output vertices back to one of the 4 quad corners
const int vertexToCornerMap[6] = int[6](
    0, // tri #1, vertex 0 → corner 0
    1, // tri #1, vertex 1 → corner 1
    2, // tri #1, vertex 2 → corner 2
    1, // tri #2, vertex 3 → corner 1 (re-use)
    3, // tri #2, vertex 4 → corner 3
    2  // tri #2, vertex 5 → corner 2 (re-use)
);

void main() {
    // ―――――――――――――――――――――――――――――――――――
    // 1) Fetch this chunk’s metadata
    // ―――――――――――――――――――――――――――――――――――
    int drawID = gl_DrawID;            // 0 .. (chunkCount-1)
    ChunkData cd = data[drawID];       // correctly 32-byte aligned

    ivec3 chunkCoords = cd.offset.xyz; // integer chunk coords (cx,cy,cz)
    int   first       = cd.first;      // starting gl_VertexID = ssboOffset*6
    int   cnt         = cd.count;      // number of vertices = quadCount*6


    // ―――――――――――――――――――――――――――――――――――
    // 2) Compute “local” vertex index [0 .. cnt−1]
    // ―――――――――――――――――――――――――――――――――――
    int localVID = int(gl_VertexID) - first;
    // localVID in [0 .. cnt−1]

    int vertexInQuadID = localVID % 6;      // which of the 6 vertices
    int corner         = vertexToCornerMap[vertexInQuadID];

    // ―――――――――――――――――――――――――――――――――――
    // 3) Read packed‐quad from the correct SSBO slot
    // ―――――――――――――――――――――――――――――――――――
    int quadIdx  = localVID / 6;            // which quad within this chunk
    int baseQuad = first / 6;               // SSBO “slot” for chunk’s first quad
    uint64_t q   = quads[ baseQuad + quadIdx ];

    int x         = int((q >>  0) & 0x3Fu);
    int y         = int((q >>  6) & 0x3Fu);
    int z         = int((q >> 12) & 0x3Fu);
    int w         = int((q >> 18) & 0x3Fu);
    int h         = int((q >> 24) & 0x3Fu);
    int type      = int((q >> 32) & 0xFFu);
    int normal_id = int((q >> 40) & 0x7u);

    // ―――――――――――――――――――――――――――――――――――
    // 4) Build the base in-chunk corner position
    // ―――――――――――――――――――――――――――――――――――
    vec3 baseVertexPos = vec3(x, y, z) * u_voxelScale;

    uint wDir = (normal_id & 2u) >> 1;
    uint hDir = 2u - (normal_id >> 2u);

    int wMod = corner >> 1;  // (0 or 1)
    int hMod = corner & 1;   // (0 or 1)

    vec3 finalVertexPos = baseVertexPos;
    finalVertexPos[wDir] += (float(w) * u_voxelScale)
                             * float(wMod)
                             * float(flipLookup[normal_id]);
    finalVertexPos[hDir] += (float(h) * u_voxelScale) * float(hMod);

    v_Normal    = normalLookup[normal_id];
    v_BlockType = float(type);

    // ―――――――――――――――――――――――――――――――――――
    // 5) Convert "in-chunk" → "world" using EXACT chunkSize = (64,64,64)
    // ―――――――――――――――――――――――――――――――――――
    vec3 chunkOriginVoxels = vec3(chunkCoords) * vec3(u_chunkSize);
    vec3 chunkWorldOrigin  = chunkOriginVoxels * u_voxelScale;

    vec3 world = finalVertexPos + chunkWorldOrigin;

    float faceOffset = 0.0007 * u_voxelScale;
    world += v_Normal * faceOffset * float(wMod * 2 - 1);
    world += v_Normal * faceOffset * float(hMod * 2 - 1);

    

    
    
    //if (chunkCoords != ivec3(0)) {
        gl_Position = projection * view * vec4(world, 1.0);
    //} else {
        //gl_Position = vec4(0);
    //}
    
    

    //gl_Position = projection * view * vec4(world + vec3(cd.offset.xyz) * 10.0, 1.0);

}
