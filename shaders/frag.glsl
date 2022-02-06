#version 330

uniform vec3 forced_color;

//in vec3 normal;
in vec3 fragPos;

void main() {
    vec3 objColor = forced_color;

    vec3 result = objColor;

    // gamma correction
    result = pow(result, vec3(1.0/2.2));

    // TODO: should we cap at 1?
    gl_FragColor = vec4(result, 1.0);
};
