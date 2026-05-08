#include "render.h"

#include "world.h"

#include <algorithm>
#include <sstream>

namespace {
VisualKind TerrainToVisual(TerrainType t) {
  switch (t) {
    case TerrainType::Water: return VisualKind::Water;
    case TerrainType::Mountain: return VisualKind::Mountain;
    case TerrainType::Highland: return VisualKind::Highland;
    case TerrainType::Midland: return VisualKind::Midland;
    case TerrainType::Lowland: return VisualKind::Lowland;
  }
  return VisualKind::Lowland;
}

VisualKind ResourceToVisual(ResourceType r) {
  switch (r) {
    case ResourceType::Iron: return VisualKind::Iron;
    case ResourceType::Copper: return VisualKind::Copper;
    case ResourceType::Coal: return VisualKind::Coal;
    case ResourceType::None: break;
  }
  return VisualKind::Lowland;
}

const AiTextureGenerator::Texture& GetTexture(RuntimeState& state, VisualKind kind, std::uint32_t variant_seed) {
  const std::uint32_t key = (static_cast<std::uint32_t>(kind) << 24u) | (variant_seed & 0x00FFFFFFu);
  const auto it = state.texture_cache.find(key);
  if (it != state.texture_cache.end()) return it->second;
  return state.texture_cache.emplace(key, state.texture_gen.Generate(kind, key ^ state.run_seed, state.theme_shift)).first->second;
}

void PutPixel(RuntimeState& state, int x, int y, std::uint32_t color) {
  if (x < 0 || y < 0 || x >= state.client_w || y >= state.client_h) return;
  state.back_pixels[static_cast<std::size_t>(y * state.client_w + x)] = color;
}

void DrawIsoTopTextured(RuntimeState& state,
                        int cx,
                        int cy,
                        const AiTextureGenerator::Texture& tex,
                        float brightness) {
  const int half_w = RuntimeState::kTileWidth / 2;
  const int half_h = RuntimeState::kTileHeight / 2;
  for (int dy = -half_h; dy <= half_h; ++dy) {
    const int span = (half_w * (half_h - std::abs(dy))) / half_h;
    if (span <= 0) continue;
    const float v = static_cast<float>(dy + half_h) / static_cast<float>(half_h * 2);
    const int ty = std::clamp(static_cast<int>(v * (AiTextureGenerator::kTextureSize - 1)), 0, AiTextureGenerator::kTextureSize - 1);
    for (int dx = -span; dx <= span; ++dx) {
      const float u = static_cast<float>(dx + span) / static_cast<float>(span * 2);
      const int tx = std::clamp(static_cast<int>(u * (AiTextureGenerator::kTextureSize - 1)), 0, AiTextureGenerator::kTextureSize - 1);
      const auto src = tex[static_cast<std::size_t>(ty * AiTextureGenerator::kTextureSize + tx)];
      PutPixel(state, cx + dx, cy + dy, MulColor(src, brightness));
    }
  }
}

void DrawIsoSides(RuntimeState& state, int cx, int top_cy, int height_px, std::uint32_t top_color) {
  if (height_px <= 0) return;
  const int half_w = RuntimeState::kTileWidth / 2;
  const int half_h = RuntimeState::kTileHeight / 2;

  POINT left_side[4] = {
      {cx - half_w, top_cy},
      {cx, top_cy + half_h},
      {cx, top_cy + half_h + height_px},
      {cx - half_w, top_cy + height_px},
  };
  POINT right_side[4] = {
      {cx + half_w, top_cy},
      {cx, top_cy + half_h},
      {cx, top_cy + half_h + height_px},
      {cx + half_w, top_cy + height_px},
  };

  const COLORREF left_col = static_cast<COLORREF>(MulColor(top_color, 0.65f));
  const COLORREF right_col = static_cast<COLORREF>(MulColor(top_color, 0.52f));
  HBRUSH left_brush = CreateSolidBrush(left_col);
  HBRUSH right_brush = CreateSolidBrush(right_col);
  HPEN no_pen = static_cast<HPEN>(GetStockObject(NULL_PEN));

  HGDIOBJ old_pen = SelectObject(state.back_dc, no_pen);
  HGDIOBJ old_brush = SelectObject(state.back_dc, left_brush);
  Polygon(state.back_dc, left_side, 4);
  SelectObject(state.back_dc, right_brush);
  Polygon(state.back_dc, right_side, 4);

  SelectObject(state.back_dc, old_brush);
  SelectObject(state.back_dc, old_pen);
  DeleteObject(left_brush);
  DeleteObject(right_brush);
}

void DrawIsoOutline(RuntimeState& state, int cx, int cy, std::uint32_t color) {
  const int half_w = RuntimeState::kTileWidth / 2;
  const int half_h = RuntimeState::kTileHeight / 2;
  HPEN pen = CreatePen(PS_SOLID, 1, static_cast<COLORREF>(color));
  HGDIOBJ old_pen = SelectObject(state.back_dc, pen);
  HGDIOBJ old_brush = SelectObject(state.back_dc, GetStockObject(NULL_BRUSH));
  POINT pts[4] = {
      {cx, cy - half_h},
      {cx + half_w, cy},
      {cx, cy + half_h},
      {cx - half_w, cy},
  };
  Polygon(state.back_dc, pts, 4);
  SelectObject(state.back_dc, old_brush);
  SelectObject(state.back_dc, old_pen);
  DeleteObject(pen);
}

void DrawMachineGlyph(RuntimeState& state, int cx, int cy) {
  HBRUSH brush = CreateSolidBrush(RGB(245, 198, 95));
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(30, 24, 20));
  HGDIOBJ old_brush = SelectObject(state.back_dc, brush);
  HGDIOBJ old_pen = SelectObject(state.back_dc, pen);
  Rectangle(state.back_dc, cx - 6, cy - 10, cx + 7, cy + 3);
  MoveToEx(state.back_dc, cx - 3, cy - 6, nullptr);
  LineTo(state.back_dc, cx + 4, cy - 6);
  MoveToEx(state.back_dc, cx, cy - 11, nullptr);
  LineTo(state.back_dc, cx, cy - 14);
  SelectObject(state.back_dc, old_pen);
  SelectObject(state.back_dc, old_brush);
  DeleteObject(brush);
  DeleteObject(pen);
}

