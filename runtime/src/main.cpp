#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

struct SimSnapshot {
  float stability;
  float pollution;
  float wage_index;
  float tax_rate;
};

struct PlanetSummary {
  std::uint32_t seed;
  std::uint32_t core_profile_id;
  std::uint32_t width;
  std::uint32_t height;
  float avg_height;
  std::uint32_t lowland_tiles;
  std::uint32_t midland_tiles;
  std::uint32_t highland_tiles;
  std::uint32_t water_tiles;
  std::uint32_t mountain_tiles;
  std::uint32_t iron_tiles;
  std::uint32_t copper_tiles;
  std::uint32_t coal_tiles;
  std::uint32_t unique_sites;
};

struct SystemSummary {
  std::uint32_t seed;
  std::uint32_t planet_count;
  std::uint32_t core_planets;
  std::uint32_t procedural_planets;
  float avg_height_across_planets;
  std::uint32_t total_unique_sites;
};

enum class TerrainType : std::uint8_t { Lowland, Midland, Highland, Water, Mountain };
enum class ResourceType : std::uint8_t { None, Iron, Copper, Coal };
enum class VisualKind : std::uint8_t {
  Lowland,
  Midland,
  Highland,
  Water,
  Mountain,
  Iron,
  Copper,
  Coal,
  Player
};

struct WorldTile {
  TerrainType terrain;
  ResourceType resource;
  std::uint8_t height_level;
};

static std::uint32_t Hash2D(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
  std::uint32_t h = seed ^ (x * 73856093u) ^ (y * 19349663u);
  h ^= (h >> 13);
  h *= 1274126177u;
  h ^= (h >> 16);
  return h;
}

static std::uint32_t PackColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8u) | (static_cast<std::uint32_t>(b) << 16u);
}

static std::uint8_t GetR(std::uint32_t c) { return static_cast<std::uint8_t>(c & 0xFFu); }
static std::uint8_t GetG(std::uint32_t c) { return static_cast<std::uint8_t>((c >> 8u) & 0xFFu); }
static std::uint8_t GetB(std::uint32_t c) { return static_cast<std::uint8_t>((c >> 16u) & 0xFFu); }

static std::uint32_t MulColor(std::uint32_t c, float factor) {
  factor = std::clamp(factor, 0.0f, 2.0f);
  auto mul = [factor](std::uint8_t v) -> std::uint8_t {
    const int out = static_cast<int>(std::lround(static_cast<float>(v) * factor));
    return static_cast<std::uint8_t>(std::clamp(out, 0, 255));
  };
  return PackColor(mul(GetR(c)), mul(GetG(c)), mul(GetB(c)));
}

static std::uint32_t LerpColor(std::uint32_t a, std::uint32_t b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  auto lerp = [t](std::uint8_t va, std::uint8_t vb) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::lround(static_cast<float>(va) + (static_cast<float>(vb) - static_cast<float>(va)) * t));
  };
  return PackColor(lerp(GetR(a), GetR(b)), lerp(GetG(a), GetG(b)), lerp(GetB(a), GetB(b)));
}

struct AiTextureGenerator {
  static constexpr int kTextureSize = 16;
  using Texture = std::array<std::uint32_t, kTextureSize * kTextureSize>;

  Texture Generate(VisualKind kind, std::uint32_t seed, float theme_shift) const {
    Texture out{};
    const float latent_a = static_cast<float>(seed & 0xFFu) / 255.0f;
    const float latent_b = static_cast<float>((seed >> 8u) & 0xFFu) / 255.0f;
    const float latent_c = static_cast<float>((seed >> 16u) & 0xFFu) / 255.0f;

    auto base = BaseColor(kind, theme_shift);
    for (int y = 0; y < kTextureSize; ++y) {
      for (int x = 0; x < kTextureSize; ++x) {
        const float xf = static_cast<float>(x);
        const float yf = static_cast<float>(y);
        const float n = Noise(xf, yf, latent_a, latent_b, latent_c);
        const float wave = 0.5f + 0.5f * std::sin((xf + latent_b * 8.0f) * 0.8f + (yf + latent_c * 6.0f) * 0.45f);

        std::array<float, 3> rgb = base;
        const float detail = (n - 0.5f) * 0.28f;
        rgb[0] += detail;
        rgb[1] += detail;
        rgb[2] += detail;

        ApplyMaterialSignature(kind, x, y, wave, n, latent_a, rgb);
        out[static_cast<std::size_t>(y * kTextureSize + x)] =
            PackColor(ToByte(rgb[0]), ToByte(rgb[1]), ToByte(rgb[2]));
      }
    }
    return out;
  }

