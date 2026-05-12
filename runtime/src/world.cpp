#include "world.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
float SmoothStep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float Hash01(int x, int y, std::uint32_t seed) {
  const std::uint32_t hx = static_cast<std::uint32_t>(x);
  const std::uint32_t hy = static_cast<std::uint32_t>(y);
  std::uint32_t h = seed ^ (hx * 374761393u) ^ (hy * 668265263u);
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= (h >> 16);
  return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float ValueNoise(float x, float y, std::uint32_t seed) {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = x0 + 1;
  const int y1 = y0 + 1;
  const float tx = SmoothStep(x - static_cast<float>(x0));
  const float ty = SmoothStep(y - static_cast<float>(y0));

  const float n00 = Hash01(x0, y0, seed);
  const float n10 = Hash01(x1, y0, seed);
  const float n01 = Hash01(x0, y1, seed);
  const float n11 = Hash01(x1, y1, seed);

  const float nx0 = n00 + (n10 - n00) * tx;
  const float nx1 = n01 + (n11 - n01) * tx;
  return nx0 + (nx1 - nx0) * ty;
}

float FractalNoise(float x, float y, std::uint32_t seed, int octaves, float lacunarity, float gain) {
  float sum = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += ValueNoise(x * frequency, y * frequency, seed + static_cast<std::uint32_t>(i * 977u)) * amplitude;
    norm += amplitude;
    amplitude *= gain;
    frequency *= lacunarity;
  }
  return norm > 0.0f ? sum / norm : 0.0f;
}

float QuantileThreshold(const std::vector<float>& values, float q) {
  if (values.empty()) return 0.0f;
  std::vector<float> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  q = std::clamp(q, 0.0f, 1.0f);
  const std::size_t idx = static_cast<std::size_t>(q * static_cast<float>(sorted.size() - 1));
  return sorted[idx];
}

bool IsOreTerrain(TerrainType terrain) {
  return terrain != TerrainType::Water;
}

int TerrainRank(TerrainType terrain) {
  switch (terrain) {
    case TerrainType::Water: return 0;
    case TerrainType::Lowland: return 1;
    case TerrainType::Midland: return 2;
    case TerrainType::Highland: return 3;
    case TerrainType::Mountain: return 4;
  }
  return 1;
}

TerrainType RankTerrain(int rank) {
  switch (std::clamp(rank, 0, 4)) {
    case 0: return TerrainType::Water;
    case 1: return TerrainType::Lowland;
    case 2: return TerrainType::Midland;
    case 3: return TerrainType::Highland;
    case 4: return TerrainType::Mountain;
  }
  return TerrainType::Lowland;
}
}  // namespace

std::uint32_t Hash2D(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
  std::uint32_t h = seed ^ (x * 73856093u) ^ (y * 19349663u);
  h ^= (h >> 13);
  h *= 1274126177u;
  h ^= (h >> 16);
  return h;
}

std::size_t TileIndex(const RuntimeState& state, int x, int y) {
  return static_cast<std::size_t>(y * state.world_w + x);
}

WorldTile* GetTile(RuntimeState& state, int x, int y) {
  if (x < 0 || y < 0 || x >= state.world_w || y >= state.world_h) return nullptr;
  return &state.tiles[TileIndex(state, x, y)];
}

Machine* FindMachineAt(RuntimeState& state, int x, int y) {
  for (auto& machine : state.machines) {
    if (machine.x == x && machine.y == y) return &machine;
  }
  return nullptr;
}

const char* ResourceLabel(ResourceType resource) {
  switch (resource) {
    case ResourceType::Iron: return "iron";
    case ResourceType::Copper: return "copper";
    case ResourceType::Coal: return "coal";
    case ResourceType::None: break;
  }
  return "none";
}

void SetStatus(RuntimeState& state, const std::string& text, float seconds) {
  state.status_text = text;
  state.status_timer_s = seconds;
}

