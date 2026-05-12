#include "gameplay.h"

#include "world.h"

#include <algorithm>
#include <cstddef>
#include <sstream>

namespace {
constexpr int kDirDx[4] = {0, 1, 0, -1};
constexpr int kDirDy[4] = {-1, 0, 1, 0};

TechnologyNode* FindTech(RuntimeState& state, const char* id) {
  for (auto& tech : state.tech_tree) {
    if (tech.id == id) return &tech;
  }
  return nullptr;
}

const TechnologyNode* FindTech(const RuntimeState& state, const char* id) {
  for (const auto& tech : state.tech_tree) {
    if (tech.id == id) return &tech;
  }
  return nullptr;
}

bool MeetsRequirements(const RuntimeState& state, const TechnologyNode& tech) {
  return state.total_iron_ore_collected >= tech.req_iron_ore &&
         state.total_copper_ore_collected >= tech.req_copper_ore &&
         state.total_coal_ore_collected >= tech.req_coal_ore &&
         state.total_iron_plate_produced >= tech.req_iron_plate;
}

void RemoveMachineAt(RuntimeState& state, int x, int y, MachineType type) {
  state.machines.erase(std::remove_if(state.machines.begin(), state.machines.end(),
                                      [&](const Machine& m) { return m.x == x && m.y == y && m.type == type; }),
                       state.machines.end());
}

void AddItemToInventory(RuntimeState& state, ItemType type, int count) {
  if (count <= 0) return;
  switch (type) {
    case ItemType::IronOre: state.inv_iron_ore += count; break;
    case ItemType::CopperOre: state.inv_copper_ore += count; break;
    case ItemType::CoalOre: state.inv_coal_ore += count; break;
    case ItemType::IronPlate: state.inv_iron_plate += count; break;
  }
}

ItemType ResourceToItem(ResourceType r) {
  switch (r) {
    case ResourceType::Iron: return ItemType::IronOre;
    case ResourceType::Copper: return ItemType::CopperOre;
    case ResourceType::Coal: return ItemType::CoalOre;
    case ResourceType::None: break;
  }
  return ItemType::IronOre;
}
}  // namespace

void InitializeTechnology(RuntimeState& state) {
  if (!state.tech_tree.empty()) return;
  state.tech_tree = {
      {"metallurgy-1", "Metallurgy I", 30, 0, 20, 0, false, "+1 plate per smelt"},
      {"automation-1", "Automation I", 20, 20, 0, 30, false, "Unlock extractor placement"},
      {"logistics-1", "Logistics I", 40, 30, 30, 60, false, "Unlock belts + faster extractors"},
      {"geology-1", "Geology I", 55, 55, 40, 80, false, "+2 manual mining yield"},
  };
  if (state.contract_target_plates <= 0) {
    state.contract_target_plates = 120 + static_cast<int>(state.run_seed % 180u);
    state.contract_reward_plates = 20 + static_cast<int>((state.run_seed >> 6u) % 16u);
    state.contract_completed = false;
  }
}

bool TechnologyUnlocked(const RuntimeState& state, const char* tech_id) {
  const auto* tech = FindTech(state, tech_id);
  return tech != nullptr && tech->unlocked;
}

void UpdateTechnologyUnlocks(RuntimeState& state) {
  for (auto& tech : state.tech_tree) {
    if (tech.unlocked) continue;
    if (!MeetsRequirements(state, tech)) continue;
    tech.unlocked = true;

    if (tech.id == "metallurgy-1") state.smelt_plate_bonus = 1;
    if (tech.id == "automation-1") { /* gating only */ }
    if (tech.id == "logistics-1") state.extractor_speed_multiplier = 1.6f;
    if (tech.id == "geology-1") state.manual_mine_bonus = 2;

    std::ostringstream ss;
    ss << "Technologia odblokowana: " << tech.name << " (" << tech.bonus_text << ")";
    SetStatus(state, ss.str(), 3.0f);
  }
}

