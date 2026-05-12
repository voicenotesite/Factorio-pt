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
  Coal
};

struct WorldTile {
  TerrainType terrain;
  ResourceType resource;
  std::uint8_t height_level;
  std::uint16_t ore_units;
  std::uint8_t biome_region;

  // Factorio-like overlays
  bool has_belt = false;
  std::uint8_t belt_dir = 1;  // 0=N,1=E,2=S,3=W

  bool has_tree = false;
  std::uint8_t tree_variant = 0;
};

enum class Dir : std::uint8_t { North = 0, East = 1, South = 2, West = 3 };

enum class MachineType : std::uint8_t { Extractor, Furnace };

enum class ItemType : std::uint8_t { IronOre, CopperOre, CoalOre, IronPlate };

struct ItemEntity {
  int x;
  int y;
  ItemType type;
  float t = 0.0f;          // 0..1 progress within current belt tile
  std::uint8_t dir = 1u;   // movement dir (0..3)
};

struct Machine {
  int x;
  int y;
  MachineType type;
  ResourceType resource;
  float timer_s;

  std::uint8_t dir = 1u;  // output dir (0..3)

  // Simple buffers to allow belt-fed automation.
  int buf_iron_ore = 0;
  int buf_coal_ore = 0;
};

struct TechnologyNode {
  std::string id;
  std::string name;
  int req_iron_ore = 0;
  int req_copper_ore = 0;
  int req_coal_ore = 0;
  int req_iron_plate = 0;
  bool unlocked = false;
  std::string bonus_text;
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
  static constexpr int kTextureSize = 32;
  using Texture = std::array<std::uint32_t, kTextureSize * kTextureSize>;

  Texture Generate(VisualKind kind, std::uint32_t seed, float theme_shift) const {
    Texture out{};
    auto base = BaseColor(kind, theme_shift);

    const std::uint32_t s0 = seed ^ 0xA53C9E11u;

    for (int y = 0; y < kTextureSize; ++y) {
      for (int x = 0; x < kTextureSize; ++x) {
        const float xf = static_cast<float>(x);
        const float yf = static_cast<float>(y);

        // Domain warp for organic, non-grid look.
        const float warp1 = Fbm(xf * 0.09f, yf * 0.09f, s0 ^ 0xB16B00B5u);
        const float warp2 = Fbm(xf * 0.09f + 37.0f, yf * 0.09f - 19.0f, s0 ^ 0xC0FFEEu);
        const float wx = xf + (warp1 - 0.5f) * 4.2f;
        const float wy = yf + (warp2 - 0.5f) * 4.2f;

        const float macro = Fbm(wx * 0.08f, wy * 0.08f, s0);
        const float micro = Fbm(wx * 0.30f, wy * 0.30f, s0 ^ 0x9E3779B9u);
        const float fine  = Fbm(wx * 0.75f, wy * 0.75f, s0 ^ 0x3C4A5B6Du);
        const float n = macro * 0.44f + micro * 0.38f + fine * 0.18f;
        // Contrast enhancement: expand dynamic range for richer, more Factorio-like detail.
        const float nc = std::clamp((n - 0.5f) * 1.60f + 0.5f, 0.0f, 1.0f);

        std::array<float, 3> rgb = base;

        const float detail_strength = IsTerrainKind(kind) ? 0.28f : 0.52f;
        const float detail = (nc - 0.5f) * detail_strength;
        rgb[0] += detail;
        rgb[1] += detail;
        rgb[2] += detail;

        if (IsTerrainKind(kind)) {
          const float broad = (macro - 0.5f);
          rgb[0] += broad * 0.11f;
          rgb[1] += broad * 0.13f;
          rgb[2] += broad * 0.09f;
        }

        const float dither = (Hash01(x, y, s0 ^ 0x3141592u) - 0.5f) * 0.016f;
        rgb[0] += dither;
        rgb[1] += dither;
        rgb[2] += dither;

        ApplyMaterialSignature(kind, x, y, macro, micro, fine, s0, rgb);
        out[static_cast<std::size_t>(y * kTextureSize + x)] = PackColor(ToByte(rgb[0]), ToByte(rgb[1]), ToByte(rgb[2]));
      }
    }

    return out;
  }