 private:
  static float Noise(float x, float y, float a, float b, float c) {
    const float n = std::sin((x + a * 41.0f) * 12.9898f + (y + b * 29.0f) * 78.233f + c * 37.719f) * 43758.5453f;
    return n - std::floor(n);
  }

  static std::array<float, 3> BaseColor(VisualKind kind, float shift) {
    shift = std::clamp(shift, -0.16f, 0.16f);
    switch (kind) {
      case VisualKind::Water: return {0.12f + shift * 0.3f, 0.35f, 0.65f + shift * 0.7f};
      case VisualKind::Mountain: return {0.44f + shift * 0.4f, 0.33f, 0.28f};
      case VisualKind::Highland: return {0.56f + shift * 0.5f, 0.47f + shift * 0.2f, 0.31f};
      case VisualKind::Midland: return {0.33f, 0.52f + shift * 0.4f, 0.26f};
      case VisualKind::Lowland: return {0.23f, 0.44f + shift * 0.5f, 0.21f};
      case VisualKind::Iron: return {0.61f, 0.63f, 0.67f};
      case VisualKind::Copper: return {0.76f, 0.43f, 0.23f};
      case VisualKind::Coal: return {0.18f, 0.18f, 0.20f};
      case VisualKind::Player: return {0.94f, 0.95f, 0.96f};
    }
    return {0.5f, 0.5f, 0.5f};
  }

  static void ApplyMaterialSignature(VisualKind kind,
                                     int x,
                                     int y,
                                     float wave,
                                     float n,
                                     float latent,
                                     std::array<float, 3>& rgb) {
    switch (kind) {
      case VisualKind::Iron: {
        const bool vein = ((x + y + static_cast<int>(latent * 13.0f)) % 5) == 0;
        if (vein) {
          rgb[0] -= 0.20f;
          rgb[1] -= 0.20f;
          rgb[2] -= 0.22f;
        }
        if (n > 0.82f) {
          rgb[0] += 0.14f;
          rgb[1] += 0.14f;
          rgb[2] += 0.16f;
        }
        break;
      }
      case VisualKind::Copper: {
        const bool oxidation = ((x * 3 + y * 5 + static_cast<int>(latent * 19.0f)) % 17) == 0;
        if (oxidation) {
          rgb[0] -= 0.18f;
          rgb[1] += 0.13f;
          rgb[2] += 0.10f;
        }
        rgb[0] += wave * 0.08f;
        break;
      }
      case VisualKind::Coal: {
        const bool crack = ((x + y * 2 + static_cast<int>(latent * 23.0f)) % 9) == 0;
        if (crack) {
          rgb[0] += 0.20f;
          rgb[1] += 0.20f;
          rgb[2] += 0.20f;
        }
        rgb[0] -= 0.05f;
        rgb[1] -= 0.05f;
        rgb[2] -= 0.05f;
        break;
      }
      case VisualKind::Water: {
        rgb[2] += wave * 0.11f;
        rgb[1] += wave * 0.05f;
        break;
      }
      case VisualKind::Mountain: {
        rgb[0] += std::max(0.0f, wave - 0.72f) * 0.2f;
        rgb[1] += std::max(0.0f, wave - 0.72f) * 0.12f;
        break;
      }
      case VisualKind::Highland:
      case VisualKind::Midland:
      case VisualKind::Lowland: {
        rgb[1] += wave * 0.06f;
        break;
      }
      case VisualKind::Player: {
        rgb = {0.94f, 0.95f, 0.96f};
        break;
      }
    }
  }

  static std::uint8_t ToByte(float v) {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(clamped * 255.0f);
  }
};

#ifdef _WIN32
using SimBootstrapFn = SimSnapshot (*)();
using SimTickFn = SimSnapshot (*)(float);
using SimSetPolicyFn = SimSnapshot (*)(float, float);
using SimGeneratePlanetFn = PlanetSummary (*)(std::uint32_t, std::uint32_t, std::uint32_t);
using SimGenerateSystemFn = SystemSummary (*)(std::uint32_t);

struct RuntimeState {
  static constexpr int kTileWidth = 48;
  static constexpr int kTileHeight = 24;
  static constexpr int kHeightStepPx = 8;
  static constexpr int kHudWidth = 340;

  HWND hwnd = nullptr;
  HDC back_dc = nullptr;
  HBITMAP back_bitmap = nullptr;
  std::uint32_t* back_pixels = nullptr;
  int client_w = 1360;
  int client_h = 820;
  bool running = true;

  int viewport_tiles_w = 26;
  int viewport_tiles_h = 26;
  int world_w = 128;
  int world_h = 128;
  int camera_x = 0;
  int camera_y = 0;
  int player_x = 8;
  int player_y = 6;

