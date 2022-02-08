#define GLEW_STATIC
#include <GL/glew.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext.hpp>

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800

#define MAX_CELESTIAL_BODIES 50

#define FOCUSED_CAMERA_DIST 25.0
#define LINE_WIDTH 500.0
#define MIN_ZOOM 800.0
#define NUM_ZOOM_LEVELS 20

#define POLL_GL_ERROR poll_gl_error(__FILE__, __LINE__)

/* TODO:
 * - Improve camera movement. (i.e. add zoom to default camera, add mouse movement)
 * - Add saturn's ring.
 * - Add Uranus and Neptune.
 * - Add relative path drawing.
 * - Add better lines. (check https://www.labri.fr/perso/nrougier/python-opengl/#rendering-lines)
 * - Add bloom.
 * - Add stars in the background (maybe skybox?)
 * - Add GUI with celestial body info.
 * - Add parameters (mass, initial velocity, distance to other bodies) to GUI.
 * - Add ability to pause/resume simulation.
 * - Add orbit prediction.
 * - Make a lagrange point halo orbit demonstration, with stationkeeping maneuvers.
 */

enum RenderingMode {
    RENDER_TO_SCALE,
    RENDER_MINIFIED,
};


struct Sphere {
#define MAX_SPHERE_ELEMENTS 3000
    GLfloat data[MAX_SPHERE_ELEMENTS];
    GLfloat normals[MAX_SPHERE_ELEMENTS];
    int num_elements;
};

struct Line {
    GLfloat vertices[2 * 3]; // triangle strip
};

struct CelestialBody;

struct LinePath {
#define MAX_LINE_PATH_SEGMENTS 1000
    Line* lines; // circular buffer
    CelestialBody *owner;
    int path_start;
    int num_segments;
};

struct CelestialBody {
    glm::dvec3 position;
    glm::dvec3 velocity;
    double mass;
    double size;
    glm::vec3 color;

    LinePath* path_taken;
    // LinePath path_prediction; TODO

    // used for RENDER_MINIFIED
    double minified_dist_scale;
    double minified_size_scale;
    CelestialBody* anchor;
};

struct GlobalState {
    RenderingMode rendering_mode;
    CelestialBody* celestial_bodies[MAX_CELESTIAL_BODIES];
    int num_celestial_bodies;
    int camera_target;
    glm::vec3 camera_pos;
    float focused_camera_distance;
    int zoom_level;
    bool light_emitter;
    bool enable_orbit_rendering;
};

void poll_gl_error(const char* file, long long line) {
    int err = glGetError();
    if (err) {
        printf("%s: line %lld: error %d, ", file, line, err);
        exit(1);
    }
}

static void error_callback(int error, const char* description) {
    fprintf(stderr, "Error: %s\n", description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    GlobalState *global_state = (GlobalState *) glfwGetWindowUserPointer(window);

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // switch rendering mode
    if ((key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL) && action == GLFW_PRESS) {
        global_state->rendering_mode = global_state->rendering_mode == RENDER_MINIFIED ? RENDER_TO_SCALE : RENDER_MINIFIED;
        // reset paths
        for (int i = 0; i < global_state->num_celestial_bodies; i++) {
            global_state->celestial_bodies[i]->path_taken->num_segments = 0;
            global_state->celestial_bodies[i]->path_taken->path_start = 0;
        }
        // reset camera
        global_state->camera_target = -1;
    }

    // enable orbit rendering
    if (key == GLFW_KEY_O && action == GLFW_PRESS) {
        global_state->enable_orbit_rendering = !global_state->enable_orbit_rendering;
    }

    // switch camera target
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && global_state->rendering_mode == RENDER_TO_SCALE) {
        global_state->camera_target = (global_state->camera_target + 1) % (global_state->num_celestial_bodies + 1);
        if (global_state->camera_target == global_state->num_celestial_bodies) {
            global_state->camera_target = -1; // default camera
        }
        // reset paths
        for (int i = 0; i < global_state->num_celestial_bodies; i++) {
            global_state->celestial_bodies[i]->path_taken->num_segments = 0;
            global_state->celestial_bodies[i]->path_taken->path_start = 0;
        }
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    GlobalState* global_state = (GlobalState*)glfwGetWindowUserPointer(window);

    if (global_state->camera_target == -1 || global_state->rendering_mode == RENDER_MINIFIED)
        return;

    CelestialBody* target = global_state->celestial_bodies[global_state->camera_target];
    float max_zoom = std::log2(target->size * 3.5f);
    float min_zoom = std::log2(MIN_ZOOM);

    if (yoffset > 0.0 && global_state->zoom_level < NUM_ZOOM_LEVELS)
        global_state->zoom_level++;
    else if (yoffset < 0.0 && global_state->zoom_level > 0)
        global_state->zoom_level--;

    // TODO: this is a hack, we should have a better path rendering implementation
    // reset paths
    for (int i = 0; i < global_state->num_celestial_bodies; i++) {
        global_state->celestial_bodies[i]->path_taken->num_segments = 0;
        global_state->celestial_bodies[i]->path_taken->path_start = 0;
    }
    
    global_state->focused_camera_distance = std::exp2(min_zoom + (max_zoom-min_zoom)*global_state->zoom_level / NUM_ZOOM_LEVELS);
}


// TODO: caller must free buffer
char* load_file(char const* path) {
    char* buffer = 0;
    long length;
    FILE* f = fopen(path, "rb");

    if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = (char*)malloc((length + 1) * sizeof(char));

        if (buffer) {
            fread(buffer, sizeof(char), length, f);
        } else {
            // TODO: clean up all aborts
            exit(-1);
        }
        fclose(f);
    }

    buffer[length] = '\0';

    return buffer;
}