  private:
  static bool IsTerrainKind(VisualKind kind) {
    return kind == VisualKind::Lowland || kind == VisualKind::Midland || kind == VisualKind::Highland ||
           kind == VisualKind::Water || kind == VisualKind::Mountain;
  }

  static std::uint32_t HashU32(std::uint32_t x) {
    x ^= x >> 16u;
    x *= 0x7FEB352Du;
    x ^= x >> 15u;
    x *= 0x846CA68Bu;
    x ^= x >> 16u;
    return x;
  }

  static float Hash01(int x, int y, std::uint32_t seed) {
    const std::uint32_t hx = HashU32(static_cast<std::uint32_t>(x) * 0x9E3779B9u);
    const std::uint32_t hy = HashU32(static_cast<std::uint32_t>(y) * 0x85EBCA6Bu);
    const std::uint32_t h = HashU32(seed ^ hx ^ (hy + 0x27D4EB2Du));
    return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;
  }

  static float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  static float ValueNoise(float x, float y, std::uint32_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const float a = Hash01(x0, y0, seed);
    const float b = Hash01(x0 + 1, y0, seed);
    const float c = Hash01(x0, y0 + 1, seed);
    const float d = Hash01(x0 + 1, y0 + 1, seed);

    const float ux = Smooth(tx);
    const float uy = Smooth(ty);
    const float ab = a + (b - a) * ux;
    const float cd = c + (d - c) * ux;
    return ab + (cd - ab) * uy;
  }

  static float Fbm(float x, float y, std::uint32_t seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    float norm = 0.0f;

    for (int i = 0; i < 5; ++i) {
      sum += amp * ValueNoise(x * freq, y * freq, seed ^ (static_cast<std::uint32_t>(i) * 0x9E3779B9u));
      norm += amp;
      amp *= 0.5f;
      freq *= 2.0f;
    }

    return (norm > 0.0f) ? (sum / norm) : 0.0f;
  }

  static std::array<float, 3> BaseColor(VisualKind kind, float shift) {
    shift = std::clamp(shift, -0.16f, 0.16f);
    switch (kind) {
      case VisualKind::Water:
        return {0.07f, 0.43f + shift * 0.04f, 0.78f};       // Vivid Factorio blue
      case VisualKind::Mountain:
        return {0.29f + shift * 0.04f, 0.27f + shift * 0.03f, 0.24f};  // Dark rock
      case VisualKind::Highland:
        return {0.65f + shift * 0.08f, 0.54f + shift * 0.05f, 0.31f};  // Sandy dry
      case VisualKind::Midland:
        return {0.55f + shift * 0.06f, 0.43f + shift * 0.07f, 0.22f};  // Warm dirt
      case VisualKind::Lowland:
        return {0.24f + shift * 0.03f, 0.54f + shift * 0.10f, 0.13f};  // Rich Factorio grass
      case VisualKind::Iron:
        return {0.42f, 0.53f, 0.72f};   // Distinct blue-grey
      case VisualKind::Copper:
        return {0.86f, 0.43f, 0.14f};   // Vivid orange
      case VisualKind::Coal:
        return {0.11f, 0.11f, 0.13f};   // Near-black
    }
    return {0.5f, 0.5f, 0.5f};
  }

