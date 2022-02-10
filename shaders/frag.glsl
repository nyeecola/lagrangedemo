#version 330

uniform vec3 forced_color;
uniform vec3 lightPos;
uniform vec3 lightColor;

uniform bool light_emitter;

smooth in vec3 normal;
smooth in vec3 fragPos;

void main() {
    vec3 objColor = forced_color;

    vec3 result = objColor;

    if (!light_emitter) {
        // lighting
        vec3 ambient = vec3(0.1, 0.1, 0.1);
        vec3 lightDir = lightPos - fragPos;
        vec3 diffuse = lightColor * clamp(dot(lightDir, normal), 0.0, 1.0);
        // TODO: should we do attenuation?
        result = (ambient + diffuse) * objColor;
    }

    // gamma correction
    result = pow(result, vec3(1.0/2.2));

    // TODO: should we cap at 1?
    gl_FragColor = vec4(result, 1.0);
};
