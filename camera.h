// camera.h
#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>
#include <glad/glad.h> // Include glad for OpenGL function pointers
#include <iostream>  // Include for std::cerr
#include <string>    // Include for std::string

class Camera {
public:
    float MovementSpeed;
    bool mouseLocked = true; // Keep track of the mouse lock state
    double lastToggleTime = 0.0;
    // Constructor
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
        float yaw = -90.0f,
        float pitch = 0.0f,
        float moveSpeed = 10.0f,
        float sensitivity = 0.05f)
        : Position(position), WorldUp(up), Yaw(yaw), Pitch(pitch), MovementSpeed(moveSpeed), MouseSensitivity(sensitivity)
    {
        updateCameraVectors();
        updateViewMatrix();
        // Removed UBO creation
    }

    // Destructor (no UBO to clean up)
    ~Camera()
    {
        // No UBO to delete
    }

    // Returns the stored view matrix
    glm::mat4 GetViewMatrix() const
    {
        return ViewMatrix;
    }

    // Processes keyboard input
    void ProcessKeyboard(GLFWwindow* window, float deltaTime)
    {
        float velocity = MovementSpeed * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            Position += Front * velocity;
        }

        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            Position -= Front * velocity;
        }

        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            Position -= Right * velocity;
        }

        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            Position += Right * velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            Position += WorldUp * velocity;
        }
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            Position -= WorldUp * velocity;
        }
        // Modify this block
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GL_TRUE);
        }

        if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // Unlock mouse
            mouseLocked = false;
        }

        if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // Lock mouse
            mouseLocked = true;
        }



        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
            MovementSpeed = MovementSpeed + 1;
        }

        if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS) {
            MovementSpeed = MovementSpeed - 1;
        }

        updateCameraVectors();
        updateViewMatrix();
        // Removed UBO update
    }

    // Processes mouse movement
    void ProcessMouseMovement(double xpos, double ypos, bool constrainPitch = true)
    {
        if (firstMouse)
        {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos;

        lastX = xpos;
        lastY = ypos;

        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw += xoffset;
        Pitch += yoffset;

        if (constrainPitch)
        {
            if (Pitch > 89.0f)
                Pitch = 89.0f;
            if (Pitch < -89.0f)
                Pitch = -89.0f;
        }

        updateCameraVectors();
        updateViewMatrix();
        // Removed UBO update
    }

    // Processes mouse scroll wheel input
    void ProcessMouseScroll(float yoffset)
    {
        Zoom -= (float)yoffset;
        if (Zoom < 1.0f)
            Zoom = 1.0f;
        if (Zoom > 45.0f)
            Zoom = 45.0f;
    }

    // New: Set the view matrix as a uniform
    void SetViewMatrixUniform(GLuint shaderProgram, const std::string& uniformName) const {
        GLint viewLoc = glGetUniformLocation(shaderProgram, uniformName.c_str());
        if (viewLoc == -1) {
            std::cerr << "Error: Uniform '" << uniformName << "' not found in shader." << std::endl;
            return;
        }
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(ViewMatrix));
    }

    // New: Set the projection matrix as a uniform
    void SetProjectionMatrixUniform(GLuint shaderProgram, const std::string& uniformName, const glm::mat4& projMatrix) const {
        GLint projLoc = glGetUniformLocation(shaderProgram, uniformName.c_str());
        if (projLoc == -1) {
            std::cerr << "Error: Uniform '" << uniformName << "' not found in shader." << std::endl;
            return;
        }
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projMatrix));
    }

    // Getter functions
    glm::vec3 GetPosition() const { return Position; }
    glm::vec3 GetFront() const { return Front; }
    glm::vec3 GetUp() const { return Up; }
    glm::vec3 GetRight() const { return Right; }
    float GetZoom() const { return Zoom; }
    // Removed get_viewMatrixUBO()
   
    // New: Get the perspective projection matrix based on current FOV and provided aspect ratio/clip planes
    glm::mat4 GetProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) const {
        return glm::perspective(glm::radians(Zoom), aspectRatio, nearPlane, farPlane);
    }

    // New function to convert normalized depth to world coordinates
    // windowWidth, windowHeight: current dimensions of your OpenGL viewport/window
    // normalizedDepth: the float value read directly from glReadPixels (0.0 to 1.0)
    // projectionMatrix: The EXACT projection matrix used for rendering the scene
    //                     (e.g., from glm::perspective or glm::ortho)
    glm::vec3 GetWorldPositionFromDepth(int windowWidth, int windowHeight, float normalizedDepth, const glm::mat4& projectionMatrix) const {
        // 1. Calculate screen coordinates for the middle
        int centerX = windowWidth / 2;
        int centerY = windowHeight / 2;

        // 2. Convert to Normalized Device Coordinates (NDC)
        float x_ndc = (2.0f * (float)centerX / windowWidth) - 1.0f;
        // Important: OpenGL's screen Y-axis is bottom-up (0 at bottom).
        // NDC Y-axis is also bottom-up (-1 at bottom, 1 at top).
        // So, a direct mapping for Y is correct:
        float y_ndc = (2.0f * (float)centerY / windowHeight) - 1.0f;
        float z_ndc = (2.0f * normalizedDepth) - 1.0f; // From [0,1] depth to [-1,1] NDC Z

        glm::vec4 ndcPoint = glm::vec4(x_ndc, y_ndc, z_ndc, 1.0f);

        // 3. Transform from NDC to View Space (Eye Space)
        glm::mat4 inverseProjection = glm::inverse(projectionMatrix);
        glm::vec4 viewSpacePoint = inverseProjection * ndcPoint;

        // Perform perspective divide to get correct 3D coordinates in view space
        if (viewSpacePoint.w != 0.0f) { // Avoid division by zero
            viewSpacePoint /= viewSpacePoint.w;
        }
        else {
            std::cerr << "Warning: Division by zero in perspective divide. Check projection matrix and depth value." << std::endl;
            return glm::vec3(0.0f); // Return origin or handle error appropriately
        }

        // 4. Transform from View Space to World Space
        glm::mat4 inverseView = glm::inverse(ViewMatrix); // Use the camera's current ViewMatrix
        glm::vec4 worldSpacePoint = inverseView * viewSpacePoint;

        // Return the XYZ components (w should be 1.0 after these operations for a point)
        return glm::vec3(worldSpacePoint);
    }

private:
    // Camera Attributes
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    // Euler Angles
    float Yaw;
    float Pitch;
    // Camera options
    float MouseSensitivity;
    float Zoom = 45.0f;
    // For first mouse input
    bool firstMouse = true;
    double lastX = 0.0f;
    double lastY = 0.0f;
    // Cached View Matrix
    glm::mat4 ViewMatrix;
    // Removed OpenGL Uniform Buffer Object

    // Calculates the front, right, and up vectors from Euler Angles
    void updateCameraVectors()
    {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }

    // Updates the stored view matrix
    void updateViewMatrix()
    {
        ViewMatrix = glm::lookAt(Position, Position + Front, Up);
    }

    // Removed createViewMatrixUBO()
    // Removed updateViewMatrixUBO()
};

#endif // CAMERA_H