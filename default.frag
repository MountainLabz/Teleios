#version 460 core

// Input variables from the vertex shader.
// These must match the 'out' variables in the vertex shader in type and name.
flat in vec3 v_Normal;
flat in float v_BlockType; // The block type, passed as a float

out vec4 FragColor;

// A lookup table for colors based on the block type.
// Ensure your block types (from 1 to N) map correctly to these indices.
const vec3 colorLookup[8] = {
    vec3(0.2, 0.659, 0.839),   // Type 1 (index 0) - Light Blue
    vec3(0.3, 0.902, 0.102), // Type 2 (index 1) - Grey
    vec3(0.278, 0.600, 0.141), // Type 3 (index 2) - Green
    vec3(1.0, 0.0, 0.0),      // Type 4 (index 3) - Dark Blue
    vec3(0.1, 0.6, 0.6),      // Type 5 (index 4) - Cyan
    vec3(0.6, 0.1, 0.6),      // Type 6 (index 5) - Purple
    vec3(0.6, 0.6, 0.1),      // Type 7 (index 6) - Yellow
    vec3(0.6, 0.1, 0.1)       // Type 8 (index 7) - Red
};

void main()
{
    // Get the base color based on the block type.
    // We subtract 1 from v_BlockType to get a 0-indexed value for the array lookup.
    // Ensure v_BlockType is at least 1 in your C++ code for valid indexing.
    int typeIndex = int(v_BlockType) - 1;

    // Clamp the index to prevent out-of-bounds access, if type is unexpected
    typeIndex = clamp(typeIndex, 0, colorLookup.length() - 1);

    vec3 baseColor = colorLookup[typeIndex];

    // Apply a simple tint based on the normal.
    // This provides a visual distinction between faces without a full lighting model.
    // Faces pointing along positive X, Y, or Z will be slightly brighter/tinted
    // in their respective color channels. For example, a face with a normal of (1,0,0)
    // will have its red component slightly increased.
    vec3 normalTintFactor = vec3(0.8) + abs(v_Normal) * 0.2;
    vec3 finalColor = baseColor * normalTintFactor;

    // Set the final fragment color
    FragColor = vec4(finalColor, 1.0f);
}