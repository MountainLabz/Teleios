#include<iostream>
#include<glad/glad.h>
#include<GLFW/glfw3.h>
#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#include<glm/gtc/type_ptr.hpp>
#include <chrono>   // For timing
#include <string.h> // For memset
#include <random>  // For random number generation

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"


#include"shaderClass.h"
#include"VAO.h"
#include"VBO.h"
#include"EBO.h"

#include "camera.h"
#include "FastNoiseLite.h"

//#define BM_IMPLEMENTATION
#include"mesher.h"
#include"ChunkHandler.h"


uint32_t window_size_x = 1600;
uint32_t window_size_y = 900;
const float voxel_scale = 0.1;

//// Vertices coordinates
//GLfloat vertices[] =
//{
//	-0.5f, -0.5f * float(sqrt(3)) / 3, 0.0f, // Lower left corner
//	0.5f, -0.5f * float(sqrt(3)) / 3, 0.0f, // Lower right corner
//	0.0f, 0.5f * float(sqrt(3)) * 2 / 3, 0.0f, // Upper corner
//	-0.5f / 2, 0.5f * float(sqrt(3)) / 6, 0.0f, // Inner left
//	0.5f / 2, 0.5f * float(sqrt(3)) / 6, 0.0f, // Inner right
//	0.0f, -0.5f * float(sqrt(3)) / 3, 0.0f // Inner down
//};

//// Indices for vertices order
//GLuint indices[] =
//{
//	0, 3, 5, // Lower left triangle
//	3, 2, 4, // Lower right triangle
//	5, 4, 1 // Upper triangle
//};


void generateHillyTerrain(std::vector<uint8_t>& voxels, int cs_p_val, int cs_p2_val, int cs_p3_val) {
    // Initialize FastNoiseLite for 2D noise
    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin); // Good for rolling hills

    // Use a random seed for reproducible terrain (removed fixed seed)
    std::random_device rd_noise;
    noise.SetSeed(123456); // Set a truly random seed // use rd_noise() for randomness

    noise.SetFrequency(0.001f); // Adjust for the scale of your hills
    noise.SetFractalType(FastNoiseLite::FractalType_FBm); // Add fractal for more detail
    noise.SetFractalOctaves(3); // Fewer octaves might be better for smoother hills
    noise.SetFractalLacunarity(2.0f);
    noise.SetFractalGain(0.5f);

    std::random_device rd;
    std::mt19937 gen(rd());
    // Changed to generate a wider range of solid voxel types (e.g., 1-7)
    std::uniform_int_distribution<> solidTypeDistrib(2, 3);

    // Determine the maximum height your terrain can reach (e.g., half the chunk height)
    const float maxHeight = cs_p_val / 2.0f;
    // Determine a base height (e.g., a quarter of the chunk height)
    const float baseHeight = cs_p_val / 4.0f;

    // Ensure the voxels vector is correctly sized and initialized to 0
    if (voxels.size() != cs_p3_val) {
        voxels.assign(cs_p3_val, 0); // Resize and fill with zeros if necessary
    }
    else {
        std::fill(voxels.begin(), voxels.end(), 0); // Just clear to zeros if already sized
    }

    // Loop for the inner (non-padded) region
    for (int z = 1; z < cs_p_val - 1; ++z) {
        for (int x = 1; x < cs_p_val - 1; ++x) { // Iterate x and z for the 2D noise
            // Get 2D noise value based on X and Z coordinates
            // Noise value is typically between -1.0 and 1.0
            float noiseValue = noise.GetNoise((float)x, (float)z);

            // Map noise value to a height in your voxel grid (Y-coordinate)
            // We want noiseValue of -1 to be lower and 1 to be higher.
            // Scale and offset noise to fit within a desirable Y range
            int terrainHeight = (int)(baseHeight + (noiseValue * maxHeight * 7.0f));

            // Clamp the terrain height to be within valid Y bounds of your chunk
            if (terrainHeight < 1) terrainHeight = 1;
            if (terrainHeight >= cs_p_val - 1) terrainHeight = cs_p_val - 2;

            // Fill voxels from the bottom (y=0) up to the calculated terrainHeight
            for (int y = 1; y < cs_p_val - 1; ++y) {
                if (y <= terrainHeight) {
                    // If it's below or at the terrain surface, make it solid
                    voxels[x + (z * cs_p_val) + (y * cs_p2_val)] = solidTypeDistrib(gen);
                }
                else {
                    // Otherwise, it's air
                    // voxels[x + (y * cs_p_val) + (z * cs_p2_val)] = 0; // Already initialized to 0
                }
            }
        }
    }
}