void DrawResourcePatch(RuntimeState& state, ResourceType resource, int cx, int cy, std::uint32_t variant) {
  VisualKind kind = ResourceToVisual(resource);
  const auto& tex = GetTexture(state, kind, variant);
  const int patch_half = 5;
  for (int py = -patch_half; py <= patch_half; ++py) {
    const int span = patch_half - std::abs(py);
    for (int px = -span; px <= span; ++px) {
      const float u = static_cast<float>(px + patch_half) / static_cast<float>(patch_half * 2);
      const float v = static_cast<float>(py + patch_half) / static_cast<float>(patch_half * 2);
      const int tx = std::clamp(static_cast<int>(u * (AiTextureGenerator::kTextureSize - 1)), 0, AiTextureGenerator::kTextureSize - 1);
      const int ty = std::clamp(static_cast<int>(v * (AiTextureGenerator::kTextureSize - 1)), 0, AiTextureGenerator::kTextureSize - 1);
      const std::uint32_t src = tex[static_cast<std::size_t>(ty * AiTextureGenerator::kTextureSize + tx)];
      PutPixel(state, cx + px, cy + py - 2, MulColor(src, 1.04f));
    }
  }
}

void DrawBiomeProp(RuntimeState& state, const WorldTile& tile, int cx, int cy, std::uint32_t variant) {
  if (tile.resource != ResourceType::None || tile.terrain == TerrainType::Water) return;
  const bool spawn = ((variant ^ 0x5A) % 31u) == 0u;
  if (!spawn) return;

  COLORREF prop = RGB(120, 170, 110);
  if (tile.biome_region == 1) prop = RGB(167, 149, 104);
  if (tile.biome_region == 2) prop = RGB(130, 165, 186);
  if (tile.biome_region == 3) prop = RGB(147, 136, 126);
  if (tile.terrain == TerrainType::Mountain) prop = RGB(160, 150, 142);

  HBRUSH brush = CreateSolidBrush(prop);
  HPEN pen = CreatePen(PS_SOLID, 1, RGB(28, 28, 28));
  HGDIOBJ old_brush = SelectObject(state.back_dc, brush);
  HGDIOBJ old_pen = SelectObject(state.back_dc, pen);

  if (tile.biome_region == 0 || tile.biome_region == 2) {
    Ellipse(state.back_dc, cx - 3, cy - 10, cx + 4, cy - 2);
    MoveToEx(state.back_dc, cx, cy - 2, nullptr);
    LineTo(state.back_dc, cx, cy + 2);
  } else {
    Rectangle(state.back_dc, cx - 3, cy - 8, cx + 3, cy - 2);
  }

  SelectObject(state.back_dc, old_pen);
  SelectObject(state.back_dc, old_brush);
  DeleteObject(brush);
  DeleteObject(pen);
}

