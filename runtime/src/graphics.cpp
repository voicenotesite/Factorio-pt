#include "graphics.h"
#include <iostream>
#include <GLFW/glfw3.h>
#include <GL/gl.h>

Graphics::Graphics() = default;

Graphics::~Graphics() {
  Cleanup();
}

bool Graphics::Initialize(int width, int height, const char* title) {
  window_width_ = width;
  window_height_ = height;

  if (!glfwInit()) {
    std::cout << "[ERROR] GLFW initialization failed.\n";
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!window) {
    std::cout << "[ERROR] GLFW window creation failed.\n";
    glfwTerminate();
    return false;
  }

  window_handle_ = window;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  SetupShaders();
  SetupGeometry();

  std::cout << "[GRAPHICS] Window initialized (" << width << "x" << height << ").\n";
  return true;
}

bool Graphics::ShouldClose() const {
  if (!window_handle_) return true;
  return glfwWindowShouldClose(static_cast<GLFWwindow*>(window_handle_));
}

void Graphics::BeginFrame() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Graphics::EndFrame() {
  if (window_handle_) {
    glfwSwapBuffers(static_cast<GLFWwindow*>(window_handle_));
    glfwPollEvents();
  }
}

void Graphics::SetClearColor(float r, float g, float b, float a) {
  glClearColor(r, g, b, a);
}

void Graphics::RenderTile(const Tile& tile) {
  if (!shader_program_) return;

  glUseProgram(shader_program_);
  glBindVertexArray(vao_);

  float colors[4] = {0.5f, 0.5f, 0.5f, 1.0f};

  switch (tile.resource_type) {
    case 1:
      colors[0] = 0.7f;
      colors[1] = 0.4f;
      colors[2] = 0.1f;
      break;
    case 2:
      colors[0] = 0.4f;
      colors[1] = 0.6f;
      colors[2] = 0.8f;
      break;
    case 3:
      colors[0] = 0.2f;
      colors[1] = 0.2f;
      colors[2] = 0.2f;
      break;
    case 4:
      colors[0] = 0.2f;
      colors[1] = 0.5f;
      colors[2] = 0.8f;
      break;
  }

  glUniform4fv(glGetUniformLocation(shader_program_, "tile_color"), 1, colors);
  glUniform2f(glGetUniformLocation(shader_program_, "tile_pos"), tile.x, tile.y);

  glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Graphics::RenderGrid(int width, int height) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      Tile t;
      t.x = static_cast<float>(x);
      t.y = static_cast<float>(y);
      t.height = 0.5f;
      t.resource_type = (x + y) % 5;
      t.resource_amount = 100.0f;

      RenderTile(t);
    }
  }
}

void Graphics::SetupShaders() {
  const char* vertex_src = R"(
    #version 330 core
    layout(location = 0) in vec3 position;
    uniform vec2 tile_pos;
    out vec3 frag_color;
    void main() {
      gl_Position = vec4(position.x + tile_pos.x * 0.1f, position.y + tile_pos.y * 0.1f, position.z, 1.0);
    }
  )";

  const char* fragment_src = R"(
    #version 330 core
    uniform vec4 tile_color;
    out vec4 frag_out;
    void main() {
      frag_out = tile_color;
    }
  )";

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vertex_src, nullptr);
  glCompileShader(vs);

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fragment_src, nullptr);
  glCompileShader(fs);

  shader_program_ = glCreateProgram();
  glAttachShader(shader_program_, vs);
  glAttachShader(shader_program_, fs);
  glLinkProgram(shader_program_);

  glDeleteShader(vs);
  glDeleteShader(fs);

  std::cout << "[GRAPHICS] Shaders compiled.\n";
}

void Graphics::SetupGeometry() {
  float vertices[] = {
      0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f, 0.1f, 0.1f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.0f, 0.0f, 0.1f, 0.0f,
  };

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  std::cout << "[GRAPHICS] Geometry setup.\n";
}

void Graphics::Cleanup() {
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (shader_program_) glDeleteProgram(shader_program_);

  if (window_handle_) {
    glfwDestroyWindow(static_cast<GLFWwindow*>(window_handle_));
  }
  glfwTerminate();

  std::cout << "[GRAPHICS] Cleanup complete.\n";
}
