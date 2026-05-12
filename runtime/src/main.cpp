#include "gameplay.h"
#include "render.h"
#include "runtime_state.h"
#include "world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#ifdef _WIN32
using SimBootstrapFn = SimSnapshot (*)();
using SimTickFn = SimSnapshot (*)(float);
using SimSetPolicyFn = SimSnapshot (*)(float, float);
using SimGeneratePlanetFn = PlanetSummary (*)(std::uint32_t, std::uint32_t, std::uint32_t);
using SimGenerateSystemFn = SystemSummary (*)(std::uint32_t);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
  switch (msg) {
    case WM_ERASEBKGND:
      // Prevent background erase flicker; we present our own backbuffer every frame.
      return 1;
    case WM_PAINT: {
      // Validate the update region; actual painting happens in the main loop.
      PAINTSTRUCT ps{};
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, w_param, l_param);
}

bool KeyEdge(int vk, bool& prev_state) {
  const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
  const bool edge = down && !prev_state;
  prev_state = down;
  return edge;
}

void EnsurePlayerOnLand(RuntimeState& state) {
  auto is_land = [&](int tx, int ty) {
    WorldTile* t = GetTile(state, tx, ty);
    return t != nullptr && t->terrain != TerrainType::Water;
  };

  if (is_land(state.player_x, state.player_y)) return;

  const int max_r = 48;
  int best_x = state.player_x;
  int best_y = state.player_y;
  int best_d = 1 << 30;

  for (int r = 1; r <= max_r; ++r) {
    for (int dy = -r; dy <= r; ++dy) {
      for (int dx = -r; dx <= r; ++dx) {
        if (std::abs(dx) != r && std::abs(dy) != r) continue;  // border only
        const int nx = state.player_x + dx;
        const int ny = state.player_y + dy;
        if (!is_land(nx, ny)) continue;
        const int d = std::abs(dx) + std::abs(dy);
        if (d < best_d) {
          best_d = d;
          best_x = nx;
          best_y = ny;
        }
      }
    }
    if (best_d != (1 << 30)) break;
  }

  state.player_x = std::clamp(best_x, 0, state.world_w - 1);
  state.player_y = std::clamp(best_y, 0, state.world_h - 1);
  state.player_fx = static_cast<float>(state.player_x) + 0.5f;
  state.player_fy = static_cast<float>(state.player_y) + 0.5f;
  SetStatus(state, "Spawn przeniesiony na lad (woda blokowala ruch).", 2.4f);
}

