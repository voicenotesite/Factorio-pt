#include "render.h"

#include "world.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

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

std::size_t VisualKindIndex(VisualKind kind) {
  return static_cast<std::size_t>(kind);
}

bool ReadU32(std::ifstream& in, std::uint32_t& out) {
  in.read(reinterpret_cast<char*>(&out), sizeof(out));
  return in.good();
}

bool LoadTextureAtlasFromFile(const std::filesystem::path& atlas_path, RuntimeState& state) {
  std::ifstream in(atlas_path, std::ios::binary);
  if (!in.is_open()) {
    OutputDebugStringA(("✗ Cannot open: " + atlas_path.string() + "\n").c_str());
    return false;
  }

  char magic[4]{};
  in.read(magic, sizeof(magic));
  if (!in.good() || magic[0] != 'F' || magic[1] != 'P' || magic[2] != 'T' || magic[3] != 'A') {
    OutputDebugStringA("✗ Invalid magic\n");
    return false;
  }

  std::uint32_t version = 0;
  std::uint32_t tex_size = 0;
  std::uint32_t kind_count = 0;
  if (!ReadU32(in, version) || !ReadU32(in, tex_size) || !ReadU32(in, kind_count)) {
    OutputDebugStringA("✗ Failed to read header\n");
    return false;
  }
  if (version != 1 || tex_size != AiTextureGenerator::kTextureSize || kind_count != RuntimeState::kVisualKindCount) {
    OutputDebugStringA("✗ Header mismatch\n");
    return false;
  }

  for (auto& bucket : state.external_textures) bucket.clear();

  for (std::uint32_t kind_i = 0; kind_i < kind_count; ++kind_i) {
    std::uint32_t kind_id = 0;
    std::uint32_t variant_count = 0;
    if (!ReadU32(in, kind_id) || !ReadU32(in, variant_count)) return false;
    if (kind_id >= RuntimeState::kVisualKindCount) return false;
    auto& variants = state.external_textures[kind_id];
    variants.reserve(static_cast<std::size_t>(variant_count));
    for (std::uint32_t v = 0; v < variant_count; ++v) {
      AiTextureGenerator::Texture tex{};
      in.read(reinterpret_cast<char*>(tex.data()), static_cast<std::streamsize>(tex.size() * sizeof(std::uint32_t)));
      if (!in.good()) return false;
      variants.push_back(tex);
    }
  }

  state.external_textures_loaded = true;
  state.texture_source = atlas_path.string();
  state.texture_cache.clear();
  OutputDebugStringA(("✓ Atlas loaded: " + atlas_path.string() + "\n").c_str());
  return true;
}

AiTextureGenerator::Texture ApplyVariantShift(const AiTextureGenerator::Texture& src, std::uint32_t variant_seed) {
  AiTextureGenerator::Texture out = src;
  const float tint = 0.985f + (static_cast<float>(variant_seed & 0x07u) / 7.0f) * 0.03f;
  for (auto& px : out) px = MulColor(px, tint);
  return out;
}

AiTextureGenerator::Texture ApplyWaterCorrection(const AiTextureGenerator::Texture& src) {
  AiTextureGenerator::Texture out = src;
  const std::uint32_t target = PackColor(22, 86, 152);
  for (auto& px : out) px = LerpColor(px, target, 0.30f);
  return out;
}

AiTextureGenerator::Texture ApplyVisualSignatureCorrection(const AiTextureGenerator::Texture& src, VisualKind kind) {
  AiTextureGenerator::Texture out = src;
  std::uint32_t target = 0u;
  float t = 0.0f;
  switch (kind) {
    case VisualKind::Lowland:  target = PackColor(78, 106, 52);  t = 0.07f; break;
    case VisualKind::Midland:  target = PackColor(122, 96, 58);  t = 0.07f; break;
    case VisualKind::Highland: target = PackColor(148, 114, 70); t = 0.07f; break;
    case VisualKind::Mountain: target = PackColor(88, 84, 78);   t = 0.08f; break;
    case VisualKind::Iron:     target = PackColor(114, 138, 166); t = 0.08f; break;
    case VisualKind::Copper:   target = PackColor(196, 96, 36);  t = 0.08f; break;
    case VisualKind::Coal:     target = PackColor(38, 38, 44);   t = 0.08f; break;
    case VisualKind::Water: break;
  }
  if (t <= 0.0f) return out;
  for (auto& px : out) px = LerpColor(px, target, t);
  return out;
}

const AiTextureGenerator::Texture& GetTexture(RuntimeState& state, VisualKind kind, std::uint32_t variant_seed) {
  const std::uint32_t key = (static_cast<std::uint32_t>(kind) << 24u) | (variant_seed & 0x00FFFFFFu);
  const auto it = state.texture_cache.find(key);
  if (it != state.texture_cache.end()) return it->second;
  AiTextureGenerator::Texture tex = state.texture_gen.Generate(kind, key ^ state.run_seed, state.theme_shift);
  tex = ApplyVisualSignatureCorrection(tex, kind);
  return state.texture_cache.emplace(key, std::move(tex)).first->second;
}

void PutPixel(RuntimeState& state, int x, int y, std::uint32_t color) {
  // World pixels must never draw into the HUD region.
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  if (x < 0 || y < 0 || x >= world_w_px || y >= state.back_h) return;
  state.back_pixels[static_cast<std::size_t>(y * state.back_w + x)] = color;
}

namespace {
inline std::uint32_t MulColorFast(std::uint32_t c, int factor_256) {
  // factor_256: 0..512 (0..2.0)
  factor_256 = std::clamp(factor_256, 0, 512);
  int r = (static_cast<int>(GetR(c)) * factor_256) >> 8;
  int g = (static_cast<int>(GetG(c)) * factor_256) >> 8;
  int b = (static_cast<int>(GetB(c)) * factor_256) >> 8;
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return PackColor(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b));
}

inline std::uint32_t LerpColorFast(std::uint32_t a, std::uint32_t b, std::uint8_t t) {
  const int it = static_cast<int>(t);
  const int ia = 255 - it;
  const int r = (static_cast<int>(GetR(a)) * ia + static_cast<int>(GetR(b)) * it) / 255;
  const int g = (static_cast<int>(GetG(a)) * ia + static_cast<int>(GetG(b)) * it) / 255;
  const int bb = (static_cast<int>(GetB(a)) * ia + static_cast<int>(GetB(b)) * it) / 255;
  return PackColor(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(bb));
}
}  // namespace

// World-space smooth noise for large-scale organic terrain colour variation.
// Returns value in [0, 1]. Grid size = macroScale tiles.
static float WorldMacroNoise(int wx, int wy, std::uint32_t seed, int macroScale) {
  const int gx0 = wx / macroScale;
  const int gy0 = wy / macroScale;
  const float fx = static_cast<float>(wx % macroScale) / static_cast<float>(macroScale);
  const float fy = static_cast<float>(wy % macroScale) / static_cast<float>(macroScale);
  // Smooth step for natural blending.
  const float ux = fx * fx * (3.0f - 2.0f * fx);
  const float uy = fy * fy * (3.0f - 2.0f * fy);
  const auto h = [&](int x, int y) {
    return static_cast<float>(Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed) & 0xFFu) / 255.0f;
  };
  const float v00 = h(gx0,     gy0);
  const float v10 = h(gx0 + 1, gy0);
  const float v01 = h(gx0,     gy0 + 1);
  const float v11 = h(gx0 + 1, gy0 + 1);
  return v00 + ux * (v10 - v00) + uy * (v01 - v00) + ux * uy * (v00 - v10 - v01 + v11);
}