  static void ApplyMaterialSignature(VisualKind kind,
                                     int x,
                                     int y,
                                     float macro,
                                     float micro,
                                     float fine,
                                     std::uint32_t seed,
                                     std::array<float, 3>& rgb) {
    const float h = Hash01(x, y, seed ^ 0x5F356495u);

    switch (kind) {
      case VisualKind::Iron: {
        // Blue-grey metallic ore: strong dark veins, blue sheen, bright sparkle.
        const float vein = micro * 0.65f + macro * 0.35f;
        if (vein > 0.68f) {
          rgb[0] -= 0.30f; rgb[1] -= 0.27f; rgb[2] -= 0.24f;
        } else if (vein < 0.26f) {
          rgb[0] -= 0.16f; rgb[1] -= 0.14f; rgb[2] -= 0.12f;
        }
        rgb[2] += (macro - 0.5f) * 0.24f;
        if (h > 0.93f) {
          rgb[0] += 0.24f; rgb[1] += 0.30f; rgb[2] += 0.44f;
        }
        if (macro > 0.60f && micro < 0.36f) {
          rgb[0] += 0.10f; rgb[1] += 0.14f; rgb[2] += 0.22f;
        }
        break;
      }
      case VisualKind::Copper: {
        // Vivid orange ore: green oxidation pockets, dark shadow pits, bright highlights.
        if (micro > 0.70f && h > 0.38f) {
          rgb[0] -= 0.30f; rgb[1] += 0.20f; rgb[2] += 0.14f;
        } else if (micro < 0.22f) {
          rgb[0] -= 0.28f; rgb[1] -= 0.22f; rgb[2] -= 0.14f;
        }
        rgb[0] += (macro - 0.5f) * 0.22f;
        if (h > 0.90f) {
          rgb[0] += 0.36f; rgb[1] += 0.18f; rgb[2] += 0.02f;
        }
        break;
      }
      case VisualKind::Coal: {
        // Near-black with bright carbon seam veins.
        if (micro > 0.76f && h > 0.46f) {
          rgb[0] += 0.38f; rgb[1] += 0.38f; rgb[2] += 0.42f;
        } else {
          rgb[0] -= 0.08f; rgb[1] -= 0.08f; rgb[2] -= 0.07f;
        }
        if (h > 0.95f) {
          rgb[0] += 0.30f; rgb[1] += 0.30f; rgb[2] += 0.34f;
        }
        break;
      }
      case VisualKind::Water: {
        rgb[0] -= micro * 0.08f;
        rgb[1] += micro * 0.07f;
        rgb[2] += micro * 0.30f + (macro - 0.5f) * 0.14f;
        if (h > 0.964f) {
          rgb[0] += 0.20f; rgb[1] += 0.28f; rgb[2] += 0.36f;
        }
        break;
      }
      case VisualKind::Mountain: {
        if (micro > 0.70f) {
          rgb[0] += 0.20f; rgb[1] += 0.18f; rgb[2] += 0.15f;
        } else if (micro < 0.22f) {
          rgb[0] -= 0.22f; rgb[1] -= 0.20f; rgb[2] -= 0.17f;
        }
        if (fine > 0.78f && micro > 0.62f) {
          rgb[0] -= 0.24f; rgb[1] -= 0.22f; rgb[2] -= 0.19f;
        }
        if (h > 0.93f) { rgb[0] += 0.16f; rgb[1] += 0.14f; rgb[2] += 0.11f; }
        break;
      }
      case VisualKind::Highland:
      case VisualKind::Midland:
      case VisualKind::Lowland: {
        const bool pebble = h > 0.90f;
        const bool blade  = (h > 0.82f) && (micro > 0.56f);
        const bool crack  = fine > 0.78f && macro < 0.40f;
        if (pebble) {
          rgb[0] += 0.14f; rgb[1] += 0.12f; rgb[2] += 0.08f;
        }
        if (kind == VisualKind::Lowland && blade) {
          rgb[1] += 0.20f; rgb[0] -= 0.08f; rgb[2] -= 0.06f;
        }
        if (kind == VisualKind::Lowland && crack) {
          rgb[0] -= 0.06f; rgb[1] -= 0.12f; rgb[2] -= 0.04f;
        }
        if (kind == VisualKind::Midland && blade) {
          rgb[0] += 0.14f; rgb[1] += 0.09f;
        }
        if (kind == VisualKind::Midland && crack) {
          rgb[0] -= 0.14f; rgb[1] -= 0.12f; rgb[2] -= 0.09f;
        }
        if (kind == VisualKind::Highland && crack) {
          rgb[0] -= 0.18f; rgb[1] -= 0.16f; rgb[2] -= 0.12f;
        }
        break;
      }
    }
  }

