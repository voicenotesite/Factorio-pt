#include "gameplay.h"
#include "render.h"
#include "runtime_state.h"
#include "world.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#ifdef _WIN32
using SimBootstrapFn = SimSnapshot (*)();
using SimTickFn = SimSnapshot (*)(float);
using SimSetPolicyFn = SimSnapshot (*)(float, float);
using SimGeneratePlanetFn = PlanetSummary (*)(std::uint32_t, std::uint32_t, std::uint32_t);
using SimGenerateSystemFn = SystemSummary (*)(std::uint32_t);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
  if (msg == WM_DESTROY) {
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

int RunRuntimeWindow(SimTickFn sim_tick,
                     SimSetPolicyFn sim_set_policy,
                     SimGeneratePlanetFn sim_generate_planet,
                     SimGenerateSystemFn sim_generate_system) {
  RuntimeState state{};
  const std::uint32_t boot_seed = static_cast<std::uint32_t>(GetTickCount64());
  state.system = sim_generate_system(2026u);
  state.world = sim_generate_planet(boot_seed ^ 0x5A17u, 128u, 128u);
  state.run_seed = state.system.seed ^ state.world.seed ^ boot_seed;
  state.theme_shift = static_cast<float>((state.run_seed & 0x7Fu)) / 127.0f * 0.32f - 0.16f;
  state.world_w = static_cast<int>(state.world.width);
  state.world_h = static_cast<int>(state.world.height);
  state.player_x = state.world_w / 2;
  state.player_y = state.world_h / 2;

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
      "Factorio-pt Runtime // M8 Refactor",
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
  if (!InitBackBuffer(state)) {
    std::cout << "[ERROR] Backbuffer init failed.\n";
    return 1;
  }

  const int world_w_px = state.client_w - RuntimeState::kHudWidth;
  state.viewport_tiles_w = std::max(8, world_w_px / RuntimeState::kTileWidth);
  state.viewport_tiles_h = std::max(8, state.client_h / RuntimeState::kTileHeight);
  state.camera_x = std::clamp(state.player_x - state.viewport_tiles_w / 2, 0, std::max(0, state.world_w - state.viewport_tiles_w));
  state.camera_y = std::clamp(state.player_y - state.viewport_tiles_h / 2, 0, std::max(0, state.world_h - state.viewport_tiles_h));

  sim_set_policy(0.60f, 0.28f);
  state.snapshot = sim_tick(0.0f);
  GenerateWorld(state);

  bool prev_w = false, prev_a = false, prev_s = false, prev_d = false;
  bool prev_i = false, prev_j = false, prev_k = false, prev_l = false;
  bool prev_q = false, prev_r = false, prev_e = false, prev_f = false, prev_b = false;

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

    if (KeyEdge('W', prev_w)) state.player_y--;
    if (KeyEdge('S', prev_s)) state.player_y++;
    if (KeyEdge('A', prev_a)) state.player_x--;
    if (KeyEdge('D', prev_d)) state.player_x++;
    if (KeyEdge('I', prev_i)) state.camera_y--;
    if (KeyEdge('K', prev_k)) state.camera_y++;
    if (KeyEdge('J', prev_j)) state.camera_x--;
    if (KeyEdge('L', prev_l)) state.camera_x++;
    if (KeyEdge('E', prev_e)) TryMine(state);
    if (KeyEdge('F', prev_f)) TrySmelt(state);
    if (KeyEdge('B', prev_b)) TryToggleDrill(state);
    if (KeyEdge('R', prev_r)) ReseedWorldStyle(state);
    if (KeyEdge('Q', prev_q) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) state.running = false;

    state.player_x = std::clamp(state.player_x, 0, state.world_w - 1);
    state.player_y = std::clamp(state.player_y, 0, state.world_h - 1);
    state.camera_x = std::clamp(state.camera_x, 0, std::max(0, state.world_w - state.viewport_tiles_w));
    state.camera_y = std::clamp(state.camera_y, 0, std::max(0, state.world_h - state.viewport_tiles_h));

    constexpr float sim_dt = 1.0f / 60.0f;
    state.snapshot = sim_tick(sim_dt);
    UpdateMachines(state, sim_dt);
    state.day_time_s += sim_dt;
    if (state.status_timer_s > 0.0f) {
      state.status_timer_s = std::max(0.0f, state.status_timer_s - sim_dt);
      if (state.status_timer_s <= 0.0f) state.status_text.clear();
    }
    state.water_anim += 0.07f;
    Render(state);

    const auto now = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<float, std::milli> frame_dt = now - frame_begin;
    state.frame_time_ms = frame_dt.count();
    frame_begin = now;

    ++fps_frames;
    const std::chrono::duration<float> fps_window = now - fps_window_start;
    if (fps_window.count() >= 1.0f) {
      state.fps = static_cast<float>(fps_frames) / fps_window.count();
      fps_frames = 0;
      fps_window_start = now;
    }

    constexpr auto target_frame = std::chrono::milliseconds(16);
    const auto sleep_time = target_frame - std::chrono::duration_cast<std::chrono::milliseconds>(frame_dt);
    if (sleep_time.count() > 0) std::this_thread::sleep_for(sleep_time);
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