  int world_origin_x = 0;
  int world_origin_y = 70;

  std::uint32_t run_seed = 0;
  std::uint32_t frame_counter = 0;
  float fps = 0.0f;
  float frame_time_ms = 0.0f;
  float water_anim = 0.0f;
  float theme_shift = 0.0f;

  SimSnapshot snapshot{};
  PlanetSummary world{};
  SystemSummary system{};
  std::vector<WorldTile> tiles;
  AiTextureGenerator texture_gen;
  std::unordered_map<std::uint32_t, AiTextureGenerator::Texture> texture_cache;
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, w_param, l_param);
}

bool InitBackBuffer(RuntimeState& state) {
  if (state.back_dc != nullptr) {
    DeleteDC(state.back_dc);
    state.back_dc = nullptr;
  }
  if (state.back_bitmap != nullptr) {
    DeleteObject(state.back_bitmap);
    state.back_bitmap = nullptr;
  }

  HDC window_dc = GetDC(state.hwnd);
  if (window_dc == nullptr) return false;

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = state.client_w;
  bmi.bmiHeader.biHeight = -state.client_h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  state.back_dc = CreateCompatibleDC(window_dc);
  state.back_bitmap = CreateDIBSection(window_dc, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&state.back_pixels), nullptr, 0);
  if (state.back_dc == nullptr || state.back_bitmap == nullptr || state.back_pixels == nullptr) {
    ReleaseDC(state.hwnd, window_dc);
    return false;
  }
  SelectObject(state.back_dc, state.back_bitmap);
  ReleaseDC(state.hwnd, window_dc);
  return true;
}

VisualKind TerrainToVisual(TerrainType t) {
  switch (t) {
    case TerrainType::Water: return VisualKind::Water;
    case TerrainType::Mountain: return VisualKind::Mountain;
    case TerrainType::Highland: return VisualKind::Highland;
    case TerrainType::Midland: return VisualKind::Midland;
    case TerrainType::Lowland: return VisualKind::Lowland;
  }
  return VisualKind::Lowland;
}

VisualKind ResourceToVisual(ResourceType r) {
  switch (r) {
    case ResourceType::Iron: return VisualKind::Iron;
    case ResourceType::Copper: return VisualKind::Copper;
    case ResourceType::Coal: return VisualKind::Coal;
    case ResourceType::None: break;
  }
  return VisualKind::Lowland;
}

const AiTextureGenerator::Texture& GetTexture(RuntimeState& state, VisualKind kind, std::uint32_t variant_seed) {
  const std::uint32_t key = (static_cast<std::uint32_t>(kind) << 24u) | (variant_seed & 0x00FFFFFFu);
  const auto it = state.texture_cache.find(key);
  if (it != state.texture_cache.end()) return it->second;
  return state.texture_cache.emplace(key, state.texture_gen.Generate(kind, key ^ state.run_seed, state.theme_shift)).first->second;
}

void PutPixel(RuntimeState& state, int x, int y, std::uint32_t color) {
  if (x < 0 || y < 0 || x >= state.client_w || y >= state.client_h) return;
  state.back_pixels[static_cast<std::size_t>(y * state.client_w + x)] = color;
}

void DrawIsoTopTextured(RuntimeState& state,
                        int cx,
                        int cy,
                        const AiTextureGenerator::Texture& tex,
                        float brightness) {
  const int half_w = RuntimeState::kTileWidth / 2;
  const int half_h = RuntimeState::kTileHeight / 2;
  for (int dy = -half_h; dy <= half_h; ++dy) {
    const int span = (half_w * (half_h - std::abs(dy))) / half_h;
    if (span <= 0) continue;
    const float v = static_cast<float>(dy + half_h) / static_cast<float>(half_h * 2);
    const int ty = std::clamp(static_cast<int>(v * (AiTextureGenerator::kTextureSize - 1)), 0, AiTextureGenerator::kTextureSize - 1);
    for (int dx = -span; dx <= span; ++dx) {
      const float u = static_cast<float>(dx + span) / static_cast<float>(span * 2);
      const int tx = std::clamp(static_cast<int>(u * (AiTextureGenerator::kTextureSize - 1)), 0, AiTextureGenerator::kTextureSize - 1);
      const auto src = tex[static_cast<std::size_t>(ty * AiTextureGenerator::kTextureSize + tx)];
      PutPixel(state, cx + dx, cy + dy, MulColor(src, brightness));
    }
  }
}