void DrawTopDownTileTextured(RuntimeState& state,
                             int x0,
                             int y0,
                             const AiTextureGenerator::Texture& tex,
                             float brightness,
                             std::uint32_t transform_seed) {
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  const int factor_256 = static_cast<int>(std::lround(std::clamp(brightness, 0.0f, 2.0f) * 256.0f));

  // Break visible tile grid: sample the same texture with per-tile offset + flips.
  const int ox = static_cast<int>(transform_seed & 7u);
  const int oy = static_cast<int>((transform_seed >> 3u) & 7u);
  const bool flip_x = ((transform_seed >> 6u) & 1u) != 0u;
  const bool flip_y = ((transform_seed >> 7u) & 1u) != 0u;
  const bool transpose = false;

  const bool fully_visible = (x0 >= 0 && y0 >= 0 && x0 + RuntimeState::kTileWidth <= world_w_px &&
                             y0 + RuntimeState::kTileHeight <= state.back_h);
  if (fully_visible && RuntimeState::kTileWidth == AiTextureGenerator::kTextureSize &&
      RuntimeState::kTileHeight == AiTextureGenerator::kTextureSize) {
    for (int dy = 0; dy < RuntimeState::kTileHeight; ++dy) {
      const int sy0 = (dy + oy) % AiTextureGenerator::kTextureSize;
      const int sy = flip_y ? (AiTextureGenerator::kTextureSize - 1 - sy0) : sy0;

      std::uint32_t* dst = state.back_pixels + static_cast<std::size_t>((y0 + dy) * state.back_w + x0);
      const std::uint32_t* src = tex.data() + static_cast<std::size_t>(sy * AiTextureGenerator::kTextureSize);
      for (int dx = 0; dx < RuntimeState::kTileWidth; ++dx) {
        const int sx0 = (dx + ox) % AiTextureGenerator::kTextureSize;
        const int sx = flip_x ? (AiTextureGenerator::kTextureSize - 1 - sx0) : sx0;
        const std::uint32_t texel = transpose
                                        ? tex[static_cast<std::size_t>(sx * AiTextureGenerator::kTextureSize + sy)]
                                        : src[sx];
        dst[dx] = MulColorFast(texel, factor_256);
      }
    }
    return;
  }

  // Clipped/edge tiles.
  for (int dy = 0; dy < RuntimeState::kTileHeight; ++dy) {
    const int sy0 = (dy + oy) % AiTextureGenerator::kTextureSize;
    const int sy = flip_y ? (AiTextureGenerator::kTextureSize - 1 - sy0) : sy0;
    for (int dx = 0; dx < RuntimeState::kTileWidth; ++dx) {
      const int sx0 = (dx + ox) % AiTextureGenerator::kTextureSize;
      const int sx = flip_x ? (AiTextureGenerator::kTextureSize - 1 - sx0) : sx0;
      const auto src = transpose ? tex[static_cast<std::size_t>(sx * AiTextureGenerator::kTextureSize + sy)]
                                 : tex[static_cast<std::size_t>(sy * AiTextureGenerator::kTextureSize + sx)];
      PutPixel(state, x0 + dx, y0 + dy, MulColorFast(src, factor_256));
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

void DrawMachineBuilding(RuntimeState& state, int cx, int cy, const Machine& machine) {
  std::uint32_t accent = PackColor(214, 190, 120);
  if (machine.type == MachineType::Extractor) {
    if (machine.resource == ResourceType::Iron) accent = PackColor(170, 182, 194);
    if (machine.resource == ResourceType::Copper) accent = PackColor(204, 138, 94);
    if (machine.resource == ResourceType::Coal) accent = PackColor(98, 103, 111);
  } else {
    accent = PackColor(210, 140, 80);
  }

  const COLORREF outline = RGB(26, 24, 22);
  const COLORREF body_col = static_cast<COLORREF>(MulColor(accent, 0.90f));
  const COLORREF dark_col = static_cast<COLORREF>(MulColor(accent, 0.65f));

  // Shadow (top-down)
  HBRUSH shadow = CreateSolidBrush(RGB(12, 12, 12));
  HGDIOBJ old_shadow = SelectObject(state.back_dc, shadow);
  Ellipse(state.back_dc, cx - 9, cy + 6, cx + 10, cy + 13);
  SelectObject(state.back_dc, old_shadow);
  DeleteObject(shadow);

  HPEN pen = CreatePen(PS_SOLID, 1, outline);
  HGDIOBJ old_pen = SelectObject(state.back_dc, pen);

  // Base body
  HBRUSH body = CreateSolidBrush(body_col);
  HGDIOBJ old_brush = SelectObject(state.back_dc, body);
  Rectangle(state.back_dc, cx - 9, cy - 9, cx + 10, cy + 10);
  SelectObject(state.back_dc, old_brush);
  DeleteObject(body);

  // Inner details
  HBRUSH inner = CreateSolidBrush(dark_col);
  SelectObject(state.back_dc, inner);
  Rectangle(state.back_dc, cx - 6, cy - 6, cx + 7, cy + 7);
  DeleteObject(inner);

  if (machine.type == MachineType::Extractor) {
    // Drill head
    HBRUSH head = CreateSolidBrush(RGB(74, 84, 96));
    SelectObject(state.back_dc, head);
    Rectangle(state.back_dc, cx + 1, cy - 2, cx + 9, cy + 3);
    DeleteObject(head);
  } else {
    // Furnace chimney + glow
    HBRUSH stack = CreateSolidBrush(RGB(62, 62, 62));
    SelectObject(state.back_dc, stack);
    Rectangle(state.back_dc, cx + 3, cy - 10, cx + 8, cy - 2);
    DeleteObject(stack);

    if (std::fmod(machine.timer_s, 1.0f) > 0.45f) {
      HBRUSH glow = CreateSolidBrush(RGB(230, 160, 92));
      SelectObject(state.back_dc, glow);
      Ellipse(state.back_dc, cx - 2, cy - 2, cx + 3, cy + 3);
      DeleteObject(glow);
    }
  }

  SelectObject(state.back_dc, old_pen);
  DeleteObject(pen);
}

void DrawPlayerGlyph(RuntimeState& state, int cx, int cy) {
  const float pulse = 0.86f + 0.14f * (0.5f + 0.5f * std::sin(state.day_time_s * 4.0f));
  const COLORREF suit       = static_cast<COLORREF>(MulColor(PackColor(234, 210, 146), pulse));
  const COLORREF suit_dark  = static_cast<COLORREF>(MulColor(PackColor(182, 158,  96), pulse));
  const COLORREF suit_shade = static_cast<COLORREF>(MulColor(PackColor(150, 128,  72), pulse));
  const COLORREF visor      = RGB(98, 206, 224);
  const COLORREF visor_hi   = RGB(196, 238, 248);
  const COLORREF outline    = RGB(20, 20, 20);

  // Drop shadow
  HBRUSH shadow_b = CreateSolidBrush(RGB(8, 8, 8));
  SelectObject(state.back_dc, shadow_b);
  HPEN np = CreatePen(PS_NULL, 0, 0);
  SelectObject(state.back_dc, np);
  Ellipse(state.back_dc, cx - 10, cy + 13, cx + 12, cy + 24);
  DeleteObject(shadow_b);
  DeleteObject(np);

  HPEN pen = CreatePen(PS_SOLID, 1, outline);
  HGDIOBJ old_pen = SelectObject(state.back_dc, pen);

  // Legs (below torso, dark)
  HBRUSH legs = CreateSolidBrush(suit_shade);
  HGDIOBJ old_brush = SelectObject(state.back_dc, legs);
  Rectangle(state.back_dc, cx - 7, cy + 8, cx, cy + 16);
  Rectangle(state.back_dc, cx + 1, cy + 8, cx + 8, cy + 16);
  SelectObject(state.back_dc, old_brush);
  DeleteObject(legs);

  // Torso
  HBRUSH body = CreateSolidBrush(suit);
  SelectObject(state.back_dc, body);
  Rectangle(state.back_dc, cx - 8, cy - 6, cx + 9, cy + 10);
  SelectObject(state.back_dc, old_brush);
  DeleteObject(body);

  // Armor stripe
  HBRUSH armor = CreateSolidBrush(suit_dark);
  SelectObject(state.back_dc, armor);
  Rectangle(state.back_dc, cx - 8, cy + 2, cx + 9, cy + 6);
  SelectObject(state.back_dc, old_brush);
  DeleteObject(armor);

  // Right-side torso shading
  HBRUSH side = CreateSolidBrush(suit_shade);
  SelectObject(state.back_dc, side);
  Rectangle(state.back_dc, cx + 5, cy - 6, cx + 9, cy + 10);
  DeleteObject(side);

  // Head
  HBRUSH head = CreateSolidBrush(RGB(224, 214, 186));
  SelectObject(state.back_dc, head);
  Ellipse(state.back_dc, cx - 8, cy - 19, cx + 9, cy - 3);
  DeleteObject(head);

  // Visor
  HBRUSH face = CreateSolidBrush(visor);
  SelectObject(state.back_dc, face);
  Rectangle(state.back_dc, cx - 5, cy - 15, cx + 6, cy - 8);
  DeleteObject(face);

  // Visor reflection highlight
  HBRUSH vhi = CreateSolidBrush(visor_hi);
  SelectObject(state.back_dc, vhi);
  HPEN np2 = CreatePen(PS_NULL, 0, 0);
  HGDIOBJ op2 = SelectObject(state.back_dc, np2);
  Rectangle(state.back_dc, cx - 4, cy - 14, cx - 2, cy - 11);
  SelectObject(state.back_dc, op2);
  DeleteObject(np2);
  DeleteObject(vhi);

  SelectObject(state.back_dc, old_pen);
  DeleteObject(pen);
}

void DrawResourceOverlayTile(RuntimeState& state,
                             ResourceType resource,
                             int x0,
                             int y0,
                             std::uint32_t /*variant*/,
                             std::uint16_t ore_units) {
  // Slight terrain darkening under ore patches — pebbles are drawn by DrawOreRocks.
  if (resource == ResourceType::None || ore_units == 0) return;
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  if (x0 + RuntimeState::kTileWidth <= 0 || y0 + RuntimeState::kTileHeight <= 0 || x0 >= world_w_px || y0 >= state.back_h) return;

  const float d = std::clamp(static_cast<float>(ore_units) / 180.0f, 0.0f, 1.0f);
  const std::uint8_t a = static_cast<std::uint8_t>(std::lround(14.0f + 18.0f * d));
  const std::uint32_t tint = PackColor(28, 26, 22);  // universal slight darkening

  const int x1 = std::min(x0 + RuntimeState::kTileWidth,  world_w_px);
  const int y1 = std::min(y0 + RuntimeState::kTileHeight, state.back_h);
  const int xa = std::max(x0, 0), ya = std::max(y0, 0);
  for (int yy = ya; yy < y1; ++yy) {
    std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(yy * state.back_w + xa);
    for (int xx = xa; xx < x1; ++xx)
      *row = LerpColorFast(*row, tint, a), ++row;
  }
}

inline void FillRectPxClipped(RuntimeState& state, int x0, int y0, int w, int h, std::uint32_t col) {
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  int x1 = x0 + w;
  int y1 = y0 + h;
  x0 = std::clamp(x0, 0, world_w_px);
  y0 = std::clamp(y0, 0, state.back_h);
  x1 = std::clamp(x1, 0, world_w_px);
  y1 = std::clamp(y1, 0, state.back_h);
  if (x0 >= x1 || y0 >= y1) return;

  for (int y = y0; y < y1; ++y) {
    std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.back_w + x0);
    std::fill(row, row + (x1 - x0), col);
  }
}

inline void DrawFilledCirclePx(RuntimeState& state, int cx, int cy, int r, std::uint32_t col) {
  if (r <= 0) return;
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  const int r2 = r * r;
  const int y0 = std::max(cy - r, 0);
  const int y1 = std::min(cy + r, state.back_h - 1);
  for (int yy = y0; yy <= y1; ++yy) {
    const int dy = yy - cy;
    const int span2 = r2 - dy * dy;
    if (span2 <= 0) continue;
    const int dx_max = static_cast<int>(std::sqrt(static_cast<float>(span2)));
    int x_start = std::max(cx - dx_max, 0);
    int x_end = std::min(cx + dx_max, world_w_px - 1);
    if (x_start > x_end) continue;
    std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(yy * state.back_w + x_start);
    std::fill(row, row + (x_end - x_start + 1), col);
  }
}

// World-space ore rock renderer.
// Each tile owns N rocks at world-pixel positions derived from (wx, wy, i, seed).
// We also draw rocks from the 3×3 tile neighbourhood that spill into this tile,
// so there are zero seams at tile boundaries.
// Factorio-style ore: hundreds of tiny pebbles (3-4px), densely packed, world-space seeded.
// Each tile contributes ~28 pebbles; neighbours' pebbles spill over the border → zero seams.
void DrawOreRocks(RuntimeState& state, int wx, int wy, int screen_x0, int screen_y0) {
  const float T  = static_cast<float>(RuntimeState::kTileSize);
  const int   ext = 5;  // max pebble radius + 1 for boundary tests
  const bool use_external = state.external_textures_loaded;

  for (int ny_off = -1; ny_off <= 1; ++ny_off) {
    for (int nx_off = -1; nx_off <= 1; ++nx_off) {
      const int nx = wx + nx_off;
      const int ny = wy + ny_off;
      if (nx < 0 || ny < 0 || nx >= state.world_w || ny >= state.world_h) continue;

      const WorldTile& nt = state.tiles[TileIndex(state, nx, ny)];
      if (nt.resource == ResourceType::None || nt.ore_units == 0) continue;

      // Ore colours. For SD textures, keep neutral so the base rock doesn't tint blue.
      std::uint32_t c_light{}, c_main{}, c_dark{};
      if (nt.resource == ResourceType::Iron) {
        if (use_external) {
          c_light = PackColor(176, 182, 188);
          c_main  = PackColor(128, 136, 144);
          c_dark  = PackColor(86,  92,  98);
        } else {
          c_light = PackColor(172, 196, 224);
          c_main  = PackColor(120, 148, 188);
          c_dark  = PackColor(72,  96,  136);
        }
      } else if (nt.resource == ResourceType::Copper) {
        if (use_external) {
          c_light = PackColor(230, 170, 120);
          c_main  = PackColor(194, 126,  82);
          c_dark  = PackColor(140,  78,  44);
        } else {
          c_light = PackColor(242, 172, 130);
          c_main  = PackColor(210, 128,  86);
          c_dark  = PackColor(148,  80,  44);
        }
      } else {  // Coal
        if (use_external) {
          c_light = PackColor(92,  94, 102);
          c_main  = PackColor(60,  62,  70);
          c_dark  = PackColor(34,  34,  40);
        } else {
          c_light = PackColor(88,  90,  98);
          c_main  = PackColor(58,  60,  68);
          c_dark  = PackColor(32,  32,  38);
        }
      }

      // Density: sparse edge tiles get fewer pebbles than rich centre tiles.
      const float d = std::clamp(static_cast<float>(nt.ore_units) / 180.0f, 0.0f, 1.0f);
      const int count = use_external
                            ? static_cast<int>(std::round(4.0f + 8.0f * d))   // 4–12 small pebbles
                            : static_cast<int>(std::round(16.0f + 20.0f * d));  // 16–36 pebbles/tile

      const std::uint32_t tile_seed = Hash2D(static_cast<std::uint32_t>(nx),
                                             static_cast<std::uint32_t>(ny), state.run_seed);

      for (int i = 0; i < count; ++i) {
        const std::uint32_t h = Hash2D(static_cast<std::uint32_t>(i * 37 + 5), tile_seed, 0xFACE0FFu);

        // World-pixel position of this pebble (uniform within tile — no bias to centre).
        const float px = static_cast<float>(nx) * T + static_cast<float>(h & 31u) / 31.0f * T;
        const float py = static_cast<float>(ny) * T + static_cast<float>((h >> 5u) & 31u) / 31.0f * T;

        // Screen coords relative to tile being drawn.
        const int sx = screen_x0 + static_cast<int>(std::round(px - static_cast<float>(wx) * T));
        const int sy = screen_y0 + static_cast<int>(std::round(py - static_cast<float>(wy) * T));

        if (sx + ext < screen_x0 || sy + ext < screen_y0 ||
            sx - ext >= screen_x0 + RuntimeState::kTileWidth ||
            sy - ext >= screen_y0 + RuntimeState::kTileHeight) continue;

        // Pebble radius: SD uses smaller pebbles to keep base rock visible.
        const int r = use_external
                          ? 1 + static_cast<int>((h >> 10u) & 1u)          // 1–2 px
                          : 1 + static_cast<int>((h >> 10u) & 1u) + static_cast<int>((h >> 11u) & 1u);  // 1–3 px

        // Small 3-layer pebble: dark base, main colour, tiny light top pixel.
        DrawFilledCirclePx(state, sx,     sy,     r,   c_dark);
        DrawFilledCirclePx(state, sx,     sy - 1, r,   c_main);
        if (r >= 2)
          PutPixel(state, sx - 1, sy - r + 1, c_light);
      }
    }
  }
}

void DrawBiomeProp(RuntimeState& state, const WorldTile& tile, int cx, int cy, std::uint32_t variant) {
  if (tile.resource != ResourceType::None || tile.terrain == TerrainType::Water) return;
  // Much rarer + softer so it reads as subtle clutter, not "pepper".
  const bool spawn = ((variant ^ 0x5Au) % 97u) == 0u;
  if (!spawn) return;

  std::uint32_t prop = PackColor(120, 170, 110);
  if (tile.biome_region == 1) prop = PackColor(167, 149, 104);
  if (tile.biome_region == 2) prop = PackColor(130, 165, 186);
  if (tile.biome_region == 3) prop = PackColor(147, 136, 126);
  if (tile.terrain == TerrainType::Mountain) prop = PackColor(160, 150, 142);

  const std::uint32_t p0 = prop;
  const std::uint32_t p1 = MulColorFast(prop, 232);
  const std::uint32_t p2 = MulColorFast(prop, 212);

  PutPixel(state, cx, cy, p0);
  PutPixel(state, cx - 1, cy, p1);
  PutPixel(state, cx + 1, cy, p1);
  PutPixel(state, cx, cy - 1, p1);
  PutPixel(state, cx, cy + 1, p1);
  PutPixel(state, cx - 1, cy - 1, p2);
  PutPixel(state, cx + 1, cy - 1, p2);
  PutPixel(state, cx - 1, cy + 1, p2);
  PutPixel(state, cx + 1, cy + 1, p2);
}

inline void DrawRectPx(RuntimeState& state, int x0, int y0, int w, int h, std::uint32_t col) {
  FillRectPxClipped(state, x0, y0, w, h, col);
}

static inline void BlendBand(RuntimeState& state,
                             int x0,
                             int y0,
                             int w,
                             int h,
                             std::uint32_t color,
                             int alpha_255,
                             std::uint32_t seed);

static inline void BlendCirclePx(RuntimeState& state,
                                 int cx,
                                 int cy,
                                 int r,
                                 std::uint32_t color,
                                 int alpha_255,
                                 std::uint32_t seed);

void DrawWaterTile(RuntimeState& state, int wx, int wy, int x0, int y0, std::uint32_t seed) {
  const std::uint32_t h = Hash2D(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy), seed);
  const int wave_a = static_cast<int>((h >> 3u) & 3u);
  const int wave_b = static_cast<int>((h >> 7u) & 5u);
  const int wave_c = static_cast<int>((h >> 11u) & 7u);

  const std::uint32_t base = PackColor(18, 82, 144);
  const std::uint32_t mid  = PackColor(26, 104, 176);
  const std::uint32_t hi   = PackColor(82, 178, 224);
  const std::uint32_t deep = PackColor(12, 56, 104);

  FillRectPxClipped(state, x0, y0, RuntimeState::kTileWidth, RuntimeState::kTileHeight, base);
  BlendBand(state, x0, y0 + 5 + wave_a, RuntimeState::kTileWidth, 2, mid, 26, seed ^ 0x41u);
  BlendBand(state, x0, y0 + 12 + wave_b, RuntimeState::kTileWidth, 2, hi, 14, seed ^ 0x82u);
  BlendBand(state, x0, y0 + 19 + wave_c, RuntimeState::kTileWidth, 2, deep, 22, seed ^ 0xC3u);
  BlendCirclePx(state, x0 + 8 + static_cast<int>((h >> 14u) & 7u), y0 + 7 + static_cast<int>((h >> 17u) & 5u), 2, hi, 22, seed ^ 0x19u);
  BlendCirclePx(state, x0 + 20 - static_cast<int>((h >> 19u) & 5u), y0 + 16, 2, mid, 18, seed ^ 0x2Bu);
}

void DrawMaterialTile(RuntimeState& state, const WorldTile& tile, int wx, int wy, int x0, int y0, std::uint32_t seed) {
  const std::uint32_t h = Hash2D(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy), seed);
  const int fx = static_cast<int>((h >> 3u) & 7u) - 3;
  const int fy = static_cast<int>((h >> 6u) & 7u) - 3;
  const int gx = x0 + RuntimeState::kTileWidth / 2 + fx;
  const int gy = y0 + RuntimeState::kTileHeight / 2 + fy;

  std::uint32_t base = PackColor(70, 96, 54);
  std::uint32_t hi = PackColor(110, 142, 82);
  std::uint32_t shadow = PackColor(44, 68, 36);
  std::uint32_t accent = PackColor(92, 124, 64);

  switch (tile.terrain) {
    case TerrainType::Lowland:
      base = tile.biome_region == 1 ? PackColor(126, 112, 66) : PackColor(72, 104, 56);
      hi = tile.biome_region == 1 ? PackColor(160, 140, 88) : PackColor(118, 150, 86);
      shadow = tile.biome_region == 1 ? PackColor(88, 72, 42) : PackColor(44, 72, 38);
      accent = tile.biome_region == 3 ? PackColor(96, 132, 70) : PackColor(102, 136, 74);
      break;
    case TerrainType::Midland:
      base = tile.biome_region == 2 ? PackColor(88, 102, 100) : PackColor(102, 90, 62);
      hi = tile.biome_region == 2 ? PackColor(124, 144, 140) : PackColor(138, 126, 86);
      shadow = tile.biome_region == 2 ? PackColor(52, 64, 66) : PackColor(74, 62, 42);
      accent = tile.biome_region == 2 ? PackColor(100, 118, 116) : PackColor(126, 110, 72);
      break;
    case TerrainType::Highland:
      base = PackColor(112, 108, 96);
      hi = PackColor(150, 146, 132);
      shadow = PackColor(68, 64, 56);
      accent = PackColor(132, 128, 116);
      break;
    case TerrainType::Mountain:
      base = PackColor(84, 82, 80);
      hi = PackColor(142, 136, 132);
      shadow = PackColor(44, 44, 48);
      accent = PackColor(108, 104, 102);
      break;
    case TerrainType::Water:
      DrawWaterTile(state, wx, wy, x0, y0, seed);
      return;
  }

  FillRectPxClipped(state, x0, y0, RuntimeState::kTileWidth, RuntimeState::kTileHeight, base);

  const int shift_x = static_cast<int>((h >> 9u) & 3u) - 1;
  const int shift_y = static_cast<int>((h >> 12u) & 3u) - 1;
  BlendCirclePx(state, gx - 3 + shift_x, gy - 2 + shift_y, 6, hi, 42, seed ^ 0x51u);
  BlendCirclePx(state, gx + 2 - shift_x, gy + 3 - shift_y, 4, shadow, 34, seed ^ 0x72u);
  BlendCirclePx(state, gx - 5 + shift_x, gy + 1 + shift_y, 3, accent, 24, seed ^ 0x93u);

  if (tile.terrain == TerrainType::Mountain) {
    BlendBand(state, x0 + 2, y0 + 7, RuntimeState::kTileWidth - 4, 2, PackColor(170, 168, 164), 24, seed ^ 0xA1u);
    BlendBand(state, x0 + 4, y0 + 14, RuntimeState::kTileWidth - 8, 2, PackColor(58, 56, 60), 26, seed ^ 0xB2u);
  } else if (tile.terrain == TerrainType::Highland) {
    BlendBand(state, x0 + 2, y0 + 6, RuntimeState::kTileWidth - 4, 2, PackColor(154, 148, 122), 20, seed ^ 0xC3u);
  } else if (tile.terrain == TerrainType::Midland) {
    BlendBand(state, x0 + 1, y0 + 9, RuntimeState::kTileWidth - 2, 2, PackColor(120, 104, 70), 18, seed ^ 0xD4u);
  } else {
    BlendBand(state, x0 + 1, y0 + 10, RuntimeState::kTileWidth - 2, 2, PackColor(84, 128, 60), 14, seed ^ 0xE5u);
  }
}

