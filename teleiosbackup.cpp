//#include<iostream>
//#include<glad/glad.h>
//#include<GLFW/glfw3.h>
//#include<glm/glm.hpp>
//#include<glm/gtc/matrix_transform.hpp>
//#include<glm/gtc/type_ptr.hpp>
//#include <chrono>   // For timing
//#include <string.h> // For memset
//
//#include"shaderClass.h"
//#include"VAO.h"
//#include"VBO.h"
//#include"EBO.h"
//
//#include "camera.h"
//
//#define BM_IMPLEMENTATION
//#include"mesher.h"
//
//uint32_t window_size_x = 1600;
//uint32_t window_size_y = 900;
//
//
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
//
//// Indices for vertices order
//GLuint indices[] =
//{
//	0, 3, 5, // Lower left triangle
//	3, 2, 4, // Lower right triangle
//	5, 4, 1 // Upper triangle
//};
//
//void mouse_callback(GLFWwindow* window, double xpos, double ypos)
//{
//	//// --- ADD THESE DEBUG PRINTS ---
//	//std::cout << "DEBUG: Raw Mouse X=" << xpos << ", Y=" << ypos << std::endl;
//	//// --- END DEBUG PRINTS ---
//
//	// Retrieve the Camera object from the window's user pointer
//	Camera* camera = static_cast<Camera*>(glfwGetWindowUserPointer(window));
//	if (!camera) return; // Defensive check if pointer is null
//
//	// The Camera::ProcessMouseMovement function handles the internal 'firstMouse' logic.
//	camera->ProcessMouseMovement(xpos, ypos);
//}
//
//
//int main() {
//
//
//	// engine stuff
//
//	glfwInit();
//
//	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
//	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
//	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
//
//	GLFWwindow* window = glfwCreateWindow(window_size_x, window_size_y, "Teleios", NULL, NULL);
//	if (window == NULL) {
//		std::cout << "failed to create window!" << std::endl;
//		glfwTerminate();
//		return -1;
//	}
//	glfwMakeContextCurrent(window);
//	glfwSetCursorPosCallback(window, mouse_callback); // Register the callback
//	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
//	glfwSetInputMode(window, GLFW_STICKY_KEYS, GLFW_FALSE);
//	glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_FALSE);
//
//	glfwSwapInterval(1); // Enable V-Sync
//	gladLoadGL();
//	glViewport(0, 0, window_size_x, window_size_y);
//	glEnable(GL_DEPTH_TEST);
//
//	// Generates Shader object using shaders defualt.vert and default.frag
//	Shader shaderProgram("default.vert", "default.frag");
//
//	GLuint CameraBindPoint = 0;
//	Camera cam = Camera();
//	cam.LinkViewMatrixToShader(shaderProgram.ID, "Matrices", 0);
//
//	// --- CRITICAL: Store camera pointer in window user data for the callback ---
//	glfwSetWindowUserPointer(window, &cam);
//
//	// Generates Vertex Array Object and binds it
//	VAO VAO1;
//	VAO1.Bind();
//
//	// Generates Vertex Buffer Object and links it to vertices
//	VBO VBO1(vertices, sizeof(vertices));
//	// Generates Element Buffer Object and links it to indices
//	EBO EBO1(indices, sizeof(indices));
//
//	// Links VBO to VAO
//	VAO1.LinkVBO(VBO1, 0);
//	// Unbind all to prevent accidentally modifying them
//	VAO1.Unbind();
//	VBO1.Unbind();
//	EBO1.Unbind();
//
//	float lastFrame = 0.0f;
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//
//	// voxel stuff
//
//
//
//
//
//	// 1. Prepare voxel data (64x64x64)
//	// Remember, order is ZXY as per the comment.
//	std::vector<uint8_t> voxels(CS_P * CS_P * CS_P);
//
//	// Fill some voxels to create a simple cube for demonstration
//	// This example creates a 62x62x62 cube inside the padded 64x64x64 volume
//	for (int z = 1; z < CS_P - 1; ++z) {
//		for (int y = 1; y < CS_P - 1; ++y) {
//			for (int x = 1; x < CS_P - 1; ++x) {
//				// Assign a non-zero type to make it visible
//				voxels[x + (y * CS_P) + (z * CS_P2)] = 1;
//			}
//		}
//	}
//
//	// 2. Initialize MeshData
//	MeshData meshData;
//	meshData.faceMasks = new uint64_t[CS_2 * 6];
//	meshData.opaqueMask = new uint64_t[CS_P2];
//	meshData.forwardMerged = new uint8_t[CS_2];
//	meshData.rightMerged = new uint8_t[CS];
//
//	// --- THIS IS THE KEY CHANGE ---
//	// Initialize with an initial size and capacity
//	// meshData.maxVertices should be a reasonable initial estimate for the *total* number of quads.
//	// The `mesh` function's `insertQuad` will then `resize` when it gets close to this limit.
//	meshData.maxVertices = CS_P * CS_P * CS_P * 6 / 2; // A very rough overestimate: max 6 faces per voxel, halved for efficiency
//	// Adjust this based on expected mesh density.
//	// A small chunk might have ~2000-4000 quads.
//	if (meshData.maxVertices < 1024) meshData.maxVertices = 1024; // Ensure a minimum for small chunks
//
//	// Allocate the vector and initialize it with an initial SIZE
//	// This makes vertices[0] (and subsequent indices up to meshData.maxVertices-1) valid to write to.
//	meshData.vertices = new BM_VECTOR<uint64_t>(meshData.maxVertices);
//	// The constructor with a size argument sets both size() and capacity().
//	// So, vertices.size() will initially be meshData.maxVertices.
//
//
//
//
//	// Initialize masks and merged arrays to zero
//	// The BM_MEMSET macro will use memset from <string.h>
//	memset(meshData.faceMasks, 0, sizeof(uint64_t) * CS_2 * 6);
//	memset(meshData.opaqueMask, 0, sizeof(uint64_t) * CS_P2);
//	memset(meshData.forwardMerged, 0, sizeof(uint8_t) * CS_2);
//	memset(meshData.rightMerged, 0, sizeof(uint8_t) * CS);
//
//	// Populate opaqueMask based on your voxel data for culling
//	// This is crucial for the meshing algorithm to work correctly.
//	// The P_MASK logic in the mesh function suggests that opaqueMask
//	// bits correspond to whether a voxel is opaque (present).
//	// You'll need to set these bits based on your `voxels` data.
//	// A simple example:
//	for (int z = 0; z < CS_P; ++z) {
//		for (int y = 0; y < CS_P; ++y) {
//			uint64_t columnBits = 0;
//			for (int x = 0; x < CS_P; ++x) {
//				if (voxels[x + (y * CS_P) + (z * CS_P2)] != 0) { // If voxel is opaque
//					columnBits |= (1ull << x);
//				}
//			}
//			meshData.opaqueMask[y + (z * CS_P)] = columnBits;
//		}
//	}
//
//
//	// --- Timing Start ---
//	auto start = std::chrono::high_resolution_clock::now();
//
//	// 3. Call the mesh function
//	mesh(voxels.data(), meshData);
//
//	// --- Timing End ---
//	auto end = std::chrono::high_resolution_clock::now();
//
//	// Calculate duration
//	std::chrono::duration<double, std::milli> duration_ms = end - start; // Duration in milliseconds
//
//	// 4. Access and print the generated mesh data and timing
//	std::cout << "Mesh generation complete." << std::endl;
//	std::cout << "Meshing took: " << duration_ms.count() << " milliseconds" << std::endl;
//	std::cout << "Total vertices generated: " << meshData.vertexCount << std::endl;
//
//	for (int i = 0; i < 6; ++i) {
//		std::cout << "Face " << i << ": "
//			<< "Begin: " << meshData.faceVertexBegin[i]
//			<< ", Length: " << meshData.faceVertexLength[i] << std::endl;
//	}
//
//
//
//
//	// Voxel binding stuff
//
//	GLsizei totalVerts = GLsizei(meshData.vertexCount * 6);
//	GLuint ssbo;
//	glGenBuffers(1, &ssbo);
//	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
//	glBufferData(GL_SHADER_STORAGE_BUFFER,
//		meshData.vertexCount * sizeof(uint64_t),
//		meshData.vertices->data(),
//		GL_STATIC_DRAW);
//	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
//	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
//
//
//
//
//
//
//
//
//
//	// Main while loop
//	while (!glfwWindowShouldClose(window))
//	{
//
//		// Delta time calculations
//		float currentFrame = glfwGetTime();
//		float deltaTime = currentFrame - lastFrame;
//		lastFrame = currentFrame;
//
//
//		// Specify the color of the background
//		glClearColor(0.07f, 0.13f, 0.17f, 1.0f);
//		// Clean the back buffer and assign the new color to it
//		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//		// update the camera
//		cam.ProcessKeyboard(window, deltaTime);
//		// In your main render loop:
//		glm::mat4 projection = glm::perspective(glm::radians(cam.GetZoom()), (float)window_size_x / (float)window_size_y, 0.1f, 100.0f);
//		cam.UpdateProjectionMatrixUBO(projection); // Call the new function
//		cam.BindViewMatrix(CameraBindPoint);
//		// Tell OpenGL which Shader Program we want to use
//		shaderProgram.Activate();
//		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);
//		// Bind the VAO so OpenGL knows to use it
//		VAO1.Bind();
//		// Draw primitives, number of indices, datatype of indices, index of indices
//		glDrawArrays(GL_TRIANGLES, 0, totalVerts);
//		// Swap the back buffer with the front buffer
//		glfwSwapBuffers(window);
//
//
//		/*if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
//			std::cout << "DEBUG: W key is PRESSED (main loop check)" << std::endl;
//		}
//		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
//			std::cout << "DEBUG: A key is PRESSED (main loop check)" << std::endl;
//		}*/
//
//		// Take care of all GLFW events
//		glfwPollEvents();
//
//
//		// --- PLEASE PROVIDE THE CONSOLE OUTPUT FOR THESE LINES ---
//		/*std::cout << "DEBUG: DeltaTime = " << deltaTime << std::endl;
//		glm::vec3 currentCamPos = cam.GetPosition();
//		std::cout << "DEBUG: Camera Position = (" << currentCamPos.x << ", " << currentCamPos.y << ", " << currentCamPos.z << ")" << std::endl;*/
//	}
//
//
//
//	// Delete all the objects we've created
//	VAO1.Delete();
//	VBO1.Delete();
//	EBO1.Delete();
//	shaderProgram.Delete();
//	// Delete window before ending the program
//	glfwDestroyWindow(window);
//	// Terminate GLFW before ending the program
//	glfwTerminate();
//	return 0;
//}