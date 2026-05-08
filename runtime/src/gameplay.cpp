#include "gameplay.h"

#include "world.h"

#include <algorithm>
#include <sstream>

void TryMine(RuntimeState& state) {
  WorldTile* tile = GetTile(state, state.player_x, state.player_y);
  if (tile == nullptr || tile->resource == ResourceType::None || tile->ore_units == 0) {
    SetStatus(state, "Brak surowca do wydobycia na tym polu.");
    return;
  }

  const int gain = std::min<int>(5, tile->ore_units);
  if (tile->resource == ResourceType::Iron) state.inv_iron_ore += gain;
  if (tile->resource == ResourceType::Copper) state.inv_copper_ore += gain;
  if (tile->resource == ResourceType::Coal) state.inv_coal_ore += gain;
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
  state.inv_iron_plate += 1;
  state.smelted_total += 1;
  SetStatus(state, "Wytopiono +1 iron plate.", 2.0f);
}

void TryToggleDrill(RuntimeState& state) {
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

  for (auto& machine : state.machines) {
    machine.timer_s += dt;
    while (machine.timer_s >= 1.2f) {
      machine.timer_s -= 1.2f;
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
}