// Overload generateTerrain so that it takes a FastNoiseLite& instead of creating a new one:
void generateTerrain(
    std::vector<uint8_t>& voxels,
    int cs_p_val,     // 64
    int cs_p2_val,    // 4096 (64 * 64)
    int cs_p3_val,    // 64^3
    glm::ivec3 offset,// in **world-voxel** units (so offset.y puts you at the correct vertical level!)
    FastNoiseLite& noise
) {
    const int pad = 1;
    const int N = cs_p_val; // e.g. 64 (including padding)

    // 1) Ensure the voxel buffer is exactly N^3, zero-filled
    if (voxels.size() != static_cast<size_t>(cs_p3_val)) {
        voxels.assign(cs_p3_val, 0);
    }
    else {
        std::fill(voxels.begin(), voxels.end(), 0);
    }

    // 2) Prepare a random generator just for choosing “solid type” (3..3 here, but could be 2..3, etc.)
    std::random_device rd;
    std::mt19937       gen(rd());
    std::uniform_int_distribution<> solidTypeDistrib(3, 3);

    // 3) Precompute our “height mapping” constants
    const float maxHeightGlobal = static_cast<float>(N) / 2.0f; // e.g. 32.0 → in world-voxel units
    const float baseHeightGlobal = static_cast<float>(N) / 4.0f; // e.g. 16.0

    // 4) Loop over x,z in [0..63], *including* padding. For each column, compute a single global terrain height:
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            // a) Compute the **global X,Z** at which we sample Perlin:
            int worldX = offset.x + (x - pad); // when x=1 → worldX = offset.x + 0
            int worldZ = offset.z + (z - pad);

            // b) Sample the noise field (in [-1..+1]) at (worldX, worldZ)
            float noiseValue = noise.GetNoise(static_cast<float>(worldX),
                static_cast<float>(worldZ));

            // c) Map that noise into a **global Y** (=“terrain surface height in world voxels”).
            //    We choose:  globalHeight = base + (noise * max * gain)
            //
            //    Note: You had *10.0f* in your code. If you still want more “mountainous” variance,
            //    put that factor here. For example, noiseValue * (maxHeightGlobal * 10.0f).
            float terrainHeightF = baseHeightGlobal + (noiseValue * maxHeightGlobal * 2.0f);

            //    Clamp to an integer in [0 .. very large], but we’ll at least floor to an int:
            int terrainHeightWorld = static_cast<int>(std::floor(terrainHeightF));
            //    We *don’t* clamp to [1..N-2] here, because this is a **global** Y. 
            //    If terrainHeightWorld < 0, it just means “everything is above that terrain → chunk is air.”
            //    If terrainHeightWorld is huge, it means we fill entire chunk from bottom to top.
            //
            //    (If you want to clamp so that no terrain ever goes above, say, Y=255 or some world-height cap,
            //     do it here. Otherwise, let it be unbounded.)

            // d) Now fill vertically. For each local y ∈ [0..63], compute its **global** Y and compare:
            for (int y = 0; y < N; ++y) {
                // Compute “this local‐voxel’s world Y”:
                int worldY = offset.y + (y - pad);
                //
                // If worldY ≤ terrainHeightWorld, that voxel is solid. Otherwise, it remains air (0).
                // (If worldY < 0, it’s also “below the terrain” if terrainHeightWorld ≥0.)
                if (worldY <= terrainHeightWorld) {
                    // e) Flatten index = x + (y * cs_p_val) + (z * cs_p2_val)
                    size_t index = static_cast<size_t>(
                        z
                        + (x * cs_p_val)
                        + (y * cs_p2_val)
                        );
                    voxels[index] = static_cast<uint8_t>(solidTypeDistrib(gen));
                }
                // else leave as 0 (air)
            }
        }
    }
}


void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	//// --- ADD THESE DEBUG PRINTS ---
	//std::cout << "DEBUG: Raw Mouse X=" << xpos << ", Y=" << ypos << std::endl;
	//// --- END DEBUG PRINTS ---

	// Retrieve the Camera object from the window's user pointer
	Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
	if (!camera) return; // Defensive check if pointer is null

	// The Camera::ProcessMouseMovement function handles the internal 'firstMouse' logic.
    if (camera->mouseLocked == true) {
        camera->ProcessMouseMovement(xpos, ypos);
    }
	
}

// In Teleios.cpp, outside of main, typically near mouse_callback
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
    // You might also want to update the window_size_x and window_size_y global variables
    // if you use them consistently for projection matrix calculations etc.
    window_size_x = width;
    window_size_y = height;
}