void TryMine(RuntimeState& state) {
  WorldTile* tile = GetTile(state, state.player_x, state.player_y);
  if (tile == nullptr || tile->resource == ResourceType::None || tile->ore_units == 0) {
    SetStatus(state, "Brak surowca do wydobycia na tym polu.");
    return;
  }

  const int gain = std::min<int>(5 + state.manual_mine_bonus, tile->ore_units);
  if (tile->resource == ResourceType::Iron) {
    state.inv_iron_ore += gain;
    state.total_iron_ore_collected += gain;
  }
  if (tile->resource == ResourceType::Copper) {
    state.inv_copper_ore += gain;
    state.total_copper_ore_collected += gain;
  }
  if (tile->resource == ResourceType::Coal) {
    state.inv_coal_ore += gain;
    state.total_coal_ore_collected += gain;
  }
  state.mined_total += gain;
  tile->ore_units = static_cast<std::uint16_t>(tile->ore_units - gain);
  const char* name = ResourceLabel(tile->resource);
  if (tile->ore_units == 0) tile->resource = ResourceType::None;

  std::ostringstream ss;
  ss << "Wydobyto +" << gain << " " << name << ".";
  SetStatus(state, ss.str(), 2.0f);
}

void TrySmelt(RuntimeState& state) {
  if (state.inv_iron_ore < 2 || state.inv_coal_ore < 1) {
    SetStatus(state, "Za malo surowcow: potrzeba 2x iron ore + 1x coal.");
    return;
  }
  state.inv_iron_ore -= 2;
  state.inv_coal_ore -= 1;
  const int plates_out = 1 + state.smelt_plate_bonus;
  state.inv_iron_plate += plates_out;
  state.smelted_total += plates_out;
  state.total_iron_plate_produced += plates_out;
  std::ostringstream ss;
  ss << "Wytopiono +" << plates_out << " iron plate.";
  SetStatus(state, ss.str(), 2.0f);
}

void RotateBuildDir(RuntimeState& state) {
  state.build_dir = static_cast<std::uint8_t>((state.build_dir + 1u) % 4u);
  static const char* kDirNames[4] = {"N", "E", "S", "W"};
  std::ostringstream ss;
  ss << "Kierunek budowy: " << kDirNames[state.build_dir];
  SetStatus(state, ss.str(), 1.2f);
}

void TryToggleBelt(RuntimeState& state) {
  if (!TechnologyUnlocked(state, "logistics-1")) {
    SetStatus(state, "Belty wymagaja technologii Logistics I.");
    return;
  }
  Machine* machine = FindMachineAt(state, state.player_x, state.player_y);
  if (machine != nullptr) {
    SetStatus(state, "Nie mozna postawic belta na maszynie.");
    return;
  }
  WorldTile* tile = GetTile(state, state.player_x, state.player_y);
  if (tile == nullptr || tile->terrain == TerrainType::Water) {
    SetStatus(state, "Belt nie moze byc na wodzie.");
    return;
  }
  if (tile->has_tree) {
    SetStatus(state, "Najpierw zetnij drzewo (C).", 1.8f);
    return;
  }

  if (tile->has_belt) {
    tile->has_belt = false;
    SetStatus(state, "Usunieto belt.", 1.4f);
    return;
  }

  if (state.inv_iron_plate < 1) {
    SetStatus(state, "Za malo iron plate (koszt belt: 1).", 1.8f);
    return;
  }

  state.inv_iron_plate -= 1;
  tile->has_belt = true;
  tile->belt_dir = state.build_dir;
  SetStatus(state, "Postawiono belt.");
}

void TryChopTree(RuntimeState& state) {
  WorldTile* tile = GetTile(state, state.player_x, state.player_y);
  if (tile == nullptr || !tile->has_tree) {
    SetStatus(state, "Brak drzewa do sciecia.", 1.6f);
    return;
  }
  tile->has_tree = false;
  const int gain = 2 + static_cast<int>((Hash2D(static_cast<std::uint32_t>(state.player_x), static_cast<std::uint32_t>(state.player_y), state.run_seed) & 1u));
  state.inv_wood += gain;
  std::ostringstream ss;
  ss << "Zebrano +" << gain << " wood.";
  SetStatus(state, ss.str(), 1.8f);
}

void TryToggleDrill(RuntimeState& state) {
  if (!TechnologyUnlocked(state, "automation-1")) {
    SetStatus(state, "Extractor wymaga technologii Automation I.");
    return;
  }

  Machine* existing = FindMachineAt(state, state.player_x, state.player_y);
  if (existing != nullptr) {
    if (existing->type != MachineType::Extractor) {
      SetStatus(state, "Pole zajete inna maszyna.");
      return;
    }
    RemoveMachineAt(state, state.player_x, state.player_y, MachineType::Extractor);
    SetStatus(state, "Usunieto extractor.", 1.8f);
    return;
  }

  WorldTile* tile = GetTile(state, state.player_x, state.player_y);
  if (tile == nullptr || tile->resource == ResourceType::None || tile->ore_units == 0) {
    SetStatus(state, "Tutaj nie mozna postawic extractora.");
    return;
  }
  if (tile->has_tree) {
    SetStatus(state, "Najpierw zetnij drzewo (C).", 1.8f);
    return;
  }
  if (state.inv_iron_plate < 4) {
    SetStatus(state, "Za malo iron plate (koszt: 4).");
    return;
  }

  state.inv_iron_plate -= 4;
  state.machines.push_back({state.player_x, state.player_y, MachineType::Extractor, tile->resource, 0.0f, state.build_dir, 0, 0});
  state.extractors_built_total += 1;
  std::ostringstream ss;
  ss << "Postawiono extractor na " << ResourceLabel(tile->resource) << ".";
  SetStatus(state, ss.str());
}