void GenerateWorld(RuntimeState& state) {
  state.tiles.resize(static_cast<std::size_t>(state.world_w * state.world_h));
  state.machines.clear();
  state.items.clear();

  {
    const int world_w = state.world_w;
    const int world_h = state.world_h;
    const int world_tile_count = world_w * world_h;
    const int region_size = 32;
    const int regions_x = (world_w + region_size - 1) / region_size;
    const int regions_y = (world_h + region_size - 1) / region_size;

    struct RegionSeed {
      int cx = 0;
      int cy = 0;
      TerrainType terrain = TerrainType::Lowland;
      ResourceType resource = ResourceType::None;
      std::uint8_t biome = 0;
      std::uint8_t height_level = 1;
      float elevation = 0.0f;
      float moisture = 0.0f;
      float heat = 0.0f;
      float tree_density = 0.0f;
    };

    auto clamp_tile = [&](int v, int limit) {
      return std::clamp(v, 0, limit - 1);
    };

    std::vector<RegionSeed> regions;
    regions.reserve(static_cast<std::size_t>(regions_x * regions_y));

    for (int ry = 0; ry < regions_y; ++ry) {
      for (int rx = 0; rx < regions_x; ++rx) {
        const std::uint32_t h = Hash2D(static_cast<std::uint32_t>(rx), static_cast<std::uint32_t>(ry), state.world.seed ^ 0x41C1u);
        const int base_x = rx * region_size + region_size / 2;
        const int base_y = ry * region_size + region_size / 2;
        const int jitter_x = static_cast<int>((h >> 1u) & 15u) - 7;
        const int jitter_y = static_cast<int>((h >> 5u) & 15u) - 7;

        RegionSeed region{};
        region.cx = clamp_tile(base_x + jitter_x, world_w);
        region.cy = clamp_tile(base_y + jitter_y, world_h);

        const float fx = static_cast<float>(rx) / std::max(1, regions_x - 1);
        const float fy = static_cast<float>(ry) / std::max(1, regions_y - 1);
        const float continent = FractalNoise(fx * 1.9f + 0.4f, fy * 1.9f - 0.2f, h ^ 0x1101u, 4, 2.05f, 0.56f);
        const float ridge = FractalNoise(fx * 4.7f + 11.0f, fy * 4.7f - 5.0f, h ^ 0x2202u, 3, 2.10f, 0.58f);
        const float wet = FractalNoise(fx * 3.2f + 19.0f, fy * 3.2f + 7.0f, h ^ 0x3303u, 3, 2.00f, 0.56f);
        const float heat = FractalNoise(fx * 2.5f - 13.0f, fy * 2.5f + 9.0f, h ^ 0x4404u, 3, 2.00f, 0.56f);

        region.elevation = std::clamp(continent * 0.60f + ridge * 0.24f + (1.0f - std::abs(continent * 2.0f - 1.0f)) * 0.16f, 0.0f, 1.0f);
        region.moisture = std::clamp(wet * 0.72f + (1.0f - region.elevation) * 0.28f, 0.0f, 1.0f);
        region.heat = std::clamp((1.0f - std::abs(fy * 2.0f - 1.0f)) * 0.74f + heat * 0.26f - region.elevation * 0.18f, 0.0f, 1.0f);

        if (region.elevation < 0.22f) region.terrain = TerrainType::Water;
        else if (region.elevation < 0.42f) region.terrain = TerrainType::Lowland;
        else if (region.elevation < 0.63f) region.terrain = TerrainType::Midland;
        else if (region.elevation < 0.82f) region.terrain = TerrainType::Highland;
        else region.terrain = TerrainType::Mountain;

        if (region.heat < 0.30f) region.biome = 2;
        else if (region.heat > 0.66f && region.moisture < 0.38f) region.biome = 1;
        else if (region.moisture > 0.70f) region.biome = 3;

        switch (region.terrain) {
          case TerrainType::Water: region.height_level = 0; break;
          case TerrainType::Lowland: region.height_level = 1; break;
          case TerrainType::Midland: region.height_level = 2; break;
          case TerrainType::Highland: region.height_level = 3; break;
          case TerrainType::Mountain: region.height_level = 4; break;
        }

        if (region.terrain != TerrainType::Water) {
          const float iron_bias = ridge * 0.40f + region.elevation * 0.22f + (region.terrain == TerrainType::Mountain ? 0.16f : 0.0f);
          const float copper_bias = heat * 0.30f + (1.0f - region.elevation) * 0.18f + (region.terrain == TerrainType::Midland ? 0.10f : 0.0f);
          const float coal_bias = region.moisture * 0.34f + (1.0f - region.elevation) * 0.14f + (region.terrain == TerrainType::Lowland ? 0.10f : 0.0f);
          if (iron_bias >= copper_bias && iron_bias >= coal_bias && iron_bias > 0.55f) {
            region.resource = ResourceType::Iron;
          } else if (copper_bias >= iron_bias && copper_bias >= coal_bias && copper_bias > 0.55f) {
            region.resource = ResourceType::Copper;
          } else if (coal_bias > 0.57f) {
            region.resource = ResourceType::Coal;
          }
          region.tree_density = std::clamp(region.moisture * 0.78f + (1.0f - std::abs(region.heat - 0.52f) * 1.35f) * 0.18f, 0.0f, 1.0f);
          if (region.terrain == TerrainType::Mountain) region.tree_density *= 0.10f;
          if (region.terrain == TerrainType::Highland) region.tree_density *= 0.45f;
          if (region.resource != ResourceType::None) region.tree_density *= 0.30f;
        }

        regions.push_back(region);
      }
    }

    std::uint32_t lowland_tiles = 0;
    std::uint32_t midland_tiles = 0;
    std::uint32_t highland_tiles = 0;
    std::uint32_t water_tiles = 0;
    std::uint32_t mountain_tiles = 0;
    std::uint32_t iron_tiles = 0;
    std::uint32_t copper_tiles = 0;
    std::uint32_t coal_tiles = 0;
    std::uint32_t unique_sites = 0;
    float height_sum = 0.0f;

    for (int y = 0; y < world_h; ++y) {
      for (int x = 0; x < world_w; ++x) {
        const std::size_t idx = TileIndex(state, x, y);
        int best_region = 0;
        int best_d2 = 1 << 30;
        for (std::size_t i = 0; i < regions.size(); ++i) {
          const int dx = x - regions[i].cx;
          const int dy = y - regions[i].cy;
          const int d2 = dx * dx + dy * dy;
          if (d2 < best_d2) {
            best_d2 = d2;
            best_region = static_cast<int>(i);
          }
        }

        const RegionSeed& region = regions[static_cast<std::size_t>(best_region)];
        const float dx = static_cast<float>(x - region.cx);
        const float dy = static_cast<float>(y - region.cy);
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float edge_factor = std::clamp(dist / static_cast<float>(region_size) * 1.2f, 0.0f, 1.0f);
        const std::uint32_t local_seed = Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), state.world.seed ^ 0xA5A5u);
        const float local_macro = FractalNoise(static_cast<float>(x) * 0.06f, static_cast<float>(y) * 0.06f, local_seed ^ 0x0F11u, 3, 2.0f, 0.58f);
        const float local_micro = FractalNoise(static_cast<float>(x) * 0.13f + 7.0f, static_cast<float>(y) * 0.13f - 11.0f, local_seed ^ 0x0F22u, 2, 2.1f, 0.60f);
        const float local_h = std::clamp(region.elevation + (local_macro - 0.5f) * 0.16f - edge_factor * 0.05f, 0.0f, 1.0f);
        const float local_m = std::clamp(region.moisture + (local_micro - 0.5f) * 0.12f, 0.0f, 1.0f);
        const float local_t = std::clamp(region.heat + (local_macro - 0.5f) * 0.10f, 0.0f, 1.0f);
        height_sum += local_h;

        TerrainType terrain = region.terrain;
        if (terrain != TerrainType::Water) {
          if (local_h < 0.20f) terrain = TerrainType::Water;
          else if (local_h < 0.40f) terrain = TerrainType::Lowland;
          else if (local_h < 0.62f) terrain = TerrainType::Midland;
          else if (local_h < 0.80f) terrain = TerrainType::Highland;
          else terrain = TerrainType::Mountain;
        }

        std::uint8_t biome = region.biome;
        if (local_t < 0.30f) biome = 2;
        else if (local_t > 0.66f && local_m < 0.38f) biome = 1;
        else if (local_m > 0.70f) biome = 3;

        WorldTile tile{};
        tile.terrain = terrain;
        tile.resource = ResourceType::None;
        tile.height_level = 0;
        tile.ore_units = 0;
        tile.biome_region = biome;
        tile.has_belt = false;
        tile.belt_dir = 1;
        tile.has_tree = false;
        tile.tree_variant = 0;

        switch (terrain) {
          case TerrainType::Water: tile.height_level = 0; ++water_tiles; break;
          case TerrainType::Lowland: tile.height_level = 1; ++lowland_tiles; break;
          case TerrainType::Midland: tile.height_level = 2; ++midland_tiles; break;
          case TerrainType::Highland: tile.height_level = 3; ++highland_tiles; break;
          case TerrainType::Mountain: tile.height_level = 4; ++mountain_tiles; break;
        }

        if (terrain != TerrainType::Water && region.resource != ResourceType::None) {
          const float ore_noise = FractalNoise(static_cast<float>(x) * 0.10f, static_cast<float>(y) * 0.10f, local_seed ^ 0xB017u, 3, 2.0f, 0.58f);
          const float region_center = 1.0f - std::clamp(dist / (region_size * 0.65f), 0.0f, 1.0f);
          const float ore_mask = region_center * 0.55f + ore_noise * 0.45f;
          if (ore_mask > 0.60f) {
            tile.resource = region.resource;
            const float richness = std::clamp(ore_mask, 0.0f, 1.2f);
            tile.ore_units = static_cast<std::uint16_t>(std::clamp(14 + static_cast<int>(richness * 36.0f), 10, 56));
            if (tile.resource == ResourceType::Iron) ++iron_tiles;
            if (tile.resource == ResourceType::Copper) ++copper_tiles;
            if (tile.resource == ResourceType::Coal) ++coal_tiles;
          }
        }

        if (terrain != TerrainType::Water && tile.resource == ResourceType::None) {
          const float tree_noise = FractalNoise(static_cast<float>(x) * 0.08f + 3.0f, static_cast<float>(y) * 0.08f - 9.0f, local_seed ^ 0xC0DEu, 3, 2.0f, 0.58f);
          const float tree_score = region.tree_density * 0.70f + tree_noise * 0.20f + (1.0f - edge_factor) * 0.10f;
          if (tree_score > 0.66f) {
            tile.has_tree = true;
            tile.tree_variant = static_cast<std::uint8_t>(Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), local_seed ^ 0x13579u) & 0x03u);
          }
        }

        state.tiles[idx] = tile;
        if (tile.resource != ResourceType::None) ++unique_sites;
      }
    }

    state.world.lowland_tiles = lowland_tiles;
    state.world.midland_tiles = midland_tiles;
    state.world.highland_tiles = highland_tiles;
    state.world.water_tiles = water_tiles;
    state.world.mountain_tiles = mountain_tiles;
    state.world.iron_tiles = iron_tiles;
    state.world.copper_tiles = copper_tiles;
    state.world.coal_tiles = coal_tiles;
    state.world.unique_sites = unique_sites;
    state.world.avg_height = world_tile_count > 0 ? height_sum / static_cast<float>(world_tile_count) : 0.0f;

    return;
  }

  const int cell_size = 16;
  const int cells_x = (state.world_w + cell_size - 1) / cell_size;
  const int cells_y = (state.world_h + cell_size - 1) / cell_size;
  const std::size_t cell_count = static_cast<std::size_t>(cells_x * cells_y);

  struct CellData {
    TerrainType terrain = TerrainType::Lowland;
    ResourceType resource = ResourceType::None;
    std::uint8_t biome = 0;
    std::uint8_t height_level = 1;
    std::uint16_t ore_units = 0;
    float elevation = 0.0f;
    float tree_density = 0.0f;
  };

  std::vector<CellData> cells(cell_count);
  auto cell_index = [cells_x](int cx, int cy) -> std::size_t {
    return static_cast<std::size_t>(cy * cells_x + cx);
  };

  for (int cy = 0; cy < cells_y; ++cy) {
    for (int cx = 0; cx < cells_x; ++cx) {
      const float fx = static_cast<float>(cx) / std::max(1, cells_x - 1);
      const float fy = static_cast<float>(cy) / std::max(1, cells_y - 1);
      const std::uint32_t s = Hash2D(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy), state.world.seed);

      const float land = FractalNoise(fx * 2.0f + 0.3f, fy * 2.0f - 0.2f, s ^ 0x1101u, 4, 2.0f, 0.55f);
      const float ridge = FractalNoise(fx * 4.5f + 9.0f, fy * 4.5f + 5.0f, s ^ 0x2202u, 4, 2.1f, 0.56f);
      const float wet = FractalNoise(fx * 3.8f + 17.0f, fy * 3.8f - 11.0f, s ^ 0x3303u, 3, 2.0f, 0.58f);
      const float heat = FractalNoise(fx * 2.7f - 13.0f, fy * 2.7f + 7.0f, s ^ 0x4404u, 3, 2.0f, 0.56f);

      CellData cell{};
      cell.elevation = std::clamp(land * 0.58f + ridge * 0.24f + (1.0f - std::abs(land * 2.0f - 1.0f)) * 0.18f, 0.0f, 1.0f);
      if (cell.elevation < 0.22f) cell.terrain = TerrainType::Water;
      else if (cell.elevation < 0.42f) cell.terrain = TerrainType::Lowland;
      else if (cell.elevation < 0.63f) cell.terrain = TerrainType::Midland;
      else if (cell.elevation < 0.82f) cell.terrain = TerrainType::Highland;
      else cell.terrain = TerrainType::Mountain;

      cell.biome = 0;
      if (heat < 0.32f) cell.biome = 2;
      else if (heat > 0.66f && wet < 0.38f) cell.biome = 1;
      else if (wet > 0.68f) cell.biome = 3;

      switch (cell.terrain) {
        case TerrainType::Water: cell.height_level = 0; break;
        case TerrainType::Lowland: cell.height_level = 1; break;
        case TerrainType::Midland: cell.height_level = 2; break;
        case TerrainType::Highland: cell.height_level = 3; break;
        case TerrainType::Mountain: cell.height_level = 4; break;
      }

      if (cell.terrain != TerrainType::Water) {
        const float iron = ridge * 0.34f + cell.elevation * 0.22f + (cell.terrain == TerrainType::Mountain ? 0.20f : 0.0f) +
                           (heat < 0.35f ? 0.10f : 0.0f);
        const float copper = wet * 0.28f + heat * 0.30f + (cell.terrain == TerrainType::Midland ? 0.12f : 0.0f) +
                             (cell.terrain == TerrainType::Lowland ? 0.08f : 0.0f);
        const float coal = wet * 0.32f + (1.0f - cell.elevation) * 0.20f + (cell.terrain == TerrainType::Lowland ? 0.10f : 0.0f);
        if (iron >= copper && iron >= coal && iron > 0.58f) {
          cell.resource = ResourceType::Iron;
        } else if (copper >= iron && copper >= coal && copper > 0.56f) {
          cell.resource = ResourceType::Copper;
        } else if (coal > 0.58f) {
          cell.resource = ResourceType::Coal;
        }
        const float resource_score = std::max({iron, copper, coal});
        if (cell.resource != ResourceType::None) {
          cell.ore_units = static_cast<std::uint16_t>(std::clamp(18 + static_cast<int>(resource_score * 42.0f), 12, 60));
        }
        cell.tree_density = std::clamp(wet * 0.70f + (1.0f - std::abs(heat - 0.52f) * 1.4f) * 0.22f, 0.0f, 1.0f);
        if (cell.terrain == TerrainType::Mountain) cell.tree_density *= 0.2f;
        if (cell.terrain == TerrainType::Highland) cell.tree_density *= 0.6f;
        if (cell.resource != ResourceType::None) cell.tree_density *= 0.35f;
      }

      cells[cell_index(cx, cy)] = cell;
    }
  }

  for (int y = 0; y < state.world_h; ++y) {
    for (int x = 0; x < state.world_w; ++x) {
      const int cx = x / cell_size;
      const int cy = y / cell_size;
      const CellData& cell = cells[cell_index(cx, cy)];
      const std::size_t idx = TileIndex(state, x, y);
      auto& tile = state.tiles[idx];

      tile.terrain = cell.terrain;
      tile.resource = ResourceType::None;
      tile.height_level = cell.height_level;
      tile.biome_region = cell.biome;
      tile.ore_units = 0;
      tile.has_belt = false;
      tile.belt_dir = 1;
      tile.has_tree = false;
      tile.tree_variant = 0;

      if (cell.terrain == TerrainType::Water) {
        continue;
      }

      const int lx = x - cx * cell_size;
      const int ly = y - cy * cell_size;
      const float u = (static_cast<float>(lx) + 0.5f) / static_cast<float>(cell_size);
      const float v = (static_cast<float>(ly) + 0.5f) / static_cast<float>(cell_size);
      const std::uint32_t s = Hash2D(static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy), state.world.seed ^ 0xA531u);

      if (cell.resource != ResourceType::None) {
        const float ore_field = FractalNoise(u * 2.4f + 5.0f, v * 2.4f - 3.0f, s ^ 0xC011u, 3, 2.0f, 0.58f);
        const float center = 1.0f - std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 1.45f;
        const float mask = ore_field * 0.65f + center * 0.35f;
        if (mask > 0.58f) {
          tile.resource = cell.resource;
          const float local_rich = std::clamp(mask, 0.0f, 1.2f);
          tile.ore_units = static_cast<std::uint16_t>(std::clamp(static_cast<int>(cell.ore_units * (0.70f + local_rich * 0.35f)), 10, 60));
        }
      }

      if (tile.resource == ResourceType::None && cell.tree_density > 0.0f) {
        const float tree_field = FractalNoise(u * 2.1f + 19.0f, v * 2.1f + 11.0f, s ^ 0xBEEF1u, 3, 2.0f, 0.56f);
        const float edge = 1.0f - std::max(std::abs(u - 0.5f), std::abs(v - 0.5f));
        const float tree_score = cell.tree_density * 0.68f + tree_field * 0.22f + edge * 0.10f;
        if (tree_score > 0.62f) {
          tile.has_tree = true;
          tile.tree_variant = static_cast<std::uint8_t>(Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), s ^ 0x13579u) & 0x03u);
        }
      }
    }
  }
}

void ReseedWorldStyle(RuntimeState& state) {
  state.run_seed ^= 0x9E3779B9u + state.frame_counter;
  state.theme_shift = static_cast<float>((state.run_seed & 0x7Fu)) / 127.0f * 0.32f - 0.16f;
  state.texture_cache.clear();
  GenerateWorld(state);
  SetStatus(state, "Nowy seed: wyglad i rozklad natury zaktualizowany.", 2.2f);
}