void DrawProgressBar(RuntimeState& state, int x, int y, int w, int h, float t, COLORREF good_col) {
  t = std::clamp(t, 0.0f, 1.0f);
  HBRUSH bg = CreateSolidBrush(RGB(34, 41, 53));
  RECT r{x, y, x + w, y + h};
  FillRect(state.back_dc, &r, bg);
  DeleteObject(bg);

  HBRUSH fg = CreateSolidBrush(good_col);
  RECT fr{x + 1, y + 1, x + 1 + static_cast<int>((w - 2) * t), y + h - 1};
  FillRect(state.back_dc, &fr, fg);
  DeleteObject(fg);
}

void DrawHud(RuntimeState& state) {
  RECT hud_rect{state.client_w - RuntimeState::kHudWidth, 0, state.client_w, state.client_h};
  HBRUSH panel_brush = CreateSolidBrush(RGB(19, 24, 33));
  FillRect(state.back_dc, &hud_rect, panel_brush);
  DeleteObject(panel_brush);

  HPEN border = CreatePen(PS_SOLID, 1, RGB(59, 69, 87));
  HGDIOBJ old_pen = SelectObject(state.back_dc, border);
  MoveToEx(state.back_dc, hud_rect.left, 0, nullptr);
  LineTo(state.back_dc, hud_rect.left, state.client_h);
  SelectObject(state.back_dc, old_pen);
  DeleteObject(border);

  SetBkMode(state.back_dc, TRANSPARENT);
  const int x = hud_rect.left + 18;
  int y = 14;
  auto draw_line = [&](const std::string& text, COLORREF color, int line_h = 22) {
    SetTextColor(state.back_dc, color);
    TextOutA(state.back_dc, x, y, text.c_str(), static_cast<int>(text.size()));
    y += line_h;
  };

  draw_line("FACTORIO-PT // VISUAL PASS", RGB(154, 225, 255), 26);
  draw_line("Runtime: Win32/GDI pseudo-iso", RGB(196, 204, 219));
  draw_line("Map view: terrain-first, clear ore spots", RGB(196, 204, 219));
  y += 4;

  std::ostringstream fps;
  fps << "FPS " << static_cast<int>(state.fps) << "   Frame " << state.frame_time_ms << " ms";
  draw_line(fps.str(), RGB(233, 235, 242));

  std::ostringstream pos;
  pos << "Player (" << state.player_x << ", " << state.player_y << ")";
  draw_line(pos.str(), RGB(233, 235, 242));
  std::ostringstream cam;
  cam << "Camera (" << state.camera_x << ", " << state.camera_y << ")";
  draw_line(cam.str(), RGB(233, 235, 242));
  std::ostringstream day;
  const int day_num = static_cast<int>(state.day_time_s / 180.0f) + 1;
  const int day_part = static_cast<int>(std::fmod(state.day_time_s, 180.0f) / 45.0f);
  static const char* kDayNames[4] = {"Dawn", "Day", "Dusk", "Night"};
  day << "Cycle: Day " << day_num << " - " << kDayNames[std::clamp(day_part, 0, 3)];
  draw_line(day.str(), RGB(233, 235, 242));

  y += 8;
  draw_line("SIM METRICS", RGB(255, 219, 148));
  draw_line("Stability", RGB(210, 220, 235), 18);
  DrawProgressBar(state, x, y, 278, 16, state.snapshot.stability, RGB(104, 212, 127));
  y += 24;
  draw_line("Pollution", RGB(210, 220, 235), 18);
  DrawProgressBar(state, x, y, 278, 16, state.snapshot.pollution, RGB(227, 122, 112));
  y += 24;

  std::ostringstream econ;
  econ << "Wage " << state.snapshot.wage_index << "   Tax " << state.snapshot.tax_rate;
  draw_line(econ.str(), RGB(233, 235, 242));
  y += 8;

  draw_line("GAMEPLAY CORE", RGB(255, 219, 148));
  std::ostringstream inv1;
  inv1 << "Iron ore: " << state.inv_iron_ore << "   Copper ore: " << state.inv_copper_ore;
  draw_line(inv1.str(), RGB(225, 231, 244));
  std::ostringstream inv2;
  inv2 << "Coal: " << state.inv_coal_ore << "   Iron plate: " << state.inv_iron_plate;
  draw_line(inv2.str(), RGB(225, 231, 244));
  std::ostringstream mach;
  mach << "Extractors: " << state.machines.size();
  draw_line(mach.str(), RGB(225, 231, 244));

  y += 8;
  draw_line("ITEM-BASED TECH TREE", RGB(255, 219, 148));
  for (const auto& tech : state.tech_tree) {
    std::ostringstream req;
    req << (tech.unlocked ? "[x] " : "[ ] ") << tech.name;
    draw_line(req.str(), tech.unlocked ? RGB(149, 220, 143) : RGB(219, 225, 238), 20);

    std::ostringstream progress;
    progress << "  req: Fe " << std::min(state.total_iron_ore_collected, tech.req_iron_ore) << "/" << tech.req_iron_ore
             << " Cu " << std::min(state.total_copper_ore_collected, tech.req_copper_ore) << "/" << tech.req_copper_ore
             << " C " << std::min(state.total_coal_ore_collected, tech.req_coal_ore) << "/" << tech.req_coal_ore
             << " Pl " << std::min(state.total_iron_plate_produced, tech.req_iron_plate) << "/" << tech.req_iron_plate;
    draw_line(progress.str(), RGB(182, 191, 210), 18);

    if (tech.unlocked) {
      draw_line("  bonus: " + tech.bonus_text, RGB(140, 216, 157), 18);
    }
  }

  y += 8;
  draw_line("CONTROLS", RGB(154, 225, 255));
  draw_line("WASD = move  |  IJKL = pan", RGB(233, 235, 242));
  draw_line("E = mine  |  F = smelt plate", RGB(233, 235, 242));
  draw_line("B = place/remove extractor", RGB(233, 235, 242));
  draw_line("R = new world style seed", RGB(233, 235, 242));
  draw_line("ESC / Q = quit", RGB(233, 235, 242));

  if (!state.status_text.empty()) {
    y += 10;
    draw_line("STATUS", RGB(255, 219, 148));
    draw_line(state.status_text, RGB(247, 236, 188));
  }
}