void TryToggleFurnace(RuntimeState& state) {
  Machine* existing = FindMachineAt(state, state.player_x, state.player_y);
  if (existing != nullptr) {
    if (existing->type != MachineType::Furnace) {
      SetStatus(state, "Pole zajete inna maszyna.");
      return;
    }
    RemoveMachineAt(state, state.player_x, state.player_y, MachineType::Furnace);
    SetStatus(state, "Usunieto furnace.", 1.8f);
    return;
  }

  WorldTile* tile = GetTile(state, state.player_x, state.player_y);
  if (tile == nullptr || tile->terrain == TerrainType::Water) {
    SetStatus(state, "Furnace nie moze stac na wodzie.");
    return;
  }
  if (tile->has_tree) {
    SetStatus(state, "Najpierw zetnij drzewo (C).", 1.8f);
    return;
  }
  if (state.inv_iron_plate < 6) {
    SetStatus(state, "Za malo iron plate (koszt furnace: 6).");
    return;
  }

  state.inv_iron_plate -= 6;
  state.machines.push_back({state.player_x, state.player_y, MachineType::Furnace, ResourceType::None, 0.0f, state.build_dir, 0, 0});
  state.furnaces_built_total += 1;
  SetStatus(state, "Postawiono furnace.");
}

void UpdateMachines(RuntimeState& state, float dt) {
  const float extractor_cycle = 1.2f / std::max(0.25f, state.extractor_speed_multiplier);
  const float furnace_cycle = 2.2f;

  int mined_iron_total = 0;
  int mined_copper_total = 0;
  int mined_coal_total = 0;

  for (auto& machine : state.machines) {
    if (machine.type == MachineType::Extractor) {
      machine.timer_s += dt;
      while (machine.timer_s >= extractor_cycle) {
        machine.timer_s -= extractor_cycle;
        WorldTile* ore_tile = GetTile(state, machine.x, machine.y);
        if (ore_tile == nullptr || ore_tile->resource != machine.resource || ore_tile->ore_units == 0) break;

        ore_tile->ore_units = static_cast<std::uint16_t>(ore_tile->ore_units - 1);
        state.mined_total += 1;

        if (machine.resource == ResourceType::Iron) mined_iron_total += 1;
        if (machine.resource == ResourceType::Copper) mined_copper_total += 1;
        if (machine.resource == ResourceType::Coal) mined_coal_total += 1;

        const ItemType out_item = ResourceToItem(machine.resource);
        const int ox = machine.x + kDirDx[machine.dir % 4u];
        const int oy = machine.y + kDirDy[machine.dir % 4u];

        bool delivered = false;
        if (ox >= 0 && oy >= 0 && ox < state.world_w && oy < state.world_h) {
          if (Machine* target = FindMachineAt(state, ox, oy); target != nullptr && target->type == MachineType::Furnace) {
            if (out_item == ItemType::IronOre) {
              target->buf_iron_ore += 1;
              delivered = true;
            } else if (out_item == ItemType::CoalOre) {
              target->buf_coal_ore += 1;
              delivered = true;
            }
          }
          if (!delivered) {
            if (WorldTile* out_tile = GetTile(state, ox, oy); out_tile != nullptr && out_tile->has_belt) {
              state.items.push_back({ox, oy, out_item, 0.0f, out_tile->belt_dir});
              delivered = true;
            }
          }
          if (!delivered) {
            if (WorldTile* out_tile = GetTile(state, ox, oy); out_tile != nullptr && out_tile->terrain != TerrainType::Water) {
              state.items.push_back({ox, oy, out_item, 0.0f, machine.dir});
              delivered = true;
            }
          }
        }

        if (!delivered) {
          // Fallback: "teleport" into inventory so early progression doesn't stall.
          AddItemToInventory(state, out_item, 1);
        }

        if (ore_tile->ore_units == 0) {
          ore_tile->resource = ResourceType::None;
          break;
        }
      }
      continue;
    }

    if (machine.type != MachineType::Furnace) continue;
    machine.timer_s += dt;
    while (machine.timer_s >= furnace_cycle) {
      const bool have_buf = machine.buf_iron_ore >= 2 && machine.buf_coal_ore >= 1;
      const bool have_inv = state.inv_iron_ore >= 2 && state.inv_coal_ore >= 1;
      if (!have_buf && !have_inv) break;

      machine.timer_s -= furnace_cycle;
      if (have_buf) {
        machine.buf_iron_ore -= 2;
        machine.buf_coal_ore -= 1;
      } else {
        state.inv_iron_ore -= 2;
        state.inv_coal_ore -= 1;
      }

      const int plates_out = 1 + state.smelt_plate_bonus;
      state.total_iron_plate_produced += plates_out;
      state.smelted_total += plates_out;

      const int ox = machine.x + kDirDx[machine.dir % 4u];
      const int oy = machine.y + kDirDy[machine.dir % 4u];
      WorldTile* out_tile = GetTile(state, ox, oy);
      if (out_tile != nullptr && out_tile->has_belt) {
        for (int i = 0; i < plates_out; ++i) {
          state.items.push_back({ox, oy, ItemType::IronPlate, 0.0f, out_tile->belt_dir});
        }
      } else {
        state.inv_iron_plate += plates_out;
      }
    }
  }

  // Totals for tech tree (even if items are on belts).
  state.total_iron_ore_collected += mined_iron_total;
  state.total_copper_ore_collected += mined_copper_total;
  state.total_coal_ore_collected += mined_coal_total;

  if (!state.contract_completed && state.contract_target_plates > 0 &&
      state.total_iron_plate_produced >= state.contract_target_plates) {
    state.contract_completed = true;
    state.inv_iron_plate += state.contract_reward_plates;
    state.extractor_speed_multiplier *= 1.10f;
    std::ostringstream ss;
    ss << "Kontrakt zakonczony! Bonus +" << state.contract_reward_plates << " iron plate, +10% extractor speed.";
    SetStatus(state, ss.str(), 4.0f);
  }
}

