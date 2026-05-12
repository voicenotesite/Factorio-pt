#pragma once

#include "runtime_state.h"

void InitializeTechnology(RuntimeState& state);
void UpdateTechnologyUnlocks(RuntimeState& state);
bool TechnologyUnlocked(const RuntimeState& state, const char* tech_id);

void TryMine(RuntimeState& state);
void TrySmelt(RuntimeState& state);
void TryToggleDrill(RuntimeState& state);
void TryToggleFurnace(RuntimeState& state);
void TryToggleBelt(RuntimeState& state);
void TryChopTree(RuntimeState& state);
void RotateBuildDir(RuntimeState& state);
void UpdateMachines(RuntimeState& state, float dt);
void UpdateItems(RuntimeState& state, float dt);