void DrawTree(RuntimeState& state, int cx, int cy, std::uint8_t biome_region, std::uint8_t variant) {
  const float pol = std::clamp(state.snapshot.pollution, 0.0f, 1.0f);
  const float s = 1.10f;

  std::uint32_t leaf = PackColor(72, 148, 74);
  std::uint32_t leaf2 = PackColor(56, 124, 62);
  if (biome_region == 1) {
    leaf = PackColor(146, 156, 78);
    leaf2 = PackColor(124, 132, 64);
  } else if (biome_region == 2) {
    leaf = PackColor(118, 160, 130);
    leaf2 = PackColor(96, 132, 110);
  } else if (biome_region == 3) {
    leaf = PackColor(112, 138, 92);
    leaf2 = PackColor(90, 112, 72);
  }

  const std::uint32_t dead = PackColor(86, 82, 78);
  const float kill = std::clamp(std::max(0.0f, pol - 0.55f) * 1.6f, 0.0f, 1.0f);
  leaf = LerpColor(leaf, dead, kill);
  leaf2 = LerpColor(leaf2, dead, kill);
  const std::uint32_t leaf3 = MulColor(leaf2, 0.88f);

  // Stable silhouette; no time-based wobble.
  const int lean = static_cast<int>(variant & 1u) - 1;
  const int crown_shift = static_cast<int>((variant >> 1u) & 1u) + lean;
  const bool perf_low = (state.fps > 0.0f && state.fps < 46.0f);

  if (perf_low) {
    DrawFilledCirclePx(state, cx + 1, cy + static_cast<int>(std::lround(8.0f * s)), static_cast<int>(std::lround(4.0f * s)), PackColor(10, 10, 10));
    const int trunk_w = std::max(2, static_cast<int>(std::lround(2.0f * s)));
    const int trunk_h = std::max(4, static_cast<int>(std::lround(5.0f * s)));
    DrawRectPx(state, cx - trunk_w / 2, cy + 2, trunk_w, trunk_h, PackColor(92, 68, 44));
    DrawFilledCirclePx(state, cx + crown_shift, cy - static_cast<int>(std::lround(3.0f * s)), static_cast<int>(std::lround(5.0f * s)), leaf);
    PutPixel(state, cx + crown_shift - 1, cy - static_cast<int>(std::lround(4.0f * s)), PackColor(214, 224, 214));
    return;
  }

  // Shadow
  DrawFilledCirclePx(state, cx + 1, cy + static_cast<int>(std::lround(8.0f * s)), static_cast<int>(std::lround(6.0f * s)), PackColor(10, 10, 10));

  // Trunk + bark shade
  const int trunk_w = std::max(2, static_cast<int>(std::lround(2.0f * s)));
  const int trunk_h = std::max(4, static_cast<int>(std::lround(6.0f * s)));
  const int trunk_x = cx - trunk_w / 2;
  const int trunk_y = cy + 2;
  DrawRectPx(state, trunk_x, trunk_y, trunk_w, trunk_h, PackColor(92, 68, 44));
  DrawRectPx(state, trunk_x + trunk_w / 2, trunk_y, 1, trunk_h, PackColor(78, 56, 34));

  // Canopy: one clear mass with small secondary clump.
  DrawFilledCirclePx(state, cx + crown_shift, cy - static_cast<int>(std::lround(3.0f * s)), static_cast<int>(std::lround(6.0f * s)), leaf);
  DrawFilledCirclePx(state, cx - crown_shift / 2, cy - static_cast<int>(std::lround(1.0f * s)), static_cast<int>(std::lround(4.0f * s)), leaf2);
  DrawFilledCirclePx(state, cx + crown_shift / 2, cy - static_cast<int>(std::lround(5.0f * s)), static_cast<int>(std::lround(2.5f * s)), leaf3);

  // Small highlight
  PutPixel(state, cx + crown_shift - 2, cy - static_cast<int>(std::lround(4.0f * s)), PackColor(220, 230, 220));
}