LinePath *create_line_path(CelestialBody *c) {
    LinePath *line_path = (LinePath *) calloc(1, sizeof(LinePath));
    if (line_path == NULL)
        exit(-1);
    // TODO: check what's up with this warning
    line_path->lines = (Line *) calloc(MAX_LINE_PATH_SEGMENTS, sizeof(Line));
    line_path->owner = c;
    return line_path;
}

void destroy_line_path(LinePath **line_path) {
    if (!line_path)
        return;

    if ((*line_path)->lines)
        free((*line_path)->lines);
    free(*line_path);
    *line_path = NULL;
}

// TODO: optimize this
// TODO: think how to make it cilindrical in 3d
void append_to_line_path(LinePath *line_path, GlobalState *global_state, glm::vec3 pos, glm::vec3 o0, glm::vec3 o1, float width) {
    int index = (line_path->path_start + line_path->num_segments) % MAX_LINE_PATH_SEGMENTS;

    glm::vec3 orig = ((o0 - o1) * 1.0f / 2.0f) + o1;
    glm::vec3 dir = pos - orig;

    glm::vec3 perp = glm::cross(glm::normalize(dir), glm::vec3(0.0, 1.0, 0.0));

    glm::vec3 p0 = pos + perp * (1.0f / 2.0f) * width;
    glm::vec3 p1 = pos - perp * (1.0f / 2.0f) * width;

    Line new_line = { p0.x, p0.y, p0.z, p1.x, p1.y, p1.z };

    line_path->lines[index] = new_line;

    line_path->num_segments++;
    if (line_path->num_segments > MAX_LINE_PATH_SEGMENTS) {
        line_path->num_segments = MAX_LINE_PATH_SEGMENTS;
        line_path->path_start = (line_path->path_start + 1) % MAX_LINE_PATH_SEGMENTS;
    }
}

void update_line_path(LinePath* line_path, GlobalState *global_state, glm::vec3 pos) {
    int index = (line_path->path_start + line_path->num_segments) % MAX_LINE_PATH_SEGMENTS;
    int index_bef = index == 0 ? MAX_LINE_PATH_SEGMENTS - 1 : index - 1;

    glm::vec3 camera_target_pos = global_state->camera_target == -1 ? glm::vec3(0) : glm::vec3(global_state->celestial_bodies[global_state->camera_target]->position);

    float width = glm::length(global_state->camera_pos - camera_target_pos) / LINE_WIDTH;
    if (line_path->num_segments == 0) {
        Line l = { -width / 2.0f, 0, 0 , width / 2.0f, 0, 0 };
        line_path->lines[index_bef] = l;
    }

    glm::vec3 o0 = glm::vec3(line_path->lines[index_bef].vertices[0], line_path->lines[index_bef].vertices[1], line_path->lines[index_bef].vertices[2]);
    glm::vec3 o1 = glm::vec3(line_path->lines[index_bef].vertices[3], line_path->lines[index_bef].vertices[4], line_path->lines[index_bef].vertices[5]);
    
    if (glm::abs(glm::length(pos - o0)) > width * 2)
        append_to_line_path(line_path, global_state, pos, o0, o1, width);
    //else // TODO
      //  line_path->lines[index]
}

