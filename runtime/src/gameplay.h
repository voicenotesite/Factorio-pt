#pragma once

#include "runtime_state.h"

void TryMine(RuntimeState& state);
void TrySmelt(RuntimeState& state);
void TryToggleDrill(RuntimeState& state);
void UpdateMachines(RuntimeState& state, float dt);
