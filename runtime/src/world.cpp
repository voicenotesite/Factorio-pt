#include "world.h"

#include <algorithm>
#include <sstream>

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

      std::uint16_t ore_units = 0;
      if (resource != ResourceType::None) {
        ore_units = static_cast<std::uint16_t>(45u + (Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), state.run_seed ^ 0xC0FFEEu) % 95u));
      }
      state.tiles[TileIndex(state, x, y)] = {terrain, resource, level, ore_units};
    }
  }
}

void ReseedWorldStyle(RuntimeState& state) {
  state.run_seed ^= 0x9E3779B9u + state.frame_counter;
  state.theme_shift = static_cast<float>((state.run_seed & 0x7Fu)) / 127.0f * 0.32f - 0.16f;
  state.texture_cache.clear();
  SetStatus(state, "Zmieniono styl wizualny swiata (seed).", 1.8f);
}