  static std::uint8_t ToByte(float v) {
    float clamped = std::clamp(v, 0.0f, 1.0f);
    // Mild gamma to avoid flat "plastic" look.
    clamped = std::pow(clamped, 1.0f / 1.12f);
    return static_cast<std::uint8_t>(clamped * 255.0f);
  }
};

struct RuntimeState {
  static constexpr int kVisualKindCount = 8;

  // Top-down Factorio-like grid.
  static constexpr int kTileSize = 32;
  static constexpr int kTileWidth = kTileSize;
  static constexpr int kTileHeight = kTileSize;
  static constexpr int kHeightStepPx = 6;
  // HUD is now an overlay; we no longer reserve a right-side panel.
  static constexpr int kHudWidth = 0;

  HWND hwnd = nullptr;
  HDC back_dc = nullptr;
  HBITMAP back_bitmap = nullptr;
  std::uint32_t* back_pixels = nullptr;

  // Window (client) size.
  int client_w = 1360;
  int client_h = 820;

  // Render/backbuffer size. In fullscreen, native rendering can be too slow because we
  // do a lot of per-pixel work. By default we cap render resolution and scale to the window.
  int back_w = 1360;
  int back_h = 820;
  int base_back_w = 1360;
  int base_back_h = 820;
  bool fixed_backbuffer = false;
  bool fixed_backbuffer_hint_shown = false;
  bool auto_perf_governor = true;

  bool running = true;

  int viewport_tiles_w = 26;
  int viewport_tiles_h = 26;
  int world_w = 256;
  int world_h = 256;
  int camera_x = 0;
  int camera_y = 0;
  float camera_fx = 0.0f;  // camera in tile coords (left/top), smooth
  float camera_fy = 0.0f;
  float camera_sub_x = 0.0f;  // fractional part of camera_f*
  float camera_sub_y = 0.0f;

  int player_x = 8;
  int player_y = 6;
  float player_fx = 8.5f;  // player center in tile coords
  float player_fy = 6.5f;

  // 0=hidden, 1=compact, 2=debug
  int hud_mode = 1;

  // Build direction (used for belts and machine outputs). Matches Factorio: R rotates.
  std::uint8_t build_dir = 1;  // 0=N,1=E,2=S,3=W

  int world_origin_x = 0;
  int world_origin_y = 0;

  std::uint32_t run_seed = 0;
  std::uint32_t frame_counter = 0;
  float fps = 0.0f;
  float frame_time_ms = 0.0f;
  float water_anim = 0.0f;
  float theme_shift = 0.0f;
  float day_time_s = 0.0f;

  SimSnapshot snapshot{};
  PlanetSummary world{};
  SystemSummary system{};
  std::vector<WorldTile> tiles;
  std::vector<Machine> machines;
  std::vector<ItemEntity> items;
  AiTextureGenerator texture_gen;
  std::unordered_map<std::uint32_t, AiTextureGenerator::Texture> texture_cache;
  std::array<std::vector<AiTextureGenerator::Texture>, kVisualKindCount> external_textures;
  bool external_textures_loaded = false;
  std::string texture_source = "procedural-fallback";

  int inv_iron_ore = 0;
  int inv_copper_ore = 0;
  int inv_coal_ore = 0;
  int inv_iron_plate = 10;
  int inv_wood = 0;
  int total_iron_ore_collected = 0;
  int total_copper_ore_collected = 0;
  int total_coal_ore_collected = 0;
  int total_iron_plate_produced = 0;

  std::vector<TechnologyNode> tech_tree;
  float extractor_speed_multiplier = 1.0f;
  int manual_mine_bonus = 0;
  int smelt_plate_bonus = 0;

  int mined_total = 0;
  int smelted_total = 0;
  int extractors_built_total = 0;
  int furnaces_built_total = 0;
  int contract_target_plates = 0;
  int contract_reward_plates = 0;
  bool contract_completed = false;
  std::string status_text = "Ready.";
  float status_timer_s = 0.0f;
};
