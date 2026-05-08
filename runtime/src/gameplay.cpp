#include "gameplay.h"

#include "world.h"

#include <algorithm>
#include <sstream>

namespace {
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
}  // namespace

void InitializeTechnology(RuntimeState& state) {
  if (!state.tech_tree.empty()) return;
  state.tech_tree = {
      {"metallurgy-1", "Metallurgy I", 30, 0, 20, 0, false, "+1 plate per smelt"},
      {"automation-1", "Automation I", 20, 20, 0, 30, false, "Unlock extractor placement"},
      {"logistics-1", "Logistics I", 40, 30, 30, 60, false, "Extractors work faster"},
      {"geology-1", "Geology I", 55, 55, 40, 80, false, "+2 manual mining yield"},
  };
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

void TryToggleDrill(RuntimeState& state) {
  if (!TechnologyUnlocked(state, "automation-1")) {
    SetStatus(state, "Extractor wymaga technologii Automation I.");
    return;
  }

  Machine* existing = FindMachineAt(state, state.player_x, state.player_y);
  if (existing != nullptr) {
    state.machines.erase(std::remove_if(state.machines.begin(), state.machines.end(),
                                        [&](const Machine& m) { return m.x == state.player_x && m.y == state.player_y; }),
                         state.machines.end());
    SetStatus(state, "Usunieto extractor.");
    return;
  }

  WorldTile* tile = GetTile(state, state.player_x, state.player_y);
  if (tile == nullptr || tile->resource == ResourceType::None || tile->ore_units == 0) {
    SetStatus(state, "Tutaj nie mozna postawic extractora.");
    return;
  }
  if (state.inv_iron_plate < 4) {
    SetStatus(state, "Za malo iron plate (koszt: 4).");
    return;
  }

  state.inv_iron_plate -= 4;
  state.machines.push_back({state.player_x, state.player_y, tile->resource, 0.0f});
  state.extractors_built_total += 1;
  std::ostringstream ss;
  ss << "Postawiono extractor na " << ResourceLabel(tile->resource) << ".";
  SetStatus(state, ss.str());
}

void UpdateMachines(RuntimeState& state, float dt) {
  int produced_iron = 0;
  int produced_copper = 0;
  int produced_coal = 0;

  const float cycle = 1.2f / std::max(0.25f, state.extractor_speed_multiplier);
  for (auto& machine : state.machines) {
    machine.timer_s += dt;
    while (machine.timer_s >= cycle) {
      machine.timer_s -= cycle;
      WorldTile* tile = GetTile(state, machine.x, machine.y);
      if (tile == nullptr || tile->resource != machine.resource || tile->ore_units == 0) break;

      tile->ore_units = static_cast<std::uint16_t>(tile->ore_units - 1);
      if (machine.resource == ResourceType::Iron) ++produced_iron;
      if (machine.resource == ResourceType::Copper) ++produced_copper;
      if (machine.resource == ResourceType::Coal) ++produced_coal;
      if (tile->ore_units == 0) {
        tile->resource = ResourceType::None;
        break;
      }
    }
  }

  state.inv_iron_ore += produced_iron;
  state.inv_copper_ore += produced_copper;
  state.inv_coal_ore += produced_coal;
  state.total_iron_ore_collected += produced_iron;
  state.total_copper_ore_collected += produced_copper;
  state.total_coal_ore_collected += produced_coal;
}
