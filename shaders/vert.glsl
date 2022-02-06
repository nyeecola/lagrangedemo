#version 330

// TODO: separate view_proj ???
uniform mat4 model;
uniform mat4 view_proj;

layout (location = 0) in vec3 vPos;
//layout (location = 1) in vec3 vNormal;

//smooth out vec3 normal;
smooth out vec3 fragPos;

void main() {
    gl_Position = view_proj * model * vec4(vPos, 1.0);
    fragPos = vec3(model * vec4(vPos, 1.0));
    //normal = mat3(transpose(inverse(model))) * vNormal; //TODO: do this on CPU
};