int RunRuntimeWindow(SimTickFn sim_tick,
                     SimSetPolicyFn sim_set_policy,
                     SimGeneratePlanetFn sim_generate_planet,
                     SimGenerateSystemFn sim_generate_system) {
  RuntimeState state{};
  const std::uint32_t boot_seed = static_cast<std::uint32_t>(GetTickCount64());
  state.system = sim_generate_system(2026u);
  state.world = sim_generate_planet(boot_seed ^ 0x5A17u, 512u, 512u);
  state.run_seed = state.system.seed ^ state.world.seed ^ boot_seed;
  state.theme_shift = static_cast<float>((state.run_seed & 0x7Fu)) / 127.0f * 0.32f - 0.16f;
  state.world_w = static_cast<int>(state.world.width);
  state.world_h = static_cast<int>(state.world.height);
  state.player_x = state.world_w / 2;
  state.player_y = state.world_h / 2;
  state.player_fx = static_cast<float>(state.player_x) + 0.5f;
  state.player_fy = static_cast<float>(state.player_y) + 0.5f;

  WNDCLASSA wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = "FactorioPtWindowClass";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (!RegisterClassA(&wc)) {
    std::cout << "[ERROR] RegisterClass failed.\n";
    return 1;
  }

  RECT desired{0, 0, state.client_w, state.client_h};
  AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
  state.hwnd = CreateWindowExA(
      0,
      wc.lpszClassName,
      "Orbitum — by Sh1t3ad Dev",
      WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      desired.right - desired.left,
      desired.bottom - desired.top,
      nullptr,
      nullptr,
      wc.hInstance,
      nullptr);

  if (state.hwnd == nullptr) {
    std::cout << "[ERROR] CreateWindowEx failed.\n";
    return 1;
  }
  if (!HandleResize(state)) {
    std::cout << "[ERROR] Backbuffer init failed.\n";
    return 1;
  }
  // Remember initial render size as the default cap for fullscreen.
  state.base_back_w = state.back_w;
  state.base_back_h = state.back_h;
  state.camera_fx = std::clamp(state.player_fx - static_cast<float>(state.viewport_tiles_w) * 0.5f, 0.0f,
                               std::max(0.0f, static_cast<float>(state.world_w - state.viewport_tiles_w)));
  state.camera_fy = std::clamp(state.player_fy - static_cast<float>(state.viewport_tiles_h) * 0.5f, 0.0f,
                               std::max(0.0f, static_cast<float>(state.world_h - state.viewport_tiles_h)));
  state.camera_x = static_cast<int>(std::floor(state.camera_fx));
  state.camera_y = static_cast<int>(std::floor(state.camera_fy));
  state.camera_sub_x = state.camera_fx - static_cast<float>(state.camera_x);
  state.camera_sub_y = state.camera_fy - static_cast<float>(state.camera_y);

  sim_set_policy(0.60f, 0.28f);
  state.snapshot = sim_tick(0.0f);
  GenerateWorld(state);
  EnsurePlayerOnLand(state);
  SetStatus(state, "Swiezy generator swiata uruchomiony.", 2.5f);
  InitializeTechnology(state);
  UpdateTechnologyUnlocks(state);

  bool prev_q = false, prev_r = false, prev_p = false, prev_c = false, prev_h = false, prev_e = false, prev_f = false, prev_b = false, prev_n = false, prev_t = false;
  bool prev_f8 = false;
  bool prev_f7 = false;
  int low_fps_windows = 0;
  int high_fps_windows = 0;

  auto frame_begin = std::chrono::high_resolution_clock::now();
  auto fps_window_start = frame_begin;
  std::uint32_t fps_frames = 0;
  MSG msg{};

  while (state.running) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) state.running = false;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    if (!state.running) break;

    if (!HandleResize(state)) {
      state.running = false;
      break;
    }

    if (!state.fixed_backbuffer && !state.fixed_backbuffer_hint_shown) {
      const int client_px = state.client_w * state.client_h;
      const int base_px = std::max(1, state.base_back_w * state.base_back_h);
      if (client_px > base_px * 2) {
        state.fixed_backbuffer_hint_shown = true;
        SetStatus(state, "Tip: F8 = fixed render resolution (duzo wiecej FPS)", 3.5f);
      }
    }

    auto key_down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };

    const auto frame_now = std::chrono::high_resolution_clock::now();
    float sim_dt = std::chrono::duration<float>(frame_now - frame_begin).count();
    // Clamp dt to keep sim stable when FPS tanks.
    sim_dt = std::clamp(sim_dt, 0.0f, 0.05f);
    frame_begin = frame_now;
    const bool w_down = key_down('W');
    const bool a_down = key_down('A');
    const bool s_down = key_down('S');
    const bool d_down = key_down('D');
    const bool shift_down = key_down(VK_SHIFT);

    float move_x = (d_down ? 1.0f : 0.0f) - (a_down ? 1.0f : 0.0f);
    float move_y = (s_down ? 1.0f : 0.0f) - (w_down ? 1.0f : 0.0f);
    const float len = std::sqrt(move_x * move_x + move_y * move_y);
    if (len > 0.001f) {
      move_x /= len;
      move_y /= len;
    }

    const float base_speed = 7.8f;  // tiles per second
    const float speed = base_speed * (shift_down ? 1.9f : 1.0f);

    auto tile_walkable = [&](int tx, int ty) {
      if (tx < 0 || ty < 0 || tx >= state.world_w || ty >= state.world_h) return false;
      const auto& t = state.tiles[TileIndex(state, tx, ty)];
      return t.terrain != TerrainType::Water;
    };

    // Move with simple water collision (axis-separated).
    if (std::abs(move_x) > 0.001f || std::abs(move_y) > 0.001f) {
      float next_fx = state.player_fx + move_x * speed * sim_dt;
      float next_fy = state.player_fy + move_y * speed * sim_dt;

      next_fx = std::clamp(next_fx, 0.001f, static_cast<float>(state.world_w) - 0.001f);
      next_fy = std::clamp(next_fy, 0.001f, static_cast<float>(state.world_h) - 0.001f);

      const int cur_tx = static_cast<int>(std::floor(state.player_fx));
      const int cur_ty = static_cast<int>(std::floor(state.player_fy));
      const int nx_tx = static_cast<int>(std::floor(next_fx));
      const int ny_ty = static_cast<int>(std::floor(next_fy));

      if (tile_walkable(nx_tx, cur_ty)) state.player_fx = next_fx;
      if (tile_walkable(cur_tx, ny_ty)) state.player_fy = next_fy;
    }

    state.player_x = std::clamp(static_cast<int>(std::floor(state.player_fx)), 0, state.world_w - 1);
    state.player_y = std::clamp(static_cast<int>(std::floor(state.player_fy)), 0, state.world_h - 1);

    // Camera: smooth follow with sub-tile offset.
    const float target_cx = std::clamp(state.player_fx - static_cast<float>(state.viewport_tiles_w) * 0.5f, 0.0f,
                                       std::max(0.0f, static_cast<float>(state.world_w - state.viewport_tiles_w)));
    const float target_cy = std::clamp(state.player_fy - static_cast<float>(state.viewport_tiles_h) * 0.5f, 0.0f,
                                       std::max(0.0f, static_cast<float>(state.world_h - state.viewport_tiles_h)));
    const float follow = 0.18f;
    state.camera_fx += (target_cx - state.camera_fx) * follow;
    state.camera_fy += (target_cy - state.camera_fy) * follow;

    state.camera_x = std::clamp(static_cast<int>(std::floor(state.camera_fx)), 0, std::max(0, state.world_w - state.viewport_tiles_w));
    state.camera_y = std::clamp(static_cast<int>(std::floor(state.camera_fy)), 0, std::max(0, state.world_h - state.viewport_tiles_h));
    state.camera_sub_x = state.camera_fx - static_cast<float>(state.camera_x);
    state.camera_sub_y = state.camera_fy - static_cast<float>(state.camera_y);

    if (KeyEdge('E', prev_e)) TryMine(state);
    if (KeyEdge('F', prev_f)) TrySmelt(state);
    if (KeyEdge('B', prev_b)) TryToggleDrill(state);
    if (KeyEdge('N', prev_n)) TryToggleFurnace(state);
    if (KeyEdge('T', prev_t)) TryToggleBelt(state);
    if (KeyEdge('C', prev_c)) TryChopTree(state);
    if (KeyEdge('R', prev_r)) RotateBuildDir(state);
    if (KeyEdge('P', prev_p)) {
      ReseedWorldStyle(state);
      EnsurePlayerOnLand(state);
    }
    if (KeyEdge(VK_F8, prev_f8)) {
      state.fixed_backbuffer = !state.fixed_backbuffer;
      state.auto_perf_governor = false;
      low_fps_windows = 0;
      high_fps_windows = 0;
      SetStatus(state,
                state.fixed_backbuffer ? "Render: fixed (F8), auto governor OFF" : "Render: native (F8), auto governor OFF",
                2.4f);
      HandleResize(state);
    }
    if (KeyEdge(VK_F7, prev_f7)) {
      state.auto_perf_governor = !state.auto_perf_governor;
      low_fps_windows = 0;
      high_fps_windows = 0;
      SetStatus(state, state.auto_perf_governor ? "Auto governor ON (F7)" : "Auto governor OFF (F7)", 2.2f);
    }
    if (KeyEdge('H', prev_h)) state.hud_mode = (state.hud_mode + 1) % 3;
    if (KeyEdge('Q', prev_q) || key_down(VK_ESCAPE)) state.running = false;

    state.snapshot = sim_tick(sim_dt);
    UpdateMachines(state, sim_dt);
    UpdateItems(state, sim_dt);
    UpdateTechnologyUnlocks(state);
    state.day_time_s += sim_dt;
    if (state.status_timer_s > 0.0f) {
      state.status_timer_s = std::max(0.0f, state.status_timer_s - sim_dt);
      if (state.status_timer_s <= 0.0f) state.status_text.clear();
    }
    state.water_anim += 4.2f * sim_dt;

    Render(state);

    state.frame_time_ms = sim_dt * 1000.0f;

    ++fps_frames;
    const std::chrono::duration<float> fps_window = frame_now - fps_window_start;
    if (fps_window.count() >= 1.0f) {
      state.fps = static_cast<float>(fps_frames) / fps_window.count();
      fps_frames = 0;
      fps_window_start = frame_now;

      if (state.auto_perf_governor) {
        if (state.fps < 45.0f) {
          low_fps_windows++;
          high_fps_windows = 0;
        } else if (state.fps > 58.0f) {
          high_fps_windows++;
          low_fps_windows = 0;
        } else {
          low_fps_windows = 0;
          high_fps_windows = 0;
        }

        if (!state.fixed_backbuffer && low_fps_windows >= 2) {
          state.fixed_backbuffer = true;
          low_fps_windows = 0;
          high_fps_windows = 0;
          SetStatus(state, "Auto governor: fixed resolution ON (FPS rescue)", 2.5f);
          HandleResize(state);
        } else if (state.fixed_backbuffer && high_fps_windows >= 4) {
          state.fixed_backbuffer = false;
          low_fps_windows = 0;
          high_fps_windows = 0;
          SetStatus(state, "Auto governor: native resolution restored", 2.5f);
          HandleResize(state);
        }
      }
    }

    // Attempt to cap at ~60 FPS when there's headroom.
    const auto after_render = std::chrono::high_resolution_clock::now();
    const auto work_ms = std::chrono::duration_cast<std::chrono::milliseconds>(after_render - frame_now);
    constexpr auto target_frame = std::chrono::milliseconds(16);
    if (work_ms < target_frame) std::this_thread::sleep_for(target_frame - work_ms);

    state.frame_counter++;
  }

  if (state.back_bitmap != nullptr) DeleteObject(state.back_bitmap);
  if (state.back_dc != nullptr) DeleteDC(state.back_dc);
  DestroyWindow(state.hwnd);
  return 0;
}
#endif

