// VAO.h
#pragma once
#include <glad/glad.h>

class VAO {
public:
    GLuint ID;

    /// Default ctor – just allocates an empty VAO you can bind for SSBO draws
    VAO() {
        glGenVertexArrays(1, &ID);
    }

    /// Bind the VAO
    void Bind() const {
        glBindVertexArray(ID);
    }

    /// Unbind any VAO
    static void Unbind() {
        glBindVertexArray(0);
    }

    /// Link a VBO to this VAO at the given layout location.
    /// You only call this if you actually have vertex attributes.
    void LinkVBO(GLuint vboID, GLuint layout, GLint size = 3, GLenum type = GL_FLOAT,
        GLsizei stride = 0, const void* offset = nullptr)
    {
        Bind();
        glBindBuffer(GL_ARRAY_BUFFER, vboID);
        glVertexAttribPointer(layout, size, type, GL_FALSE, stride, offset);
        glEnableVertexAttribArray(layout);
        // leave the VBO bound to the VAO, unbind only the ARRAY_BUFFER
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        Unbind();
    }

    /// Delete the VAO
    void Delete() {
        glDeleteVertexArrays(1, &ID);
    }
};