void DrawIsoSides(RuntimeState& state, int cx, int top_cy, int height_px, std::uint32_t top_color) {
  if (height_px <= 0) return;
  const int half_w = RuntimeState::kTileWidth / 2;
  const int half_h = RuntimeState::kTileHeight / 2;

  POINT left_side[4] = {
      {cx - half_w, top_cy},
      {cx, top_cy + half_h},
      {cx, top_cy + half_h + height_px},
      {cx - half_w, top_cy + height_px},
  };
  POINT right_side[4] = {
      {cx + half_w, top_cy},
      {cx, top_cy + half_h},
      {cx, top_cy + half_h + height_px},
      {cx + half_w, top_cy + height_px},
  };

  const COLORREF left_col = static_cast<COLORREF>(MulColor(top_color, 0.65f));
  const COLORREF right_col = static_cast<COLORREF>(MulColor(top_color, 0.52f));
  HBRUSH left_brush = CreateSolidBrush(left_col);
  HBRUSH right_brush = CreateSolidBrush(right_col);
  HPEN no_pen = static_cast<HPEN>(GetStockObject(NULL_PEN));

  HGDIOBJ old_pen = SelectObject(state.back_dc, no_pen);
  HGDIOBJ old_brush = SelectObject(state.back_dc, left_brush);
  Polygon(state.back_dc, left_side, 4);
  SelectObject(state.back_dc, right_brush);
  Polygon(state.back_dc, right_side, 4);

  SelectObject(state.back_dc, old_brush);
  SelectObject(state.back_dc, old_pen);
  DeleteObject(left_brush);
  DeleteObject(right_brush);
}

void DrawIsoOutline(RuntimeState& state, int cx, int cy, std::uint32_t color) {
  const int half_w = RuntimeState::kTileWidth / 2;
  const int half_h = RuntimeState::kTileHeight / 2;
  HPEN pen = CreatePen(PS_SOLID, 1, static_cast<COLORREF>(color));
  HGDIOBJ old_pen = SelectObject(state.back_dc, pen);
  HGDIOBJ old_brush = SelectObject(state.back_dc, GetStockObject(NULL_BRUSH));

  POINT pts[4] = {
      {cx, cy - half_h},
      {cx + half_w, cy},
      {cx, cy + half_h},
      {cx - half_w, cy},
  };
  Polygon(state.back_dc, pts, 4);

  SelectObject(state.back_dc, old_brush);
  SelectObject(state.back_dc, old_pen);
  DeleteObject(pen);
}

void DrawResourceGlyph(RuntimeState& state, ResourceType resource, int cx, int cy) {
  if (resource == ResourceType::None) return;
  COLORREF col = RGB(210, 210, 210);
  if (resource == ResourceType::Iron) col = RGB(185, 192, 208);
  if (resource == ResourceType::Copper) col = RGB(214, 141, 88);
  if (resource == ResourceType::Coal) col = RGB(130, 130, 130);

  HBRUSH brush = CreateSolidBrush(col);
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(35, 35, 35));
  HGDIOBJ old_brush = SelectObject(state.back_dc, brush);
  HGDIOBJ old_pen = SelectObject(state.back_dc, pen);

  if (resource == ResourceType::Iron) {
    Rectangle(state.back_dc, cx - 4, cy - 3, cx + 5, cy + 4);
    Rectangle(state.back_dc, cx - 2, cy - 6, cx + 3, cy - 2);
  } else if (resource == ResourceType::Copper) {
    Ellipse(state.back_dc, cx - 5, cy - 5, cx + 6, cy + 6);
    Ellipse(state.back_dc, cx - 2, cy - 2, cx + 3, cy + 3);
  } else if (resource == ResourceType::Coal) {
    POINT tri[3] = {{cx - 5, cy + 4}, {cx + 5, cy + 4}, {cx, cy - 5}};
    Polygon(state.back_dc, tri, 3);
  }

  SelectObject(state.back_dc, old_pen);
  SelectObject(state.back_dc, old_brush);
  DeleteObject(brush);
  DeleteObject(pen);
}

