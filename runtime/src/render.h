#pragma once

#include "runtime_state.h"

bool InitBackBuffer(RuntimeState& state);
bool HandleResize(RuntimeState& state);
bool TryLoadExternalTextureAtlas(RuntimeState& state);
void Render(RuntimeState& state);