// Adapted from https://stackoverflow.com/questions/7687148/drawing-sphere-in-opengl-without-using-glusphere
Sphere *create_sphere(int gradation) {
    GLfloat x, y, z, alpha, beta;  
    GLfloat radius = 1.0f;

    constexpr float PI = glm::pi<float>();

    Sphere* sphere = (Sphere *) calloc(1, sizeof(Sphere));
    if (sphere == NULL) {
        fprintf(stderr, "Error: failed to allocate memory for a sphere.\n");
        exit(-1);
    }
    sphere->num_elements = 6 * (gradation + 1) * (2 * gradation + 1);
    assert(sphere->num_elements <= MAX_SPHERE_ELEMENTS);

    int i = 0;
    for (alpha = 0.0; alpha < 1.01 * PI; alpha += PI / gradation)
    {
        for (beta = 0.0; beta < 2.01 * PI; beta += PI / gradation)
        {
            sphere->data[i+0] = radius * cos(beta) * sin(alpha);
            sphere->data[i+1] = radius * sin(beta) * sin(alpha);
            sphere->data[i+2] = radius * cos(alpha);
         
            sphere->data[i+3] = radius * cos(beta) * sin(alpha + PI / gradation);
            sphere->data[i+4] = radius * sin(beta) * sin(alpha + PI / gradation);
            sphere->data[i+5] = radius * cos(alpha + PI / gradation);

            for (int n = 0; n < 2; n++) {
                glm::vec3 normalized = glm::normalize(glm::vec3(sphere->data[i + (n * 3) + 0], sphere->data[i + (n * 3) + 1], sphere->data[i + (n * 3) + 2]));
                sphere->normals[i + (n * 3) + 0] = normalized.x;
                sphere->normals[i + (n * 3) + 1] = normalized.y;
                sphere->normals[i + (n * 3) + 2] = normalized.z;
            }

            i += 6;
        }
    }

    assert(i == sphere->num_elements);

    return sphere;
}

CelestialBody *create_celestial_body(glm::dvec3 position, glm::dvec3 velocity, float mass, float size, glm::vec3 color,
                                     double minified_size_scale, double minified_dist_scale, CelestialBody *anchor) {
    CelestialBody *c = (CelestialBody*) calloc(1, sizeof(CelestialBody));
    if (c == NULL) {
        fprintf(stderr, "Error: failed to allocate memory for celestial body.\n");
        exit(-1);
    }

    c->position = position;
    c->velocity = velocity;
    c->mass = mass;
    c->size = size;
    c->color = color;
    c->path_taken = create_line_path(c);
    c->minified_dist_scale = minified_dist_scale;
    c->minified_size_scale = minified_size_scale;
    c->anchor = anchor;

    return c;
}

void destroy_celestial_body(CelestialBody **c) {
    if (c && (*c)) {
        if ((*c)->path_taken) {
            free((*c)->path_taken);
            (*c)->path_taken = NULL;
        }
        free(*c);
        *c = NULL;
    }
}