void UpdateItems(RuntimeState& state, float dt) {
  constexpr float belt_speed_tiles_per_s = 2.8f;

  if (state.items.size() > 3000) {
    state.items.erase(state.items.begin(), state.items.begin() + (state.items.size() - 2000));
  }

  for (std::size_t i = 0; i < state.items.size(); /* increment inside */) {
    ItemEntity& it = state.items[i];
    WorldTile* tile = GetTile(state, it.x, it.y);
    if (tile != nullptr && tile->has_belt) {
      it.dir = tile->belt_dir;
      it.t += dt * belt_speed_tiles_per_s;

      bool erased = false;
      while (it.t >= 1.0f) {
        it.t -= 1.0f;
        const int nx = it.x + kDirDx[it.dir % 4u];
        const int ny = it.y + kDirDy[it.dir % 4u];
        if (nx < 0 || ny < 0 || nx >= state.world_w || ny >= state.world_h) {
          it.t = 0.0f;
          break;
        }

        if (Machine* target = FindMachineAt(state, nx, ny); target != nullptr && target->type == MachineType::Furnace) {
          if (it.type == ItemType::IronOre) {
            target->buf_iron_ore += 1;
            state.items.erase(state.items.begin() + static_cast<std::ptrdiff_t>(i));
            erased = true;
            break;
          }
          if (it.type == ItemType::CoalOre) {
            target->buf_coal_ore += 1;
            state.items.erase(state.items.begin() + static_cast<std::ptrdiff_t>(i));
            erased = true;
            break;
          }
          // Furnace blocks other items.
          it.t = 0.0f;
          break;
        }

        it.x = nx;
        it.y = ny;
        WorldTile* next_tile = GetTile(state, nx, ny);
        if (next_tile == nullptr || !next_tile->has_belt) {
          it.t = 0.0f;
          break;
        }
        it.dir = next_tile->belt_dir;
      }
      if (erased) continue;
    }

    ++i;
  }

  // Auto-pickup on player's tile (keeps early gameplay smooth).
  for (std::size_t i = 0; i < state.items.size(); /* increment inside */) {
    const ItemEntity& it = state.items[i];
    if (it.x == state.player_x && it.y == state.player_y) {
      AddItemToInventory(state, it.type, 1);
      state.items.erase(state.items.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
}