std::uint32_t ItemToColor(ItemType type) {
  switch (type) {
    case ItemType::IronOre: return PackColor(178, 186, 194);
    case ItemType::CopperOre: return PackColor(214, 144, 92);
    case ItemType::CoalOre: return PackColor(72, 74, 82);
    case ItemType::IronPlate: return PackColor(224, 224, 230);
  }
  return PackColor(255, 255, 255);
}

void DrawBeltOverlay(RuntimeState& state, int x0, int y0, std::uint8_t dir) {
  const int cx = x0 + RuntimeState::kTileWidth / 2;
  const int cy = y0 + RuntimeState::kTileHeight / 2;
  const std::uint32_t belt_col = PackColor(58, 62, 70);
  const std::uint32_t arrow_col = PackColor(170, 176, 188);

  auto line_h = [&](int y, int x1, int x2, std::uint32_t col) {
    for (int x = x1; x <= x2; ++x) PutPixel(state, x, y, col);
  };
  auto line_v = [&](int x, int y1, int y2, std::uint32_t col) {
    for (int y = y1; y <= y2; ++y) PutPixel(state, x, y, col);
  };

  dir %= 4u;
  if (dir == 1u || dir == 3u) {
    line_h(cy - 2, cx - 10, cx + 10, belt_col);
    line_h(cy + 2, cx - 10, cx + 10, belt_col);
    const int tip = (dir == 1u) ? (cx + 10) : (cx - 10);
    PutPixel(state, tip, cy, arrow_col);
    PutPixel(state, tip + (dir == 1u ? -1 : 1), cy - 1, arrow_col);
    PutPixel(state, tip + (dir == 1u ? -1 : 1), cy + 1, arrow_col);
  } else {
    line_v(cx - 2, cy - 10, cy + 10, belt_col);
    line_v(cx + 2, cy - 10, cy + 10, belt_col);
    const int tip = (dir == 2u) ? (cy + 10) : (cy - 10);
    PutPixel(state, cx, tip, arrow_col);
    PutPixel(state, cx - 1, tip + (dir == 2u ? -1 : 1), arrow_col);
    PutPixel(state, cx + 1, tip + (dir == 2u ? -1 : 1), arrow_col);
  }
}