// Corrected unpacking functions to match your getQuad packing logic:
inline uint8_t unpackQuadX(uint64_t quad) { return quad & 0x3F; } // Bits 0-5 (6 bits)
inline uint8_t unpackQuadY(uint64_t quad) { return (quad >> 6) & 0x3F; } // Bits 6-11 (6 bits)
inline uint8_t unpackQuadZ(uint64_t quad) { return (quad >> 12) & 0x3F; } // Bits 12-17 (6 bits)
inline uint8_t unpackQuadW(uint64_t quad) { return (quad >> 18) & 0x3F; } // Bits 18-23 (6 bits)
inline uint8_t unpackQuadH(uint64_t quad) { return (quad >> 24) & 0x3F; } // Bits 24-29 (6 bits)
inline uint8_t unpackQuadType(uint64_t quad) { return (quad >> 32) & 0xFF; } // Bits 32-39 (8 bits, as implied by normal_id starting at 40)
inline uint8_t unpackQuadFace(uint64_t quad) { return (quad >> 40) & 0x7; }  // Bits 40-42 (3 bits)

int main() {
    // --- GLFW / OpenGL init ---
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(window_size_x, window_size_y, "Teleios", NULL, NULL);
    if (!window) { std::cout << "failed to create window!" << std::endl; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_FALSE);
    glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_FALSE);
    glfwSwapInterval(1);
    gladLoadGL();
    glViewport(0, 0, window_size_x, window_size_y);
    glEnable(GL_DEPTH_TEST);

    Shader shaderProgram("default.vert", "default.frag");

    Camera cam;
    
    glfwSetWindowUserPointer(window, &cam);
    
    // --- Empty VAO for pull draws ---
    VAO pullVAO;



    // ImGui Init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
    ImGuiStyle& style = ImGui::GetStyle();

    // Example: Change text color and window background
    style.Colors[ImGuiCol_Text] = ImVec4(0.1f, 0.1f, 0.1f, 0.9f); // White text
    style.Colors[ImGuiCol_WindowBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f); // Dark, slightly transparent background
    style.WindowRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    //style.Colors[ImGuiCol_CheckMark] = ImVec4(.0f, 1.0f, 1.0f, 1.0f); // White text
    //style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White text
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.1f); // White text
    
    

    










    // 1. Generate two meshes:
    //MeshData meshData0 = generateVoxelMesh(1,);
    //MeshData meshData1 = generateVoxelMesh(0);

    // 2. Create & init the ChunkHandler:
    ChunkHandler handler;
    bool ok = handler.init(/*maxTotalQuads=*/10'000'0000u);


    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 10; ++y) {
            for (int z = 0; z < 10; ++z) {
                std::vector<std::unique_ptr<ISDFEdit>> blankSphereEdits;

                glm::ivec3 coords = { x, z,  y};
                glm::ivec3 offsetInVoxels = coords * 62;
                MeshData meshData = handler.generateVoxelMesh(2, offsetInVoxels, handler.sharedNoise, blankSphereEdits);


                std::vector<uint64_t> data = *meshData.vertices;
                handler.addOrUpdateChunk(coords, data);
            }
        }
    }
    
    // Now that both chunks are in `chunkMap`, we can bind both SSBOs:
    

    std::cout << "Loaded chunks: " << handler.getLoadedChunkCount() << "\n"; // should print 2


    //// 4. Add chunk #1 at coords (1,0,0):
    //glm::ivec3 coords3 = { 1, 0, 1 };
    //std::vector<uint64_t> data3 = *meshData0.vertices;
    //std::cout << "MeshData 2 quad count: " << meshData0.vertices->size() << "\n";
    //handler.addOrUpdateChunk(coords3, data3);

    // Now that both chunks are in `chunkMap`, we can bind both SSBOs:


    std::cout << "Loaded chunks: " << handler.getLoadedChunkCount() << "\n"; // should print 2


    handler.bindMetadataSSBO(2);
    

    bool render_check = true;
    bool render_trig = true;
    float render_dist = 1000.0;
    GLfloat color[] = { 0.6f, 0.8f, 0.9f, 1.0f };
    float edit_size = 4.0f;
    int edit_type = 1;
    int edit_shape = 0;

    float depthValue; // For a single depth value



    // --- Main loop ---
    float lastFrame = 0;
    while (!glfwWindowShouldClose(window)) {
        
        // Get window dimensions
        int windowWidth, windowHeight;
        glfwGetFramebufferSize(window, &windowWidth, &windowHeight);


        float currentFrame = glfwGetTime();
        float dt = currentFrame - lastFrame;
        lastFrame = currentFrame;
        
        glClearColor(color[0], color[1], color[2], color[3]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ImGui New Frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();


        shaderProgram.Activate();
        glUniform3i(glGetUniformLocation(shaderProgram.ID, "u_chunkSize"),
            62, 62, 62);
        glUniform1f(glGetUniformLocation(shaderProgram.ID, "u_voxelScale"),
            voxel_scale);

        if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
            glm::vec3 raycast_pos = cam.GetWorldPositionFromDepth(windowWidth, windowHeight, depthValue, cam.GetProjectionMatrix(windowWidth / windowHeight, 0.1, render_dist));


            glm::ivec3 chunkhash = glm::ivec3(glm::round(raycast_pos / glm::vec3(62 * voxel_scale)));
            std::cout << cam.GetPosition().x;
            std::cout << "Result: (" << chunkhash.x << "," << chunkhash.y << "," << chunkhash.z << ")" << "\n";

            auto start = std::chrono::high_resolution_clock::now();
            
            if (edit_shape == 0) {
                auto cubeedit = std::make_unique<SDFCubeEdit>(raycast_pos, glm::vec3(edit_size), edit_type);
                handler.addSDFEditAtWorldPos(*cubeedit, 62, handler.sharedNoise);
            }
            else {
                auto sphereEdit = std::make_unique<SDFSphereEdit>(raycast_pos, edit_size, edit_type);
                handler.addSDFEditAtWorldPos(*sphereEdit, 62, handler.sharedNoise);
            }


            
            

            
            
            // Stop timing
            auto end = std::chrono::high_resolution_clock::now();

            // Calculate duration
            std::chrono::duration<double, std::milli> duration = end - start; // Duration in milliseconds

            // Print the duration
            std::cout << "Time taken to generate Edits and all that stuff: " << duration.count() << " ms" << std::endl;

        }


               
        cam.ProcessKeyboard(window, dt);
        glm::mat4 proj = glm::perspective(glm::radians(cam.GetZoom()),
            (float)window_size_x / (float)window_size_y,
            0.1f, render_dist);
        // Set individual uniforms
        cam.SetViewMatrixUniform(shaderProgram.ID, "view");
        cam.SetProjectionMatrixUniform(shaderProgram.ID, "projection", proj);
        
        std::vector<GLint>   firsts;
        std::vector<GLsizei> counts;
        size_t N = handler.retrieveFirstsAndCounts(firsts, counts);
        
        handler.bindQuadsSSBO(1);
        handler.bindMetadataSSBO(2);
        //handler.bindSSBO(1);  // the big QuadBuffer @ binding=1
        //handler.bindMetadataSSBO(2); // the ChunkInfo SSBO @ binding=2
        
        pullVAO.Bind();
        //glDrawArrays(GL_TRIANGLES, 0, GLsizei(quadCount * 6));
        if (render_check == true) {
            if (render_trig == true) {
                //glDrawArrays(GL_TRIANGLES, 0, (meshData.vertexCount - 1) * 6);
                glMultiDrawArrays(GL_TRIANGLES, firsts.data(), counts.data(), static_cast<GLsizei>(N));
            }
            else {
                glMultiDrawArrays(GL_LINES, firsts.data(), counts.data(), static_cast<GLsizei>(N));
            }
            
        }
        


        
        // ImGui Draw
        ImGui::Begin("Settings");
        ImGui::Text("Copyright MountainLabs 2025");
        ImGui::Checkbox("Render", &render_check);
        ImGui::Checkbox("Render Triangles", &render_trig);
        ImGui::SliderFloat("RenderDist", &render_dist, 10.0, 1000.0);
        ImGui::SliderFloat("Camera Speed", &cam.MovementSpeed, 1.0, 100.0);
        ImGui::SliderFloat("Brush Size", &edit_size, 0.1, 30.0);
        ImGui::SliderInt("Edit Type", &edit_type, 0, 7);
        ImGui::SliderInt("Edit Shape", &edit_shape, 0, 1);
        ImGui::ColorEdit4("BackGround Color", color);
        ImGui::End();

        

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Ensure rendering commands are finished
        glFinish();

        

        // Calculate center coordinates
        int centerX = windowWidth / 2;
        int centerY = windowHeight / 2;

        glReadPixels(
            centerX,
            centerY,
            1,                   // Width (1 pixel)
            1,                   // Height (1 pixel)
            GL_DEPTH_COMPONENT,  // Format: read from the depth buffer
            GL_FLOAT,            // Data type: depth values are floats
            &depthValue          // Pointer to your float buffer
        );

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Voxel Cleanup
    handler.clearAll();
    //handler.destroySSBO();
    

    // ImGui Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // cleanup
    pullVAO.Delete();
    //glDeleteBuffers(1, &ssbo);
    shaderProgram.Delete();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}