void DrawSkyGradient(RuntimeState& state) {
  const float cycle = std::fmod(state.day_time_s, 180.0f) / 180.0f;
  const float phase = 0.5f + 0.5f * std::sin(cycle * 6.283185f - 1.570796f);
  const std::uint32_t night_top = PackColor(10, 14, 24);
  const std::uint32_t night_mid = PackColor(20, 28, 40);
  const std::uint32_t night_bottom = PackColor(12, 16, 24);
  const std::uint32_t day_top = PackColor(54, 90, 132);
  const std::uint32_t day_mid = PackColor(92, 128, 168);
  const std::uint32_t day_bottom = PackColor(30, 46, 68);
  const std::uint32_t top = LerpColor(night_top, day_top, phase);
  const std::uint32_t mid = LerpColor(night_mid, day_mid, phase);
  const std::uint32_t bottom = LerpColor(night_bottom, day_bottom, phase);
  for (int y = 0; y < state.client_h; ++y) {
    const float t = static_cast<float>(y) / static_cast<float>(state.client_h - 1);
    const std::uint32_t col = t < 0.5f ? LerpColor(top, mid, t * 2.0f) : LerpColor(mid, bottom, (t - 0.5f) * 2.0f);
    std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.client_w);
    std::fill(row, row + state.client_w, col);
  }
}