void DrawItemEntity(RuntimeState& state, const ItemEntity& item) {
  const int x0 = state.world_origin_x + (item.x - state.camera_x) * RuntimeState::kTileWidth;
  const int y0 = state.world_origin_y + (item.y - state.camera_y) * RuntimeState::kTileHeight;
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;

  if (x0 + RuntimeState::kTileWidth < 0 || x0 >= world_w_px || y0 + RuntimeState::kTileHeight < 0 || y0 >= state.back_h) return;

  const std::uint32_t col = ItemToColor(item.type);
  const int pad = 4;
  const int max_travel = RuntimeState::kTileWidth - pad * 2;
  const int t_px = pad + static_cast<int>(std::clamp(item.t, 0.0f, 1.0f) * static_cast<float>(max_travel));

  int px = x0 + RuntimeState::kTileWidth / 2;
  int py = y0 + RuntimeState::kTileHeight / 2;
  const std::uint8_t dir = item.dir % 4u;
  if (dir == 1u) px = x0 + t_px;
  if (dir == 3u) px = x0 + (RuntimeState::kTileWidth - t_px);
  if (dir == 2u) py = y0 + t_px;
  if (dir == 0u) py = y0 + (RuntimeState::kTileHeight - t_px);

  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      PutPixel(state, px + dx, py + dy, col);
    }
  }
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
  if (state.hud_mode == 0) return;

  const int pad = 10;
  const int x0 = 12;
  const int y0 = 12;
  const int w = (state.hud_mode == 2) ? 420 : 330;
  const int h = (state.hud_mode == 2) ? 420 : 190;

  RECT hud_rect{x0, y0, x0 + w, y0 + h};
  HBRUSH panel_brush = CreateSolidBrush(RGB(18, 22, 30));
  FillRect(state.back_dc, &hud_rect, panel_brush);
  DeleteObject(panel_brush);

  HPEN border = CreatePen(PS_SOLID, 1, RGB(59, 69, 87));
  HGDIOBJ old_pen = SelectObject(state.back_dc, border);
  HGDIOBJ old_brush = SelectObject(state.back_dc, GetStockObject(NULL_BRUSH));
  Rectangle(state.back_dc, hud_rect.left, hud_rect.top, hud_rect.right, hud_rect.bottom);
  SelectObject(state.back_dc, old_brush);
  SelectObject(state.back_dc, old_pen);
  DeleteObject(border);

  SetBkMode(state.back_dc, TRANSPARENT);
  const int x = x0 + pad;
  int y = y0 + pad;
  auto draw_line = [&](const std::string& text, COLORREF color, int line_h = 18) {
    SetTextColor(state.back_dc, color);
    TextOutA(state.back_dc, x, y, text.c_str(), static_cast<int>(text.size()));
    y += line_h;
  };

  std::ostringstream fps;
  fps << "FPS " << static_cast<int>(state.fps) << "  " << static_cast<int>(state.frame_time_ms) << "ms";
  draw_line("ORBITUM", RGB(154, 225, 255), 20);
  draw_line("by Sh1t3ad Dev", RGB(120, 180, 120), 16);
  draw_line(fps.str(), RGB(233, 235, 242));

  std::ostringstream res;
  res << "Render " << state.back_w << "x" << state.back_h << " -> " << state.client_w << "x" << state.client_h
      << (state.fixed_backbuffer ? " (fixed/F8)" : " (native/F8)");
  draw_line(res.str(), RGB(170, 178, 195));
  const bool perf_critical = (state.fps > 0.0f && state.fps < 32.0f);
  const bool perf_low = (state.fps > 0.0f && state.fps < 46.0f);
  std::ostringstream quality;
  quality << (perf_critical ? "Quality PERF-CRITICAL" : (perf_low ? "Quality BALANCED" : "Quality HIGH"))
          << " | AutoGov " << (state.auto_perf_governor ? "ON(F7)" : "OFF(F7)");
  draw_line(quality.str(), RGB(181, 201, 226));

  static const char* kDirNames[4] = {"N", "E", "S", "W"};
  std::ostringstream dir;
  dir << "Dir " << kDirNames[state.build_dir % 4u] << "   (H = HUD)";
  draw_line(dir.str(), RGB(196, 204, 219));

  std::ostringstream inv;
  inv << "FeOre " << state.inv_iron_ore << "  CuOre " << state.inv_copper_ore << "  Coal " << state.inv_coal_ore;
  draw_line(inv.str(), RGB(225, 231, 244));
  std::ostringstream inv2;
  inv2 << "Plate " << state.inv_iron_plate << "  Wood " << state.inv_wood;
  draw_line(inv2.str(), RGB(225, 231, 244));

  int extractor_count = 0;
  int furnace_count = 0;
  for (const auto& machine : state.machines) {
    if (machine.type == MachineType::Extractor) extractor_count++;
    if (machine.type == MachineType::Furnace) furnace_count++;
  }
  std::ostringstream mach;
  mach << "Extractors " << extractor_count << "  Furnaces " << furnace_count;
  draw_line(mach.str(), RGB(210, 220, 235));

  if (!state.status_text.empty()) {
    draw_line(state.status_text, RGB(247, 236, 188));
  }

  if (state.hud_mode == 2) {
    y += 6;
    draw_line("SIM", RGB(255, 219, 148));
    draw_line("Stability", RGB(210, 220, 235), 16);
    DrawProgressBar(state, x, y, w - pad * 2, 14, state.snapshot.stability, RGB(104, 212, 127));
    y += 20;
    draw_line("Pollution", RGB(210, 220, 235), 16);
    DrawProgressBar(state, x, y, w - pad * 2, 14, state.snapshot.pollution, RGB(227, 122, 112));
  }

  // Controls hint (compact)
  y = hud_rect.bottom - pad - 18;
  draw_line("WASD move | Shift run | E mine | F smelt | B/N/T build | C chop | R rotate | P reseed | F7 auto | F8 res", RGB(182, 191, 210));
}