void GenerateWorld(RuntimeState& state) {
  state.tiles.resize(static_cast<std::size_t>(state.world_w * state.world_h));
  const std::uint32_t area = state.world.width * state.world.height;
  const std::uint32_t water_cut = (state.world.water_tiles * 100u) / area;
  const std::uint32_t mountain_cut = ((state.world.water_tiles + state.world.mountain_tiles) * 100u) / area;
  const std::uint32_t high_cut = ((state.world.water_tiles + state.world.mountain_tiles + state.world.highland_tiles) * 100u) / area;
  const std::uint32_t mid_cut =
      ((state.world.water_tiles + state.world.mountain_tiles + state.world.highland_tiles + state.world.midland_tiles) * 100u) / area;

  const std::uint32_t iron_cut = (state.world.iron_tiles * 100u) / area;
  const std::uint32_t copper_cut = ((state.world.iron_tiles + state.world.copper_tiles) * 100u) / area;
  const std::uint32_t coal_cut = ((state.world.iron_tiles + state.world.copper_tiles + state.world.coal_tiles) * 100u) / area;

  for (int y = 0; y < state.world_h; ++y) {
    for (int x = 0; x < state.world_w; ++x) {
      const std::uint32_t hash = Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), state.world.seed) % 100u;
      const std::uint32_t rhash = Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), state.world.seed ^ 0xBADC0DEu) % 100u;
      const std::uint32_t hhash = Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), state.world.seed ^ 0xA11CEu) % 100u;

      TerrainType terrain = TerrainType::Lowland;
      if (hash < water_cut) terrain = TerrainType::Water;
      else if (hash < mountain_cut) terrain = TerrainType::Mountain;
      else if (hash < high_cut) terrain = TerrainType::Highland;
      else if (hash < mid_cut) terrain = TerrainType::Midland;

      ResourceType resource = ResourceType::None;
      if (rhash < iron_cut) resource = ResourceType::Iron;
      else if (rhash < copper_cut) resource = ResourceType::Copper;
      else if (rhash < coal_cut) resource = ResourceType::Coal;

      std::uint8_t level = 1;
      switch (terrain) {
        case TerrainType::Water: level = 0; break;
        case TerrainType::Lowland: level = static_cast<std::uint8_t>(1 + (hhash % 2u)); break;
        case TerrainType::Midland: level = static_cast<std::uint8_t>(2 + (hhash % 2u)); break;
        case TerrainType::Highland: level = static_cast<std::uint8_t>(3 + (hhash % 2u)); break;
        case TerrainType::Mountain: level = static_cast<std::uint8_t>(4 + (hhash % 2u)); break;
      }

      state.tiles[static_cast<std::size_t>(y * state.world_w + x)] = {terrain, resource, level};
    }
  }
}

void DrawProgressBar(RuntimeState& state, int x, int y, int w, int h, float t, COLORREF good_col) {
  t = std::clamp(t, 0.0f, 1.0f);
  HBRUSH bg = CreateSolidBrush(RGB(34, 41, 53));
  RECT r{x, y, x + w, y + h};
  FillRect(state.back_dc, &r, bg);
  DeleteObject(bg);

  HBRUSH fg = CreateSolidBrush(good_col);
  RECT fr{x + 1, y + 1, x + 1 + static_cast<int>((w - 2) * t), y + h - 1};
  FillRect(state.back_dc, &fr, fg);
  DeleteObject(fg);
}

void DrawHud(RuntimeState& state) {
  RECT hud_rect{
      state.client_w - RuntimeState::kHudWidth,
      0,
      state.client_w,
      state.client_h,
  };
  HBRUSH panel_brush = CreateSolidBrush(RGB(19, 24, 33));
  FillRect(state.back_dc, &hud_rect, panel_brush);
  DeleteObject(panel_brush);

  HPEN border = CreatePen(PS_SOLID, 1, RGB(59, 69, 87));
  HGDIOBJ old_pen = SelectObject(state.back_dc, border);
  MoveToEx(state.back_dc, hud_rect.left, 0, nullptr);
  LineTo(state.back_dc, hud_rect.left, state.client_h);
  SelectObject(state.back_dc, old_pen);
  DeleteObject(border);

  SetBkMode(state.back_dc, TRANSPARENT);
  const int x = hud_rect.left + 18;
  int y = 14;

  auto draw_line = [&](const std::string& text, COLORREF color, int line_h = 22) {
    SetTextColor(state.back_dc, color);
    TextOutA(state.back_dc, x, y, text.c_str(), static_cast<int>(text.size()));
    y += line_h;
  };

  draw_line("FACTORIO-PT // VISUAL PASS", RGB(154, 225, 255), 26);
  draw_line("Runtime: Win32/GDI pseudo-iso", RGB(196, 204, 219));
  draw_line("AI textures: dynamic per run", RGB(196, 204, 219));
  y += 4;

  std::ostringstream fps;
  fps << "FPS " << static_cast<int>(state.fps) << "   Frame " << state.frame_time_ms << " ms";
  draw_line(fps.str(), RGB(233, 235, 242));

  std::ostringstream pos;
  pos << "Player (" << state.player_x << ", " << state.player_y << ")";
  draw_line(pos.str(), RGB(233, 235, 242));

  std::ostringstream cam;
  cam << "Camera (" << state.camera_x << ", " << state.camera_y << ")";
  draw_line(cam.str(), RGB(233, 235, 242));

  y += 8;
  draw_line("SIM METRICS", RGB(255, 219, 148));
  draw_line("Stability", RGB(210, 220, 235), 18);
  DrawProgressBar(state, x, y, 278, 16, state.snapshot.stability, RGB(104, 212, 127));
  y += 24;

  draw_line("Pollution", RGB(210, 220, 235), 18);
  DrawProgressBar(state, x, y, 278, 16, state.snapshot.pollution, RGB(227, 122, 112));
  y += 24;

  std::ostringstream econ;
  econ << "Wage " << state.snapshot.wage_index << "   Tax " << state.snapshot.tax_rate;
  draw_line(econ.str(), RGB(233, 235, 242));

  y += 8;
  draw_line("RESOURCE READABILITY", RGB(255, 219, 148));
  draw_line("Iron: bright-cold veins", RGB(203, 210, 225));
  draw_line("Copper: warm oxidized nodes", RGB(220, 162, 112));
  draw_line("Coal: dark sharp cracks", RGB(174, 178, 190));

  y += 8;
  draw_line("CONTROLS", RGB(154, 225, 255));
  draw_line("WASD = move  |  IJKL = pan", RGB(233, 235, 242));
  draw_line("R = new world style seed", RGB(233, 235, 242));
  draw_line("ESC / Q = quit", RGB(233, 235, 242));
}