void render_celestial_body(GlobalState *global_state, GLuint VAO, GLuint pathVAO, GLuint pathVBO, GLuint program, Sphere *s, CelestialBody *c) {
    double scale_dist = 1.0, scale_size = 1.0, anchor_scale_dist = 1.0;
    CelestialBody* anchor = NULL;

    // adjust scale given a rendering mode
    switch (global_state->rendering_mode) {
    case RENDER_MINIFIED:
        scale_size = c->minified_size_scale;
        scale_dist = c->minified_dist_scale;
        anchor = c->anchor;
        anchor_scale_dist = c->anchor ? c->anchor->minified_dist_scale : 1.0;
        break;
    case RENDER_TO_SCALE:
        break;
    default:
        fprintf(stderr, "Error: Invalid rendering mode.");
        exit(-1);
    }

    // render the celestial body
    glBindVertexArray(VAO);
        glm::mat4 model_mat(1.0f);
        if (anchor) {
            model_mat = glm::translate(model_mat, glm::vec3(c->position - c->anchor->position) * scale_dist);
            model_mat = glm::translate(model_mat, glm::vec3(c->anchor->position) * anchor_scale_dist);
        } else {
            model_mat = glm::translate(model_mat, glm::vec3(c->position) * scale_dist);
        }
        model_mat = glm::scale(model_mat, glm::vec3(c->size) * scale_size);
        glUniform3fv(glGetUniformLocation(program, "forced_color"), 1, glm::value_ptr(c->color));
        glUniformMatrix4fv(glGetUniformLocation(program, "model"), 1, GL_FALSE, glm::value_ptr(model_mat));

        // FIXME: we are hardcoding the only light source as the sun
        // TODO: add a better lighting system with support for multiple lights
        glUniform3fv(glGetUniformLocation(program, "lightPos"), 1, glm::value_ptr(glm::vec3(global_state->celestial_bodies[0]->position)));
        glUniform3fv(glGetUniformLocation(program, "lightColor"), 1, glm::value_ptr(glm::vec3(global_state->celestial_bodies[0]->color)));
        glUniform1i(glGetUniformLocation(program, "light_emitter"), c == global_state->celestial_bodies[0]);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, s->num_elements / 3);
    glBindVertexArray(0);

    // TODO: Technically this shouldn't be here, since it's not rendering anything and this code should run even when the screen loses focus
    // update the path taken by the celestial body
    glm::vec3 rendered_position;
    if (anchor) {
        rendered_position = c->anchor->position * anchor_scale_dist;
        rendered_position += (c->position - c->anchor->position) * scale_dist;
    } else {
        rendered_position = c->position * scale_dist;
    }
    update_line_path(c->path_taken, global_state, rendered_position);

    // render path taken
    if (global_state->enable_orbit_rendering) {
        glBindVertexArray(pathVAO);
            glBindBuffer(GL_ARRAY_BUFFER, pathVBO);
            model_mat = glm::mat4(1.0f);
            glUniform3fv(glGetUniformLocation(program, "forced_color"), 1, glm::value_ptr(c->color));
            glUniformMatrix4fv(glGetUniformLocation(program, "model"), 1, GL_FALSE, glm::value_ptr(model_mat));

            // FIXME: temporary hack to transfor the circular buffer into a linear buffer
            // TODO: stop allocating this stuff thousands of times!!!
            Line* linearized = (Line*)calloc(c->path_taken->num_segments, sizeof(Line));
            if (linearized == NULL) exit(-1);
            memcpy(linearized, c->path_taken->lines + c->path_taken->path_start, (c->path_taken->num_segments - c->path_taken->path_start) * 6 * sizeof(GLfloat));
            memcpy(linearized + (c->path_taken->num_segments - c->path_taken->path_start), c->path_taken->lines, c->path_taken->path_start * 6 * sizeof(GLfloat));

            glBufferData(GL_ARRAY_BUFFER, c->path_taken->num_segments * 6 * sizeof(GLfloat), /*c->path_taken->lines*/ linearized, GL_DYNAMIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void*)0);

            free(linearized);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, c->path_taken->num_segments * 2);
        glBindVertexArray(0);
    }
}