void DrawSkyGradient(RuntimeState& state) {
  // Factorio-like: flat background. World tiles fully cover the play area anyway.
  const std::uint32_t bg = PackColor(16, 18, 22);
  for (int y = 0; y < state.back_h; ++y) {
    std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.back_w);
    std::fill(row, row + state.back_w, bg);
  }
}

static inline void BlendBand(RuntimeState& state,
                            int x0,
                            int y0,
                            int w,
                            int h,
                            std::uint32_t color,
                            int alpha_255,
                            std::uint32_t seed) {
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  int x1 = x0 + w;
  int y1 = y0 + h;
  x0 = std::clamp(x0, 0, world_w_px);
  y0 = std::clamp(y0, 0, state.back_h);
  x1 = std::clamp(x1, 0, world_w_px);
  y1 = std::clamp(y1, 0, state.back_h);
  if (x0 >= x1 || y0 >= y1) return;

  for (int y = y0; y < y1; ++y) {
    std::uint32_t* dst = state.back_pixels + static_cast<std::size_t>(y * state.back_w + x0);
    for (int x = x0; x < x1; ++x) {
      // Slight alpha jitter so seams aren't perfectly straight.
      const int jitter = static_cast<int>(((seed + static_cast<std::uint32_t>(x * 17 + y * 23)) >> 4u) & 7u) - 3;
      const int a = std::clamp(alpha_255 + jitter * 6, 0, 255);
      dst[x - x0] = LerpColorFast(dst[x - x0], color, static_cast<std::uint8_t>(a));
    }
  }
}

static inline void DarkenBand(RuntimeState& state, int x0, int y0, int w, int h, int factor_256) {
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  int x1 = x0 + w;
  int y1 = y0 + h;
  x0 = std::clamp(x0, 0, world_w_px);
  y0 = std::clamp(y0, 0, state.back_h);
  x1 = std::clamp(x1, 0, world_w_px);
  y1 = std::clamp(y1, 0, state.back_h);
  if (x0 >= x1 || y0 >= y1) return;

  for (int y = y0; y < y1; ++y) {
    std::uint32_t* dst = state.back_pixels + static_cast<std::size_t>(y * state.back_w + x0);
    for (int x = x0; x < x1; ++x) {
      dst[x - x0] = MulColorFast(dst[x - x0], factor_256);
    }
  }
}

static inline void BlendCirclePx(RuntimeState& state,
                                 int cx,
                                 int cy,
                                 int r,
                                 std::uint32_t color,
                                 int alpha_255,
                                 std::uint32_t seed) {
  if (r <= 0 || alpha_255 <= 0) return;
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  const int r2 = r * r;
  const int y0 = std::max(cy - r, 0);
  const int y1 = std::min(cy + r, state.back_h - 1);
  for (int y = y0; y <= y1; ++y) {
    const int dy = y - cy;
    const int dy2 = dy * dy;
    std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.back_w);
    const int x0 = std::max(cx - r, 0);
    const int x1 = std::min(cx + r, world_w_px - 1);
    for (int x = x0; x <= x1; ++x) {
      const int dx = x - cx;
      const int d2 = dx * dx + dy2;
      if (d2 > r2) continue;
      const int edge = ((r2 - d2) * 255) / std::max(1, r2);
      const int jitter = static_cast<int>((Hash2D(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), seed) >> 3u) & 31u) - 15;
      const int a = std::clamp((alpha_255 * edge) / 255 + jitter, 0, 255);
      if (a <= 0) continue;
      row[x] = LerpColorFast(row[x], color, static_cast<std::uint8_t>(a));
    }
  }
}

static inline void DrawMacroGroundDecal(RuntimeState& state,
                                        const WorldTile& tile,
                                        int wx,
                                        int wy,
                                        int x0,
                                        int y0,
                                        std::uint32_t variant,
                                        bool perf_low,
                                        bool perf_critical) {
  if (tile.terrain == TerrainType::Water || tile.terrain == TerrainType::Mountain) return;
  if ((wx & 1) != 0 || (wy & 1) != 0) return;
  if (perf_critical) return;

  const std::uint32_t h = Hash2D(static_cast<std::uint32_t>(wx >> 1), static_cast<std::uint32_t>(wy >> 1), variant ^ 0xA19C73u);
  if ((h & 15u) > (perf_low ? 2u : 5u)) return;

  int jx = static_cast<int>((h >> 8u) & 31u) - 15;
  int jy = static_cast<int>((h >> 13u) & 31u) - 15;
  const int cx = x0 + RuntimeState::kTileWidth / 2 + jx;
  const int cy = y0 + RuntimeState::kTileHeight / 2 + jy;
  const int r = perf_low ? (5 + static_cast<int>((h >> 18u) & 3u)) : (7 + static_cast<int>((h >> 18u) & 7u));

  std::uint32_t tint = PackColor(84, 76, 60);
  if (tile.biome_region == 0) tint = PackColor(70, 92, 64);
  if (tile.biome_region == 1) tint = PackColor(106, 88, 62);
  if (tile.biome_region == 2) tint = PackColor(70, 94, 98);
  if (tile.biome_region == 3) tint = PackColor(92, 86, 76);
  if (tile.terrain == TerrainType::Highland) tint = MulColorFast(tint, 220);
  if (tile.terrain == TerrainType::Lowland) tint = MulColorFast(tint, 245);

  const int alpha = perf_low ? 22 : 34;
  BlendCirclePx(state, cx, cy, r, tint, alpha, h ^ 0x5B4Du);
}

static inline const WorldTile* GetTileSafe(const RuntimeState& state, int wx, int wy) {
  if (wx < 0 || wy < 0 || wx >= state.world_w || wy >= state.world_h) return nullptr;
  return &state.tiles[TileIndex(state, wx, wy)];
}