void DrawSkyGradient(RuntimeState& state) {
  const std::uint32_t top = PackColor(18, 24, 36);
  const std::uint32_t mid = PackColor(32, 44, 62);
  const std::uint32_t bottom = PackColor(17, 20, 28);
  for (int y = 0; y < state.client_h; ++y) {
    const float t = static_cast<float>(y) / static_cast<float>(state.client_h - 1);
    std::uint32_t col = t < 0.5f ? LerpColor(top, mid, t * 2.0f) : LerpColor(mid, bottom, (t - 0.5f) * 2.0f);
    std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.client_w);
    std::fill(row, row + state.client_w, col);
  }
}

void RenderWorld(RuntimeState& state) {
  const int world_w_px = state.client_w - RuntimeState::kHudWidth;
  state.world_origin_x = world_w_px / 2;

  const int max_sum = state.viewport_tiles_w + state.viewport_tiles_h - 2;
  for (int s = 0; s <= max_sum; ++s) {
    const int tx_min = std::max(0, s - (state.viewport_tiles_h - 1));
    const int tx_max = std::min(state.viewport_tiles_w - 1, s);
    for (int tx = tx_min; tx <= tx_max; ++tx) {
      const int ty = s - tx;
      const int wx = state.camera_x + tx;
      const int wy = state.camera_y + ty;
      if (wx < 0 || wy < 0 || wx >= state.world_w || wy >= state.world_h) continue;
      const auto& tile = state.tiles[static_cast<std::size_t>(wy * state.world_w + wx)];

      const int base_x = state.world_origin_x + (tx - ty) * (RuntimeState::kTileWidth / 2);
      const int base_y = state.world_origin_y + (tx + ty) * (RuntimeState::kTileHeight / 2);
      const int elevation_px = static_cast<int>(tile.height_level) * RuntimeState::kHeightStepPx;
      const int top_cy = base_y - elevation_px;

      VisualKind kind = TerrainToVisual(tile.terrain);
      if (tile.resource != ResourceType::None) kind = ResourceToVisual(tile.resource);

      const std::uint32_t variant = Hash2D(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy), state.run_seed) & 0xFFu;
      const auto& tex = GetTexture(state, kind, variant);
      const std::uint32_t side_base = tex[static_cast<std::size_t>((AiTextureGenerator::kTextureSize / 2) * AiTextureGenerator::kTextureSize +
                                                                   (AiTextureGenerator::kTextureSize / 2))];

      float brightness = 1.0f;
      if (tile.terrain == TerrainType::Water) {
        const float anim = 0.96f + std::sin(state.water_anim + static_cast<float>((wx + wy) % 16)) * 0.05f;
        brightness = anim;
      } else if (tile.terrain == TerrainType::Mountain) {
        brightness = 0.92f;
      }

      DrawIsoSides(state, base_x, top_cy, elevation_px, side_base);
      DrawIsoTopTextured(state, base_x, top_cy, tex, brightness);
      DrawIsoOutline(state, base_x, top_cy, MulColor(side_base, 0.45f));
      DrawResourceGlyph(state, tile.resource, base_x, top_cy);

      if (wx == state.player_x && wy == state.player_y) {
        DrawIsoOutline(state, base_x, top_cy - 1, PackColor(255, 255, 255));
        DrawIsoOutline(state, base_x, top_cy - 3, PackColor(252, 215, 126));
      }
    }
  }
}

