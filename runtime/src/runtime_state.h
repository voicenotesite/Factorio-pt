#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
using HWND = void*;
using HDC = void*;
using HBITMAP = void*;
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
  std::uint16_t ore_units;
  std::uint8_t biome_region;
};

struct Machine {
  int x;
  int y;
  ResourceType resource;
  float timer_s;
};

inline std::uint32_t PackColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8u) | (static_cast<std::uint32_t>(b) << 16u);
}

inline std::uint8_t GetR(std::uint32_t c) { return static_cast<std::uint8_t>(c & 0xFFu); }
inline std::uint8_t GetG(std::uint32_t c) { return static_cast<std::uint8_t>((c >> 8u) & 0xFFu); }
inline std::uint8_t GetB(std::uint32_t c) { return static_cast<std::uint8_t>((c >> 16u) & 0xFFu); }

inline std::uint32_t MulColor(std::uint32_t c, float factor) {
  factor = std::clamp(factor, 0.0f, 2.0f);
  auto mul = [factor](std::uint8_t v) -> std::uint8_t {
    const int out = static_cast<int>(std::lround(static_cast<float>(v) * factor));
    return static_cast<std::uint8_t>(std::clamp(out, 0, 255));
  };
  return PackColor(mul(GetR(c)), mul(GetG(c)), mul(GetB(c)));
}

inline std::uint32_t LerpColor(std::uint32_t a, std::uint32_t b, float t) {
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
  std::vector<Machine> machines;
  AiTextureGenerator texture_gen;
  std::unordered_map<std::uint32_t, AiTextureGenerator::Texture> texture_cache;

  int inv_iron_ore = 0;
  int inv_copper_ore = 0;
  int inv_coal_ore = 0;
  int inv_iron_plate = 10;
  std::string status_text = "Ready.";
  float status_timer_s = 0.0f;
};