int main()
{
    GLFWwindow* window;
    GLuint vertex_shader, fragment_shader, program;
    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Lagrange Demo", NULL, NULL);

    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwMakeContextCurrent(window);

    glewExperimental = 1;
    if (glewInit() != GLEW_OK) {
        exit(EXIT_FAILURE);
    }

    // display OpenGL context version
    {
        const GLubyte* version_str = glGetString(GL_VERSION);
        printf("%s\n", version_str);
    }

    glfwSwapInterval(0); // TODO: check

    // TODO: fix the size
    GLchar shader_info_buffer[200];
    GLint shader_info_len;

    // TODO: free
    char* vertex_shader_text = load_file("shaders/vert.glsl");
    char* fragment_shader_text = load_file("shaders/frag.glsl");

    // vertex shader
    {
        vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, (const char* const*)&vertex_shader_text, NULL);
        glCompileShader(vertex_shader);

        glGetShaderInfoLog(vertex_shader, 200, &shader_info_len, shader_info_buffer);

        if (shader_info_len) printf("Vertex Shader: %s\n", shader_info_buffer);
    }

    // frag shader
    {
        fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, (const char* const*)&fragment_shader_text, NULL);
        glCompileShader(fragment_shader);
        glGetShaderInfoLog(fragment_shader, 200, &shader_info_len, shader_info_buffer);
        if (shader_info_len) printf("Fragment Shader: %s\n", shader_info_buffer);
    }

    // shader program
    {
        program = glCreateProgram();
        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glLinkProgram(program);

        glGetProgramInfoLog(program, 200, &shader_info_len, shader_info_buffer);
        if (shader_info_len) printf("Shader Program: %s\n", shader_info_buffer);
    }

    // intialize misc stuff
    Sphere *sphere = create_sphere(12);

    float frame_time = (float) glfwGetTime();
    float last_time = (float) glfwGetTime();
    float last_fps_update = (float) glfwGetTime();
    int num_frames = 0;
    float physics_step = 1 / 300.0f;


    // initialize celestial bodies
    /*
     * In real life, the gravitational constant is approximately 6*10^(-11).
     * We can change this in our simulation to get a stronger gravity effect,
     * since we don't want to wait a whole year for the earth to go around the
     * sun once.
     */
