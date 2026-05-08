#pragma once

#include "runtime_state.h"

#include <cstddef>
#include <cstdint>
#include <string>

std::uint32_t Hash2D(std::uint32_t x, std::uint32_t y, std::uint32_t seed);
std::size_t TileIndex(const RuntimeState& state, int x, int y);
WorldTile* GetTile(RuntimeState& state, int x, int y);
Machine* FindMachineAt(RuntimeState& state, int x, int y);
const char* ResourceLabel(ResourceType resource);
void SetStatus(RuntimeState& state, const std::string& text, float seconds = 2.5f);
void GenerateWorld(RuntimeState& state);
void ReseedWorldStyle(RuntimeState& state);