void Render(RuntimeState& state) {
  DrawSkyGradient(state);
  RenderWorld(state);

  if (state.snapshot.pollution > 0.30f) {
    const float haze = std::clamp((state.snapshot.pollution - 0.30f) * 1.2f, 0.0f, 0.5f);
    const std::uint32_t haze_color = PackColor(62, 52, 44);
    for (int y = 0; y < state.client_h; ++y) {
      std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.client_w);
      for (int x = 0; x < state.client_w - RuntimeState::kHudWidth; ++x) {
        row[x] = LerpColor(row[x], haze_color, haze);
      }
    }
  }

  DrawHud(state);
  HDC window_dc = GetDC(state.hwnd);
  BitBlt(window_dc, 0, 0, state.client_w, state.client_h, state.back_dc, 0, 0, SRCCOPY);
  ReleaseDC(state.hwnd, window_dc);
}

bool KeyEdge(int vk, bool& prev_state) {
  const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
  const bool edge = down && !prev_state;
  prev_state = down;
  return edge;
}

bool HandleResize(RuntimeState& state) {
  RECT rc{};
  if (!GetClientRect(state.hwnd, &rc)) return false;
  const int w = std::max(1, static_cast<int>(rc.right - rc.left));
  const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));
  if (w == state.client_w && h == state.client_h) return true;

  state.client_w = w;
  state.client_h = h;
  const int world_w_px = state.client_w - RuntimeState::kHudWidth;
  state.viewport_tiles_w = std::max(8, world_w_px / RuntimeState::kTileWidth);
  state.viewport_tiles_h = std::max(8, state.client_h / RuntimeState::kTileHeight);
  return InitBackBuffer(state);
}

void ReseedWorldStyle(RuntimeState& state) {
  state.run_seed ^= 0x9E3779B9u + state.frame_counter;
  state.theme_shift = static_cast<float>((state.run_seed & 0x7Fu)) / 127.0f * 0.32f - 0.16f;
  state.texture_cache.clear();
  GenerateWorld(state);
}