#define SUN_SIZE_SCALE 25.0
#define ROCKY_SIZE_SCALE 700.0
#define GASSY_SIZE_SCALE 125.0
#define SUN_DIST_SCALE 1.0
#define ROCKY_DIST_SCALE 0.35
#define MOON_DIST_SCALE 20.0
#define GASSY_DIST_SCALE 0.155
    double gravitational_constant = 6 * glm::pow(10, 0); // TODO: tune this
    CelestialBody *sun = create_celestial_body(glm::dvec3(0.0), glm::dvec3(0.0), 333000.0, 0.0164 * 100, glm::vec3(0.97f, 0.45f, 0.1f), SUN_SIZE_SCALE, SUN_DIST_SCALE, NULL);
    // TODO: figure out why the game breaks if I put the same distance for two planets (i.e. 382.0)
    CelestialBody* mercury = create_celestial_body(glm::dvec3(382.0 * 0.4164, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 0.055, 0.0164 * 0.3829, glm::vec3(0.8f, 0.4f, 0.35f), ROCKY_SIZE_SCALE, ROCKY_DIST_SCALE, sun);
    CelestialBody *venus = create_celestial_body(glm::dvec3(279.48, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 0.81494, 0.0164 * 0.9499, glm::vec3(0.8f, 0.8f, 0.6f), ROCKY_SIZE_SCALE, ROCKY_DIST_SCALE, sun);
    CelestialBody *earth = create_celestial_body(glm::dvec3(382.0, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 1.0, 0.0164, glm::vec3(0.02f, 0.05f, 1.0f), ROCKY_SIZE_SCALE, ROCKY_DIST_SCALE, sun);
    CelestialBody *moon = create_celestial_body(glm::dvec3(383.0, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 0.0123, 0.0164 / 3.5, glm::vec3(0.8f, 0.8f, 0.8f), ROCKY_SIZE_SCALE, MOON_DIST_SCALE, earth);
    CelestialBody* mars = create_celestial_body(glm::dvec3(581.4, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 0.107, 0.0164 * 0.532, glm::vec3(0.95f, 0.25f, 0.2f), ROCKY_SIZE_SCALE, ROCKY_DIST_SCALE, sun);
    CelestialBody* jupiter = create_celestial_body(glm::dvec3(1938.65, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 317.81, 0.0164 * 10.97, glm::vec3(0.75f, 0.85f, 0.5f), GASSY_SIZE_SCALE, GASSY_DIST_SCALE, sun);
    CelestialBody* saturn = create_celestial_body(glm::dvec3(382.0 * 10.07, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 95.159, 0.0164 * 9.1402, glm::vec3(0.75f, 0.85f, 0.5f), GASSY_SIZE_SCALE, GASSY_DIST_SCALE * 0.7, sun);
    // we found the lagrange2 distance (3.69537571433) by trial and error, at 3.69537883714 it goes away from the sun and at 3.69527883714 it goes towards the sun
    CelestialBody* lagrange2 = create_celestial_body(glm::dvec3(382.0 + 3.69537571438, 0.0, 0.0), glm::dvec3(0.0, 0.0, 1.0), 0.00001, 0.0164 / 3.5, glm::vec3(0.0f, 1.0f, 0.0f), ROCKY_SIZE_SCALE, MOON_DIST_SCALE * 0.5, earth);
    CelestialBody* lagrange4 = create_celestial_body(glm::rotate(glm::vec3(earth->position), glm::radians(60.0f), glm::vec3(0.0, 1.0, 0.0)), glm::dvec3(0.0, 0.0, 1.0), 0.00001, 0.0164 / 3.5, glm::vec3(0.0f, 1.0f, 0.0f), ROCKY_SIZE_SCALE, ROCKY_DIST_SCALE, sun);
    double orbital_velocity_mag = std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - mercury->position)));
    mercury->velocity *= orbital_velocity_mag * (3.2/3.0);
    orbital_velocity_mag = std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - venus->position)));
    venus->velocity *= orbital_velocity_mag;
    orbital_velocity_mag = std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - earth->position)));
    earth->velocity *= orbital_velocity_mag;
    // Why does this work? Sounds like a such a coincidence.
    orbital_velocity_mag = std::sqrt((gravitational_constant * earth->mass) / (glm::length(earth->position - moon->position)));
    orbital_velocity_mag += std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - moon->position)));
    moon->velocity *= orbital_velocity_mag;
    orbital_velocity_mag = std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - mars->position)));
    mars->velocity *= orbital_velocity_mag * (3.1 / 3.0);
    orbital_velocity_mag = std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - jupiter->position)));
    jupiter->velocity *= orbital_velocity_mag;
    orbital_velocity_mag = std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - saturn->position)));
    saturn->velocity *= orbital_velocity_mag;
    orbital_velocity_mag = std::sqrt((gravitational_constant * earth->mass) / (glm::length(earth->position - lagrange2->position)));
    orbital_velocity_mag += std::sqrt((gravitational_constant * sun->mass) / (glm::length(sun->position - lagrange2->position)));
    lagrange2->velocity *= orbital_velocity_mag;
    lagrange4->velocity = glm::cross(glm::normalize(sun->position - lagrange4->position), glm::dvec3(0.0, 1.0, 0.0)) * -glm::length(earth->velocity);

    // Initialize global simulation state
    GlobalState global_state = { };
    global_state.rendering_mode = RENDER_MINIFIED;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = sun;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = mercury;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = venus;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = earth;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = moon;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = mars;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = jupiter;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = saturn;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = lagrange2;
    global_state.celestial_bodies[global_state.num_celestial_bodies++] = lagrange4;
    global_state.camera_target = -1;
    global_state.focused_camera_distance = FOCUSED_CAMERA_DIST;
    global_state.zoom_level = 10;
    global_state.enable_orbit_rendering = false;
    glfwSetWindowUserPointer(window, (void*) &global_state);

    // initialize GL buffers
    GLuint VAO, VBO, nVBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &nVBO);

    glBindVertexArray(VAO);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sphere->num_elements * sizeof(GLfloat), sphere->data, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void*)0);

        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, nVBO);
        glBufferData(GL_ARRAY_BUFFER, sphere->num_elements * sizeof(GLfloat), sphere->normals, GL_STATIC_DRAW);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void*)0);
    glBindVertexArray(0);

    GLuint pathVAO, pathVBO;
    glGenVertexArrays(1, &pathVAO);
    glGenBuffers(1, &pathVBO);

    glBindVertexArray(pathVAO);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, pathVBO);
    // ... call glBufferData() in real time, we update it every frame anyway
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void*)0);
    glBindVertexArray(0);

    double physics_accumulator = 0.0;
    POLL_GL_ERROR;
    while (!glfwWindowShouldClose(window)) {
        frame_time = glfwGetTime() - last_time;
        if (frame_time > 0.3) frame_time = 0.3; // we "pause" the simulation if rendering takes too long
        last_time += frame_time;
        physics_accumulator += frame_time;

        // Measure FPS
        double currentTime = glfwGetTime();
        num_frames++;
        if (currentTime - last_fps_update >= 1.0) {
            printf("%f ms/frame (%f FPS)\n", 1000.0 / double(num_frames), double(num_frames));
            num_frames = 0;
            last_fps_update += 1.0;
        }

        /* handle screen resize */
        // TODO: don't do this every frame, only when it changes!!
        float ratio;
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        ratio = width / (float)height;
        glm::mat4 proj_mat = glm::perspective<float>(glm::quarter_pi<float>(), ratio, 0.01f, 2000.0f);

        // set up gl context vars to start drawing
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glViewport(0, 0, width, height);
        glClearColor(0.0, 0.0, 0.0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);

        // TODO: interpolate between next physics frame and the accumulator remainder before rendering
        // physics
        while (physics_accumulator >= physics_step) {
            double delta_time = physics_step;

            // naive n-body simulation using particle-based Newton's laws of motion
            // we calculate all the velocities before update the position because it's more stable that way
            
            for (int i = 0; i < global_state.num_celestial_bodies; i++) {
                for (int j = 0; j < global_state.num_celestial_bodies; j++) {
                    if (i == j) continue; // a celestial body isn't affected by its own gravity

                    CelestialBody* c_i = global_state.celestial_bodies[i];
                    CelestialBody* c_j = global_state.celestial_bodies[j];

                    double distance_i_j = glm::length(c_i->position - c_j->position);
                    // the epsilon is used to avoid the gravity going to infinity when distance goes too close to zero
                    // TODO: this doesn't seem to help much, maybe we have to choose a bigger epsilon, but we don't want to break the simulation
                    double epsilon = 0.000001;
                    double gravity_force = ((double)gravitational_constant * c_i->mass * c_j->mass) / ((distance_i_j * distance_i_j) + epsilon);
                    c_i->velocity += (glm::normalize(c_j->position - c_i->position) * gravity_force * (1.0f / (double)c_i->mass)) * delta_time;
                }
            }

            for (int i = 0; i < global_state.num_celestial_bodies; i++) {
                CelestialBody* c_i = global_state.celestial_bodies[i];
                c_i->position += c_i->velocity * delta_time;
            }

            physics_accumulator -= delta_time;
        }

        // camera stuff
        glm::mat4 view_mat;
        if (global_state.camera_target == -1) {
            glm::vec3 camera_target(0);
            global_state.camera_pos = glm::vec3(0, 1000, 0);
            glm::vec3 camera_up(0, 0, -1);
            view_mat = glm::lookAt(global_state.camera_pos, camera_target, camera_up);
        }
        else {
            CelestialBody* target = global_state.celestial_bodies[global_state.camera_target];
            glm::vec3 camera_target(0);
            switch (global_state.rendering_mode) {
            case RENDER_TO_SCALE:
                camera_target = target->position;
                break;
            case RENDER_MINIFIED:
                camera_target = target->anchor ? glm::vec3(target->anchor->position * target->anchor->minified_dist_scale) : glm::vec3(0);
                camera_target += (target->position - (target->anchor ? target->anchor->position : glm::dvec3(0))) * target->minified_dist_scale;
                break;
            default:
                fprintf(stderr, "Error: Invalid rendering mode.\n");
                exit(-1);
            }
            glm::vec3 camera_up(0, 1, 0);
            global_state.camera_pos = ((glm::vec3(target->position) + glm::normalize(glm::vec3(target->velocity)) * (target->size + global_state.focused_camera_distance)) - glm::vec3(target->position));
            global_state.camera_pos += glm::vec3(target->position);
            global_state.camera_pos += glm::vec3(0, global_state.focused_camera_distance / 2, 0); // offset from the plane a little bit // TODO: parameterize this?
            //fprintf(stderr, "camera position: %f %f %f\n", camera_pos.x, camera_pos.y, camera_pos.z);
            view_mat = glm::lookAt(global_state.camera_pos, camera_target, camera_up);
        }
        // make view-projection matrix
        glm::mat4 view_proj_mat = proj_mat * view_mat;
        glUniformMatrix4fv(glGetUniformLocation(program, "view_proj"), 1, GL_FALSE, glm::value_ptr(view_proj_mat));

        // rendering
        for (int i = 0; i < global_state.num_celestial_bodies; i++) {
            render_celestial_body(&global_state, VAO, pathVAO, pathVBO, program, sphere, global_state.celestial_bodies[i]);
        }

        // finished rendering the frame
        glfwSwapBuffers(window);
        POLL_GL_ERROR;
        glfwPollEvents();
    }
}