void RenderWorld(RuntimeState& state) {
  const int world_w_px = state.client_w - RuntimeState::kHudWidth;
  state.world_origin_x = world_w_px / 2;

  const int max_sum = state.viewport_tiles_w + state.viewport_tiles_h - 2;
  for (int s = 0; s <= max_sum; ++s) {
    const int tx_min = std::max(0, s - (state.viewport_tiles_h - 1));
    const int tx_max = std::min(state.viewport_tiles_w - 1, s);
    for (int tx = tx_min; tx <= tx_max; ++tx) {
      const int ty = s - tx;
      const int wx = state.camera_x + tx;
      const int wy = state.camera_y + ty;
      if (wx < 0 || wy < 0 || wx >= state.world_w || wy >= state.world_h) continue;
      const auto& tile = state.tiles[TileIndex(state, wx, wy)];

      const int base_x = state.world_origin_x + (tx - ty) * (RuntimeState::kTileWidth / 2);
      const int base_y = state.world_origin_y + (tx + ty) * (RuntimeState::kTileHeight / 2);
      const int elevation_px = static_cast<int>(tile.height_level) * RuntimeState::kHeightStepPx;
      const int top_cy = base_y - elevation_px;

      const std::uint32_t variant = (Hash2D(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy), state.run_seed) &
                                     0xFFu) ^ (static_cast<std::uint32_t>(tile.biome_region) * 47u);
      const auto& terrain_tex = GetTexture(state, TerrainToVisual(tile.terrain), variant);
      const std::uint32_t side_base =
          terrain_tex[static_cast<std::size_t>((AiTextureGenerator::kTextureSize / 2) * AiTextureGenerator::kTextureSize + (AiTextureGenerator::kTextureSize / 2))];

      float brightness = 1.0f;
      if (tile.terrain == TerrainType::Water) {
        const float anim = 0.96f + std::sin(state.water_anim + static_cast<float>((wx + wy) % 16)) * 0.05f;
        brightness = anim;
      } else if (tile.terrain == TerrainType::Mountain) {
        brightness = 0.92f;
      }
      if (tile.biome_region == 0) brightness *= 1.04f;
      if (tile.biome_region == 1) brightness *= 0.97f;
      if (tile.biome_region == 2) brightness *= 1.01f;
      if (tile.biome_region == 3) brightness *= 0.95f;

      DrawIsoSides(state, base_x, top_cy, elevation_px, side_base);
      DrawIsoTopTextured(state, base_x, top_cy, terrain_tex, brightness);
      DrawIsoOutline(state, base_x, top_cy, MulColor(side_base, 0.45f));

      if (tile.resource != ResourceType::None && tile.ore_units > 0) {
        const bool marker = ((wx * 3 + wy * 5 + static_cast<int>(variant)) % 3) != 0;
        if (marker) {
          DrawResourcePatch(state, tile.resource, base_x, top_cy, variant ^ 0x3Du);
        }
      }
      DrawBiomeProp(state, tile, base_x, top_cy, variant ^ 0x2Eu);

      if (FindMachineAt(state, wx, wy) != nullptr) {
        DrawMachineGlyph(state, base_x, top_cy);
      }
      if (wx == state.player_x && wy == state.player_y) {
        DrawIsoOutline(state, base_x, top_cy - 1, PackColor(255, 255, 255));
        DrawIsoOutline(state, base_x, top_cy - 3, PackColor(252, 215, 126));
      }
    }
  }
}
}  // namespace

bool InitBackBuffer(RuntimeState& state) {
  if (state.back_dc != nullptr) {
    DeleteDC(state.back_dc);
    state.back_dc = nullptr;
  }
  if (state.back_bitmap != nullptr) {
    DeleteObject(state.back_bitmap);
    state.back_bitmap = nullptr;
  }

  HDC window_dc = GetDC(state.hwnd);
  if (window_dc == nullptr) return false;

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = state.client_w;
  bmi.bmiHeader.biHeight = -state.client_h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  state.back_dc = CreateCompatibleDC(window_dc);
  state.back_bitmap = CreateDIBSection(window_dc, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&state.back_pixels), nullptr, 0);
  if (state.back_dc == nullptr || state.back_bitmap == nullptr || state.back_pixels == nullptr) {
    ReleaseDC(state.hwnd, window_dc);
    return false;
  }
  SelectObject(state.back_dc, state.back_bitmap);
  ReleaseDC(state.hwnd, window_dc);
  return true;
}

bool HandleResize(RuntimeState& state) {
  RECT rc{};
  if (!GetClientRect(state.hwnd, &rc)) return false;
  const int w = std::max(1, static_cast<int>(rc.right - rc.left));
  const int h = std::max(1, static_cast<int>(rc.bottom - rc.top));
  if (w == state.client_w && h == state.client_h) return true;

  state.client_w = w;
  state.client_h = h;
  const int world_w_px = state.client_w - RuntimeState::kHudWidth;
  state.viewport_tiles_w = std::max(8, world_w_px / RuntimeState::kTileWidth);
  state.viewport_tiles_h = std::max(8, state.client_h / RuntimeState::kTileHeight);
  return InitBackBuffer(state);
}

void Render(RuntimeState& state) {
  DrawSkyGradient(state);
  RenderWorld(state);

  if (state.snapshot.pollution > 0.30f) {
    const float haze = std::clamp((state.snapshot.pollution - 0.30f) * 1.2f, 0.0f, 0.5f);
    const std::uint32_t haze_color = PackColor(62, 52, 44);
    for (int y = 0; y < state.client_h; ++y) {
      std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.client_w);
      for (int x = 0; x < state.client_w - RuntimeState::kHudWidth; ++x) {
        row[x] = LerpColor(row[x], haze_color, haze);
      }
    }
  }

  DrawHud(state);
  HDC window_dc = GetDC(state.hwnd);
  BitBlt(window_dc, 0, 0, state.client_w, state.client_h, state.back_dc, 0, 0, SRCCOPY);
  ReleaseDC(state.hwnd, window_dc);
}