static inline void ApplyTerrainEdgesAndAO(RuntimeState& state, int wx, int wy, int x0, int y0, std::uint32_t seed) {
  const WorldTile* t = GetTileSafe(state, wx, wy);
  if (!t) return;

  const WorldTile* n = GetTileSafe(state, wx, wy - 1);
  const WorldTile* s = GetTileSafe(state, wx, wy + 1);
  const WorldTile* w = GetTileSafe(state, wx - 1, wy);
  const WorldTile* e = GetTileSafe(state, wx + 1, wy);

  const bool is_water = (t->terrain == TerrainType::Water);

  // Readable transitions; no copyrighted assets.
  const std::uint32_t dirt_edge = PackColor(42, 36, 28);
  const std::uint32_t wet_edge = PackColor(36, 48, 51);
  const std::uint32_t dry_edge = PackColor(58, 51, 39);

  auto do_edge = [&](const WorldTile* nb, int bx, int by, int bw, int bh, bool north_south) {
    if (!nb) return;

    if (t->terrain != nb->terrain) {
      if (!is_water && nb->terrain != TerrainType::Water) {
        BlendBand(state, bx, by, bw, bh, dirt_edge, 58, seed);
        BlendBand(state, bx, by, bw, bh, dry_edge, 30, seed + 1337u);
      } else if (!is_water && nb->terrain == TerrainType::Water) {
        BlendBand(state, bx, by, bw, bh, wet_edge, 88, seed);
      } else if (is_water && nb->terrain != TerrainType::Water) {
        DarkenBand(state, bx, by, bw, bh, 236);
      }
    }

    if (nb->height_level > t->height_level) {
      DarkenBand(state, bx, by, bw, bh, 220);
      // Slight falloff inward so it's not a harsh outline.
      if (north_south) {
        DarkenBand(state, bx, by + 1, bw, std::min(2, bh), 238);
      } else {
        DarkenBand(state, bx + 1, by, std::min(2, bw), bh, 238);
      }
    }
  };

  const int tw = RuntimeState::kTileWidth;
  const int th = RuntimeState::kTileHeight;

  // 4px feathered bands for softer, less stair-stepped biome edges.
  do_edge(n, x0, y0, tw, 4, true);
  do_edge(s, x0, y0 + th - 4, tw, 4, true);
  do_edge(w, x0, y0, 4, th, false);
  do_edge(e, x0 + tw - 4, y0, 4, th, false);

  // Corner blend avoids "plus sign" seams where 4 tiles meet.
  const std::uint32_t seam_corner = is_water ? PackColor(24, 34, 44) : PackColor(44, 38, 30);
  BlendBand(state, x0, y0, 4, 4, seam_corner, 56, seed ^ 0x11u);
  BlendBand(state, x0 + tw - 4, y0, 4, 4, seam_corner, 56, seed ^ 0x22u);
  BlendBand(state, x0, y0 + th - 4, 4, 4, seam_corner, 56, seed ^ 0x33u);
  BlendBand(state, x0 + tw - 4, y0 + th - 4, 4, 4, seam_corner, 56, seed ^ 0x44u);
}

void RenderWorld(RuntimeState& state) {
  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  const int saved_dc = SaveDC(state.back_dc);
  IntersectClipRect(state.back_dc, 0, 0, world_w_px, state.back_h);

  state.world_origin_x = static_cast<int>(std::lround(-state.camera_sub_x * static_cast<float>(RuntimeState::kTileWidth)));
  state.world_origin_y = static_cast<int>(std::lround(-state.camera_sub_y * static_cast<float>(RuntimeState::kTileHeight)));

  const int draw_w = state.viewport_tiles_w + 2;
  const int draw_h = state.viewport_tiles_h + 2;
  const bool perf_critical = (state.fps > 0.0f && state.fps < 32.0f);

  // Pass 1: tiles + resources + belts
  for (int ty = 0; ty < draw_h; ++ty) {
    for (int tx = 0; tx < draw_w; ++tx) {
      const int wx = state.camera_x + tx;
      const int wy = state.camera_y + ty;
      if (wx < 0 || wy < 0 || wx >= state.world_w || wy >= state.world_h) continue;

      const int x0 = state.world_origin_x + tx * RuntimeState::kTileWidth;
      const int y0 = state.world_origin_y + ty * RuntimeState::kTileHeight;

      const auto& tile = state.tiles[TileIndex(state, wx, wy)];

      const std::uint32_t block_seed = Hash2D(static_cast<std::uint32_t>(wx / 8),
                                              static_cast<std::uint32_t>(wy / 8),
                                              state.run_seed ^ 0x6AC1u);
      const std::uint32_t detail_variant = block_seed ^ (static_cast<std::uint32_t>(tile.biome_region) * 47u);
      DrawMaterialTile(state, tile, wx, wy, x0, y0, block_seed);

      // Shoreline hints for readable lakes/rivers.
      if (tile.terrain == TerrainType::Water) {
        const std::uint32_t foam = PackColor(186, 214, 230);
        const std::uint32_t deep = PackColor(12, 32, 56);
        auto is_water = [&](int nx, int ny) {
          if (nx < 0 || ny < 0 || nx >= state.world_w || ny >= state.world_h) return true;
          return state.tiles[TileIndex(state, nx, ny)].terrain == TerrainType::Water;
        };
        const bool n = is_water(wx, wy - 1);
        const bool s = is_water(wx, wy + 1);
        const bool w = is_water(wx - 1, wy);
        const bool e = is_water(wx + 1, wy);
        if (!n) {
          FillRectPxClipped(state, x0, y0, RuntimeState::kTileWidth, 1, foam);
          FillRectPxClipped(state, x0, y0 + 1, RuntimeState::kTileWidth, 1, deep);
        }
        if (!s) {
          FillRectPxClipped(state, x0, y0 + RuntimeState::kTileHeight - 1, RuntimeState::kTileWidth, 1, foam);
          FillRectPxClipped(state, x0, y0 + RuntimeState::kTileHeight - 2, RuntimeState::kTileWidth, 1, deep);
        }
        if (!w) {
          FillRectPxClipped(state, x0, y0, 1, RuntimeState::kTileHeight, foam);
          FillRectPxClipped(state, x0 + 1, y0, 1, RuntimeState::kTileHeight, deep);
        }
        if (!e) {
          FillRectPxClipped(state, x0 + RuntimeState::kTileWidth - 1, y0, 1, RuntimeState::kTileHeight, foam);
          FillRectPxClipped(state, x0 + RuntimeState::kTileWidth - 2, y0, 1, RuntimeState::kTileHeight, deep);
        }
        // Corner sparkles to make coast transitions less boxy.
        if (!n && !w) PutPixel(state, x0, y0, PackColor(214, 232, 242));
        if (!n && !e) PutPixel(state, x0 + RuntimeState::kTileWidth - 1, y0, PackColor(214, 232, 242));
        if (!s && !w) PutPixel(state, x0, y0 + RuntimeState::kTileHeight - 1, PackColor(214, 232, 242));
        if (!s && !e) PutPixel(state, x0 + RuntimeState::kTileWidth - 1, y0 + RuntimeState::kTileHeight - 1, PackColor(214, 232, 242));
      }

      // Hide grid: blend terrain transitions + add subtle contact shadows (AO).
      if (!perf_critical || ((wx + wy) & 1) == 0) {
        ApplyTerrainEdgesAndAO(state, wx, wy, x0, y0, detail_variant);
      }

      const int cx = x0 + RuntimeState::kTileWidth / 2;
      const int cy = y0 + RuntimeState::kTileHeight / 2;

      if (tile.resource != ResourceType::None && tile.ore_units > 0) {
        const std::uint32_t ore_variant = detail_variant ^ 0x3Du;
        DrawResourceOverlayTile(state, tile.resource, x0, y0, ore_variant, tile.ore_units);
        DrawOreRocks(state, wx, wy, x0, y0);
      }

      if (tile.has_belt) {
        DrawBeltOverlay(state, x0, y0, tile.belt_dir);
      }
    }
  }

  // Pass 2: moving items on belts
  for (const auto& it : state.items) {
    DrawItemEntity(state, it);
  }

  // Pass 3: machines + trees
  for (int ty = 0; ty < draw_h; ++ty) {
    for (int tx = 0; tx < draw_w; ++tx) {
      const int wx = state.camera_x + tx;
      const int wy = state.camera_y + ty;
      if (wx < 0 || wy < 0 || wx >= state.world_w || wy >= state.world_h) continue;

      const int x0 = state.world_origin_x + tx * RuntimeState::kTileWidth;
      const int y0 = state.world_origin_y + ty * RuntimeState::kTileHeight;

      const int cx = x0 + RuntimeState::kTileWidth / 2;
      const int cy = y0 + RuntimeState::kTileHeight / 2;

      const auto& tile = state.tiles[TileIndex(state, wx, wy)];
      if (tile.has_tree) {
        DrawTree(state, cx, cy, tile.biome_region, tile.tree_variant);
      }

      // Machines render on top of trees if something slips through.
      if (Machine* machine = FindMachineAt(state, wx, wy); machine != nullptr) {
        DrawMachineBuilding(state, cx, cy, *machine);
      }


    }
  }

  // Player highlight (tile under feet) + glyph (smooth position)
  {
    const int tx = state.player_x - state.camera_x;
    const int ty = state.player_y - state.camera_y;
    const int hx0 = state.world_origin_x + tx * RuntimeState::kTileWidth;
    const int hy0 = state.world_origin_y + ty * RuntimeState::kTileHeight;

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 242, 190));
    HGDIOBJ old_pen = SelectObject(state.back_dc, pen);
    HGDIOBJ old_brush = SelectObject(state.back_dc, GetStockObject(NULL_BRUSH));
    Rectangle(state.back_dc, hx0 + 1, hy0 + 1, hx0 + RuntimeState::kTileWidth - 1, hy0 + RuntimeState::kTileHeight - 1);
    SelectObject(state.back_dc, old_brush);
    SelectObject(state.back_dc, old_pen);
    DeleteObject(pen);

    const float px = static_cast<float>(state.world_origin_x) + (state.player_fx - static_cast<float>(state.camera_x)) * static_cast<float>(RuntimeState::kTileWidth);
    const float py = static_cast<float>(state.world_origin_y) + (state.player_fy - static_cast<float>(state.camera_y)) * static_cast<float>(RuntimeState::kTileHeight);
    DrawPlayerGlyph(state, static_cast<int>(std::lround(px)), static_cast<int>(std::lround(py)));
  }

  RestoreDC(state.back_dc, saved_dc);
}
}  // namespace

