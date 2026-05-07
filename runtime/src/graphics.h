#pragma once

#include <cstdint>

struct Tile {
  float x, y;
  float height;
  std::uint32_t resource_type;  // 0=empty, 1=iron, 2=copper, 3=coal, 4=water
  float resource_amount;
};

class Graphics {
 public:
  Graphics();
  ~Graphics();

  bool Initialize(int width, int height, const char* title);
  bool ShouldClose() const;
  void BeginFrame();
  void EndFrame();

  void RenderTile(const Tile& tile);
  void RenderGrid(int width, int height);

  void SetClearColor(float r, float g, float b, float a = 1.0f);

 private:
  int window_width_ = 0;
  int window_height_ = 0;
  void* window_handle_ = nullptr;
  std::uint32_t vao_ = 0, vbo_ = 0;
  std::uint32_t shader_program_ = 0;

  void SetupShaders();
  void SetupGeometry();
  void Cleanup();
};
