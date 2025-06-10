// EBO.h
#pragma once
#include <glad/glad.h>

class EBO {
public:
    GLuint ID;

    /// Creates an EBO *for the currently bound VAO*.
    /// Avoid unbinding it, since ELEMENT_ARRAY_BUFFER is part of VAO state.
    EBO(const GLuint* indices, GLsizeiptr size) {
        glGenBuffers(1, &ID);
        // bind into the VAO’s state
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ID);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, indices, GL_STATIC_DRAW);
        // do NOT glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        // leaving it bound so that any glBindVertexArray(VAO) reattaches it.
    }

    /// If you ever need to switch away, call this *while the VAO you want to detach from is bound*.
    static void UnbindFromVAO() {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void Delete() {
        glDeleteBuffers(1, &ID);
    }
};