int main() {
#ifdef _WIN32
  HMODULE sim_lib = LoadLibraryA("factorio_pt_sim.dll");
  if (sim_lib == nullptr) {
    std::cout << "[ERROR] Rust sim library not found (factorio_pt_sim.dll).\n";
    return 1;
  }

  auto* sim_bootstrap = reinterpret_cast<SimBootstrapFn>(GetProcAddress(sim_lib, "sim_bootstrap"));
  auto* sim_tick = reinterpret_cast<SimTickFn>(GetProcAddress(sim_lib, "sim_tick"));
  auto* sim_set_policy = reinterpret_cast<SimSetPolicyFn>(GetProcAddress(sim_lib, "sim_set_policy"));
  auto* sim_generate_planet = reinterpret_cast<SimGeneratePlanetFn>(GetProcAddress(sim_lib, "sim_generate_planet"));
  auto* sim_generate_system = reinterpret_cast<SimGenerateSystemFn>(GetProcAddress(sim_lib, "sim_generate_system"));

  if (sim_bootstrap == nullptr || sim_tick == nullptr || sim_set_policy == nullptr || sim_generate_planet == nullptr ||
      sim_generate_system == nullptr) {
    std::cout << "[ERROR] Missing symbols in factorio_pt_sim.dll.\n";
    FreeLibrary(sim_lib);
    return 1;
  }

  const SimSnapshot boot = sim_bootstrap();
  std::cout << "[BOOT] Runtime modularized.\n";
  std::cout << "[SIM] stability=" << boot.stability << " pollution=" << boot.pollution << "\n";

  const int code = RunRuntimeWindow(sim_tick, sim_set_policy, sim_generate_planet, sim_generate_system);
  FreeLibrary(sim_lib);
  return code;
#else
  std::cout << "[INFO] Window runtime is currently Windows-only.\n";
  return 0;
#endif
}