int RunRuntimeWindow(SimTickFn sim_tick,
                     SimSetPolicyFn sim_set_policy,
                     SimGeneratePlanetFn sim_generate_planet,
                     SimGenerateSystemFn sim_generate_system) {
  RuntimeState state{};
  const std::uint32_t boot_seed = static_cast<std::uint32_t>(GetTickCount64());
  state.system = sim_generate_system(2026u);
  state.world = sim_generate_planet(boot_seed ^ 0x5A17u, 128u, 128u);
  state.run_seed = state.system.seed ^ state.world.seed ^ boot_seed;
  state.theme_shift = static_cast<float>((state.run_seed & 0x7Fu)) / 127.0f * 0.32f - 0.16f;
  state.world_w = static_cast<int>(state.world.width);
  state.world_h = static_cast<int>(state.world.height);
  state.player_x = state.world_w / 2;
  state.player_y = state.world_h / 2;

  WNDCLASSA wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = "FactorioPtWindowClass";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (!RegisterClassA(&wc)) {
    std::cout << "[ERROR] RegisterClass failed.\n";
    return 1;
  }

  RECT desired{0, 0, state.client_w, state.client_h};
  AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
  state.hwnd = CreateWindowExA(
      0,
      wc.lpszClassName,
      "Factorio-pt Runtime // Visual Pass",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      desired.right - desired.left,
      desired.bottom - desired.top,
      nullptr,
      nullptr,
      wc.hInstance,
      nullptr);

  if (state.hwnd == nullptr) {
    std::cout << "[ERROR] CreateWindowEx failed.\n";
    return 1;
  }

  if (!InitBackBuffer(state)) {
    std::cout << "[ERROR] Backbuffer init failed.\n";
    return 1;
  }

  const int world_w_px = state.client_w - RuntimeState::kHudWidth;
  state.viewport_tiles_w = std::max(8, world_w_px / RuntimeState::kTileWidth);
  state.viewport_tiles_h = std::max(8, state.client_h / RuntimeState::kTileHeight);
  state.camera_x = std::clamp(state.player_x - state.viewport_tiles_w / 2, 0, std::max(0, state.world_w - state.viewport_tiles_w));
  state.camera_y = std::clamp(state.player_y - state.viewport_tiles_h / 2, 0, std::max(0, state.world_h - state.viewport_tiles_h));

  sim_set_policy(0.60f, 0.28f);
  state.snapshot = sim_tick(0.0f);
  GenerateWorld(state);

  bool prev_w = false, prev_a = false, prev_s = false, prev_d = false;
  bool prev_i = false, prev_j = false, prev_k = false, prev_l = false;
  bool prev_q = false, prev_r = false;

  auto frame_begin = std::chrono::high_resolution_clock::now();
  auto fps_window_start = frame_begin;
  std::uint32_t fps_frames = 0;
  MSG msg{};

  while (state.running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) state.running = false;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (!state.running) break;

    if (!HandleResize(state)) {
      state.running = false;
      break;
    }

    if (KeyEdge('W', prev_w)) state.player_y--;
    if (KeyEdge('S', prev_s)) state.player_y++;
    if (KeyEdge('A', prev_a)) state.player_x--;
    if (KeyEdge('D', prev_d)) state.player_x++;

    if (KeyEdge('I', prev_i)) state.camera_y--;
    if (KeyEdge('K', prev_k)) state.camera_y++;
    if (KeyEdge('J', prev_j)) state.camera_x--;
    if (KeyEdge('L', prev_l)) state.camera_x++;
    if (KeyEdge('R', prev_r)) ReseedWorldStyle(state);
    if (KeyEdge('Q', prev_q) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) state.running = false;

    state.player_x = std::clamp(state.player_x, 0, state.world_w - 1);
    state.player_y = std::clamp(state.player_y, 0, state.world_h - 1);
    state.camera_x = std::clamp(state.camera_x, 0, std::max(0, state.world_w - state.viewport_tiles_w));
    state.camera_y = std::clamp(state.camera_y, 0, std::max(0, state.world_h - state.viewport_tiles_h));

    state.snapshot = sim_tick(1.0f / 60.0f);
    state.water_anim += 0.07f;
    Render(state);

    const auto now = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<float, std::milli> frame_dt = now - frame_begin;
    state.frame_time_ms = frame_dt.count();
    frame_begin = now;

    ++fps_frames;
    const std::chrono::duration<float> fps_window = now - fps_window_start;
    if (fps_window.count() >= 1.0f) {
      state.fps = static_cast<float>(fps_frames) / fps_window.count();
      fps_frames = 0;
      fps_window_start = now;
    }

    constexpr auto target_frame = std::chrono::milliseconds(16);
    const auto sleep_time = target_frame - std::chrono::duration_cast<std::chrono::milliseconds>(frame_dt);
    if (sleep_time.count() > 0) std::this_thread::sleep_for(sleep_time);
    state.frame_counter++;
  }

  if (state.back_bitmap != nullptr) DeleteObject(state.back_bitmap);
  if (state.back_dc != nullptr) DeleteDC(state.back_dc);
  DestroyWindow(state.hwnd);
  return 0;
}
#endif

int main() {
#ifdef _WIN32
  HMODULE sim_lib = LoadLibraryA("factorio_pt_sim.dll");
  if (sim_lib == nullptr) {
    std::cout << "[ERROR] Rust sim library not found (factorio_pt_sim.dll).\n";
    return 1;
  }

  auto* sim_bootstrap = reinterpret_cast<SimBootstrapFn>(GetProcAddress(sim_lib, "sim_bootstrap"));
  auto* sim_tick = reinterpret_cast<SimTickFn>(GetProcAddress(sim_lib, "sim_tick"));
  auto* sim_set_policy = reinterpret_cast<SimSetPolicyFn>(GetProcAddress(sim_lib, "sim_set_policy"));
  auto* sim_generate_planet = reinterpret_cast<SimGeneratePlanetFn>(GetProcAddress(sim_lib, "sim_generate_planet"));
  auto* sim_generate_system = reinterpret_cast<SimGenerateSystemFn>(GetProcAddress(sim_lib, "sim_generate_system"));

  if (sim_bootstrap == nullptr || sim_tick == nullptr || sim_set_policy == nullptr || sim_generate_planet == nullptr ||
      sim_generate_system == nullptr) {
    std::cout << "[ERROR] Missing symbols in factorio_pt_sim.dll.\n";
    FreeLibrary(sim_lib);
    return 1;
  }

  const SimSnapshot boot = sim_bootstrap();
  std::cout << "[BOOT] Runtime visual pass online.\n";
  std::cout << "[SIM] stability=" << boot.stability << " pollution=" << boot.pollution << "\n";

  const int code = RunRuntimeWindow(sim_tick, sim_set_policy, sim_generate_planet, sim_generate_system);
  FreeLibrary(sim_lib);
  return code;
#else
  std::cout << "[INFO] Window runtime is currently Windows-only.\n";
  return 0;
#endif
}