bool TryLoadExternalTextureAtlas(RuntimeState& state) {
  std::filesystem::path exe_dir;
#ifdef _WIN32
  char exe_path[MAX_PATH]{};
  const DWORD len = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
  if (len > 0) exe_dir = std::filesystem::path(std::string(exe_path, exe_path + len)).parent_path();
#endif
  const std::filesystem::path cwd = std::filesystem::current_path();
  std::vector<std::filesystem::path> candidates = {
      cwd / "assets" / "generated" / "runtime_texture_atlas.bin",
      cwd / ".." / "assets" / "generated" / "runtime_texture_atlas.bin",
      cwd / ".." / ".." / "assets" / "generated" / "runtime_texture_atlas.bin",
  };
  if (!exe_dir.empty()) {
    // exe is in runtime/, project root is runtime/../, so assets are at runtime/../../assets
    candidates.push_back(exe_dir / ".." / ".." / "assets" / "generated" / "runtime_texture_atlas.bin");
    candidates.push_back(exe_dir / ".." / "assets" / "generated" / "runtime_texture_atlas.bin");
    candidates.push_back(exe_dir / "assets" / "generated" / "runtime_texture_atlas.bin");
    candidates.push_back(exe_dir / "runtime_texture_atlas.bin");
  }
  for (const auto& path : candidates) {
    if (LoadTextureAtlasFromFile(path, state)) {
      OutputDebugStringA(("✓ Loaded atlas: " + path.string() + "\n").c_str());
      return true;
    }
  }
  OutputDebugStringA("✗ Atlas not found in any candidate path\n");
  state.external_textures_loaded = false;
  state.texture_source = "procedural-fallback";
  return false;
}

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
  bmi.bmiHeader.biWidth = state.back_w;
  bmi.bmiHeader.biHeight = -state.back_h;
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

  // Compute desired render/backbuffer size.
  if (state.base_back_w <= 0 || state.base_back_h <= 0) {
    state.base_back_w = w;
    state.base_back_h = h;
  }
  const int desired_back_w = state.fixed_backbuffer ? std::min(state.base_back_w, w) : w;
  const int desired_back_h = state.fixed_backbuffer ? std::min(state.base_back_h, h) : h;

  const bool client_same = (w == state.client_w && h == state.client_h);
  const bool back_same = (desired_back_w == state.back_w && desired_back_h == state.back_h);
  const bool backbuffer_ready = (state.back_dc != nullptr && state.back_bitmap != nullptr && state.back_pixels != nullptr);
  if (client_same && back_same && backbuffer_ready) return true;

  state.client_w = w;
  state.client_h = h;
  state.back_w = desired_back_w;
  state.back_h = desired_back_h;

  const int world_w_px = state.back_w - RuntimeState::kHudWidth;
  state.viewport_tiles_w = std::max(8, world_w_px / RuntimeState::kTileWidth);
  state.viewport_tiles_h = std::max(8, state.back_h / RuntimeState::kTileHeight);
  return InitBackBuffer(state);
}

void Render(RuntimeState& state) {
  DrawSkyGradient(state);
  RenderWorld(state);

  if (state.snapshot.pollution > 0.30f) {
    const float haze = std::clamp((state.snapshot.pollution - 0.30f) * 1.2f, 0.0f, 0.5f);
    const std::uint32_t haze_color = PackColor(62, 52, 44);
    const std::uint8_t haze_alpha = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(haze * 255.0f)), 0, 255));
    const bool perf_critical = (state.fps > 0.0f && state.fps < 32.0f);
    for (int y = 0; y < state.back_h; ++y) {
      std::uint32_t* row = state.back_pixels + static_cast<std::size_t>(y * state.back_w);
      if (perf_critical && (y & 1) == 1) continue;
      for (int x = 0; x < state.back_w - RuntimeState::kHudWidth; ++x) {
        if (perf_critical && (x & 1) == 1) continue;
        row[x] = LerpColorFast(row[x], haze_color, haze_alpha);
      }
    }
  }

  DrawHud(state);

  HDC window_dc = GetDC(state.hwnd);
  if (window_dc == nullptr) return;

  // Fast path: 1:1 present.
  if (state.back_w == state.client_w && state.back_h == state.client_h) {
    BitBlt(window_dc, 0, 0, state.client_w, state.client_h, state.back_dc, 0, 0, SRCCOPY);
    ReleaseDC(state.hwnd, window_dc);
    return;
  }

  // Fixed render resolution should scale with an integer factor to stay crisp.
  const float sx = static_cast<float>(state.client_w) / static_cast<float>(state.back_w);
  const float sy = static_cast<float>(state.client_h) / static_cast<float>(state.back_h);
  int scale_i = 1;
  if (state.fixed_backbuffer) {
    scale_i = std::max(1, static_cast<int>(std::floor(std::min(sx, sy) + 1e-4f)));
  }
  const float s = state.fixed_backbuffer ? static_cast<float>(scale_i) : std::min(sx, sy);

  const int out_w = std::max(1, static_cast<int>(std::lround(static_cast<float>(state.back_w) * s)));
  const int out_h = std::max(1, static_cast<int>(std::lround(static_cast<float>(state.back_h) * s)));
  const int out_x = (state.client_w - out_w) / 2;
  const int out_y = (state.client_h - out_h) / 2;

  // Clear only the letterbox areas to avoid visible flashes.
  const HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if (out_y > 0) {
    RECT r{0, 0, state.client_w, out_y};
    FillRect(window_dc, &r, black);
  }
  if (out_y + out_h < state.client_h) {
    RECT r{0, out_y + out_h, state.client_w, state.client_h};
    FillRect(window_dc, &r, black);
  }
  if (out_x > 0) {
    RECT r{0, out_y, out_x, out_y + out_h};
    FillRect(window_dc, &r, black);
  }
  if (out_x + out_w < state.client_w) {
    RECT r{out_x + out_w, out_y, state.client_w, out_y + out_h};
    FillRect(window_dc, &r, black);
  }

  SetStretchBltMode(window_dc, COLORONCOLOR);
  StretchBlt(window_dc, out_x, out_y, out_w, out_h, state.back_dc, 0, 0, state.back_w, state.back_h, SRCCOPY);
  ReleaseDC(state.hwnd, window_dc);
}
