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

  const int total_tiles = state.world_w * state.world_h;
  const float inv_total = total_tiles > 0 ? 1.0f / static_cast<float>(total_tiles) : 0.0f;
  const float water_ratio = static_cast<float>(state.world.water_tiles) * inv_total;
  const float mountain_ratio = static_cast<float>(state.world.mountain_tiles) * inv_total;
  const float high_ratio = static_cast<float>(state.world.highland_tiles) * inv_total;
  const float mid_ratio = static_cast<float>(state.world.midland_tiles) * inv_total;
  const float low_ratio = static_cast<float>(state.world.lowland_tiles) * inv_total;

  std::vector<float> elevation(static_cast<std::size_t>(total_tiles), 0.0f);
  std::vector<float> roughness(static_cast<std::size_t>(total_tiles), 0.0f);
  std::vector<float> moisture(static_cast<std::size_t>(total_tiles), 0.0f);

  const float base_scale = 0.028f;
  const float warp_scale = 0.014f;
  for (int y = 0; y < state.world_h; ++y) {
    for (int x = 0; x < state.world_w; ++x) {
      const std::size_t idx = TileIndex(state, x, y);
      const float fx = static_cast<float>(x);
      const float fy = static_cast<float>(y);

      const float warp_x = (ValueNoise(fx * warp_scale, fy * warp_scale, state.world.seed ^ 0x77A11u) - 0.5f) * 12.0f;
      const float warp_y = (ValueNoise(fx * warp_scale, fy * warp_scale, state.world.seed ^ 0x99B22u) - 0.5f) * 12.0f;
      const float nx = (fx + warp_x) * base_scale;
      const float ny = (fy + warp_y) * base_scale;

      const float continental = FractalNoise(nx * 0.7f, ny * 0.7f, state.world.seed ^ 0x1000u, 5, 2.0f, 0.5f);
      const float detail = FractalNoise(nx * 2.0f, ny * 2.0f, state.world.seed ^ 0x2000u, 4, 2.1f, 0.55f);
      const float ridge = std::abs(FractalNoise(nx * 1.4f, ny * 1.4f, state.world.seed ^ 0x3000u, 3, 2.0f, 0.5f) * 2.0f - 1.0f);
      elevation[idx] = std::clamp(continental * 0.65f + detail * 0.20f + (1.0f - ridge) * 0.15f, 0.0f, 1.0f);

      roughness[idx] = FractalNoise(nx * 3.2f, ny * 3.2f, state.world.seed ^ 0x4000u, 3, 2.0f, 0.5f);
      moisture[idx] = FractalNoise(nx * 1.5f, ny * 1.5f, state.world.seed ^ 0x5000u, 4, 2.0f, 0.55f);
    }
  }

  const float water_t = QuantileThreshold(elevation, water_ratio);
  const float low_t = QuantileThreshold(elevation, water_ratio + low_ratio);
  const float mid_t = QuantileThreshold(elevation, water_ratio + low_ratio + mid_ratio);
  const float high_t = QuantileThreshold(elevation, water_ratio + low_ratio + mid_ratio + high_ratio);
  (void)mountain_ratio;

  std::vector<float> iron_score(static_cast<std::size_t>(total_tiles), -1.0f);
  std::vector<float> copper_score(static_cast<std::size_t>(total_tiles), -1.0f);
  std::vector<float> coal_score(static_cast<std::size_t>(total_tiles), -1.0f);

  for (int y = 0; y < state.world_h; ++y) {
    for (int x = 0; x < state.world_w; ++x) {
      const std::size_t idx = TileIndex(state, x, y);
      TerrainType terrain = TerrainType::Lowland;
      const float h = elevation[idx];
      if (h < water_t) terrain = TerrainType::Water;
      else if (h >= high_t) terrain = TerrainType::Mountain;
      else if (h >= mid_t) terrain = TerrainType::Highland;
      else if (h >= low_t) terrain = TerrainType::Midland;
      else terrain = TerrainType::Lowland;

      std::uint8_t level = 1;
      switch (terrain) {
        case TerrainType::Water: level = 0; break;
        case TerrainType::Lowland: level = static_cast<std::uint8_t>(1 + static_cast<int>(roughness[idx] > 0.62f)); break;
        case TerrainType::Midland: level = static_cast<std::uint8_t>(2 + static_cast<int>(roughness[idx] > 0.58f)); break;
        case TerrainType::Highland: level = static_cast<std::uint8_t>(3 + static_cast<int>(roughness[idx] > 0.54f)); break;
        case TerrainType::Mountain: level = static_cast<std::uint8_t>(4 + static_cast<int>(roughness[idx] > 0.50f)); break;
      }

      state.tiles[idx] = {terrain, ResourceType::None, level, 0u};

      if (!IsOreTerrain(terrain)) continue;

      const float fx = static_cast<float>(x) * 0.046f;
      const float fy = static_cast<float>(y) * 0.046f;
      const float blob_iron = FractalNoise(fx, fy, state.world.seed ^ 0xC11u, 4, 2.1f, 0.52f);
      const float blob_copper = FractalNoise(fx, fy, state.world.seed ^ 0xC22u, 4, 2.1f, 0.52f);
      const float blob_coal = FractalNoise(fx, fy, state.world.seed ^ 0xC33u, 4, 2.1f, 0.52f);

      const float terrain_pref_iron = (terrain == TerrainType::Mountain ? 0.20f : terrain == TerrainType::Highland ? 0.13f : 0.05f);
      const float terrain_pref_copper = (terrain == TerrainType::Highland ? 0.17f : terrain == TerrainType::Midland ? 0.10f : 0.02f);
      const float terrain_pref_coal = (terrain == TerrainType::Midland ? 0.14f : terrain == TerrainType::Lowland ? 0.11f : 0.03f);

      iron_score[idx] = blob_iron * 0.72f + roughness[idx] * 0.20f + terrain_pref_iron;
      copper_score[idx] = blob_copper * 0.72f + (1.0f - roughness[idx]) * 0.10f + terrain_pref_copper;
      coal_score[idx] = blob_coal * 0.70f + moisture[idx] * 0.18f + terrain_pref_coal;
    }
  }

  auto assign_resource = [&](ResourceType resource, std::vector<float>& scores, std::uint32_t target_count) {
    if (target_count == 0) return;
    std::vector<std::size_t> order;
    order.reserve(scores.size());
    for (std::size_t i = 0; i < scores.size(); ++i) {
      if (scores[i] >= 0.0f) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return scores[a] > scores[b]; });

    std::uint32_t placed = 0;
    for (std::size_t idx : order) {
      if (placed >= target_count) break;
      auto& tile = state.tiles[idx];
      if (tile.resource != ResourceType::None || !IsOreTerrain(tile.terrain)) continue;
      tile.resource = resource;
      const float score = std::clamp(scores[idx], 0.0f, 1.2f);
      const int richness = 50 + static_cast<int>(score * 90.0f);
      tile.ore_units = static_cast<std::uint16_t>(std::clamp(richness, 30, 180));
      placed++;
    }
  };

  assign_resource(ResourceType::Iron, iron_score, state.world.iron_tiles);
  assign_resource(ResourceType::Copper, copper_score, state.world.copper_tiles);
  assign_resource(ResourceType::Coal, coal_score, state.world.coal_tiles);

  for (auto& tile : state.tiles) {
    if (tile.resource == ResourceType::None) {
      tile.ore_units = 0;
      continue;
    }
    if (!IsOreTerrain(tile.terrain)) {
      tile.resource = ResourceType::None;
      tile.ore_units = 0;
      continue;
    }
    if (tile.ore_units == 0) {
      tile.ore_units = 35;
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
