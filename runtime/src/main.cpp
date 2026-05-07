#include <chrono>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace term {
  constexpr const char* kReset = "\x1b[0m";
  constexpr const char* kBold = "\x1b[1m";
  constexpr const char* kGreen = "\x1b[32m";
  constexpr const char* kYellow = "\x1b[33m";
  constexpr const char* kRed = "\x1b[31m";
  constexpr const char* kCyan = "\x1b[36m";
  constexpr const char* kMagenta = "\x1b[35m";
  constexpr const char* kWhite = "\x1b[37m";
}

#ifdef _WIN32
void EnableAnsiColors() {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == INVALID_HANDLE_VALUE) return;
  DWORD mode = 0;
  if (!GetConsoleMode(out, &mode)) return;
  SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#endif

struct SimSnapshot {
  float stability;
  float pollution;
  float wage_index;
  float tax_rate;
};

struct PlanetSummary {
  std::uint32_t seed;
  std::uint32_t core_profile_id;
  std::uint32_t width;
  std::uint32_t height;
  float avg_height;
  std::uint32_t lowland_tiles;
  std::uint32_t midland_tiles;
  std::uint32_t highland_tiles;
  std::uint32_t water_tiles;
  std::uint32_t mountain_tiles;
  std::uint32_t iron_tiles;
  std::uint32_t copper_tiles;
  std::uint32_t coal_tiles;
  std::uint32_t unique_sites;
};

struct SystemSummary {
  std::uint32_t seed;
  std::uint32_t planet_count;
  std::uint32_t core_planets;
  std::uint32_t procedural_planets;
  float avg_height_across_planets;
  std::uint32_t total_unique_sites;
};

#ifdef _WIN32
using SimBootstrapFn = SimSnapshot (*)();
using SimTickFn = SimSnapshot (*)(float);
using SimSetPolicyFn = SimSnapshot (*)(float, float);
using SimGetSnapshotFn = SimSnapshot (*)();
using SimGeneratePlanetFn = PlanetSummary (*)(std::uint32_t, std::uint32_t, std::uint32_t);
using SimGeneratePlanetFromCoreFn = PlanetSummary (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
using SimGenerateSystemFn = SystemSummary (*)(std::uint32_t);
#endif

int main() {
#ifdef _WIN32
  EnableAnsiColors();
  HMODULE sim_lib = LoadLibraryA("factorio_pt_sim.dll");
  if (sim_lib == nullptr) {
    std::cout << term::kRed << "[ERROR]" << term::kReset << " Rust sim library not found.\n";
    return 1;
  }

  auto* sim_bootstrap = reinterpret_cast<SimBootstrapFn>(GetProcAddress(sim_lib, "sim_bootstrap"));
  auto* sim_tick = reinterpret_cast<SimTickFn>(GetProcAddress(sim_lib, "sim_tick"));
  auto* sim_set_policy = reinterpret_cast<SimSetPolicyFn>(GetProcAddress(sim_lib, "sim_set_policy"));
  auto* sim_get_snapshot = reinterpret_cast<SimGetSnapshotFn>(GetProcAddress(sim_lib, "sim_get_snapshot"));
  auto* sim_generate_planet =
      reinterpret_cast<SimGeneratePlanetFn>(GetProcAddress(sim_lib, "sim_generate_planet"));
  auto* sim_generate_planet_from_core =
      reinterpret_cast<SimGeneratePlanetFromCoreFn>(GetProcAddress(sim_lib, "sim_generate_planet_from_core"));
  auto* sim_generate_system = reinterpret_cast<SimGenerateSystemFn>(GetProcAddress(sim_lib, "sim_generate_system"));

  if (sim_bootstrap == nullptr || sim_tick == nullptr || sim_set_policy == nullptr || sim_get_snapshot == nullptr ||
      sim_generate_planet == nullptr || sim_generate_planet_from_core == nullptr || sim_generate_system == nullptr) {
    std::cout << term::kRed << "[ERROR]" << term::kReset << " Symbol lookup failed.\n";
    FreeLibrary(sim_lib);
    return 1;
  }

  std::cout << term::kCyan << term::kBold << "[BOOT]" << term::kReset << " Factorio-pt runtime booted.\n";
  std::cout << term::kWhite << "[INFO]" << term::kReset << " Target: 2.5D runtime + Rust simulation + C# tools.\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  const SystemSummary system = sim_generate_system(2026u);
  std::cout << term::kMagenta << "[SYSTEM]" << term::kReset << " Seed " << system.seed << " -> planets: " 
            << term::kGreen << system.planet_count << term::kReset << ", core: " << system.core_planets 
            << ", avg_height: " << system.avg_height_across_planets << ", unique_sites: " << system.total_unique_sites << "\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const PlanetSummary planet = sim_generate_planet(1337u, 256u, 256u);
  std::cout << term::kMagenta << "[PLANET]" << term::kReset << " Seed " << planet.seed << " [core " << planet.core_profile_id 
            << "] (" << planet.width << "x" << planet.height << ") -> avg_height: " << planet.avg_height << "\n";
  std::cout << term::kYellow << "[TERRAIN]" << term::kReset << " low: " << planet.lowland_tiles << ", mid: " << planet.midland_tiles
            << ", high: " << planet.highland_tiles << ", water: " << planet.water_tiles
            << ", mountain: " << planet.mountain_tiles << ", unique_sites: " << planet.unique_sites << "\n";
  std::cout << term::kYellow << "[RESOURCES]" << term::kReset << " iron: " << term::kGreen << planet.iron_tiles << term::kReset 
            << ", copper: " << planet.copper_tiles << ", coal: " << planet.coal_tiles << "\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const SimSnapshot start_snapshot = sim_bootstrap();
  std::cout << term::kCyan << "[SIM]" << term::kReset << " stability: " << start_snapshot.stability 
            << ", pollution: " << start_snapshot.pollution << ", wage: " << start_snapshot.wage_index 
            << ", tax: " << start_snapshot.tax_rate << "\n";

  const SimSnapshot policy_snapshot = sim_set_policy(0.75f, 0.25f);
  std::cout << term::kGreen << "[POLICY_0]" << term::kReset << " wage: " << policy_snapshot.wage_index 
            << ", tax: " << policy_snapshot.tax_rate << "\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  constexpr int tick_count = 20;
  constexpr float delta_seconds = 0.5f;
  for (int tick = 1; tick <= tick_count; ++tick) {
    if (tick == 5) {
      const SimSnapshot changed_policy = sim_set_policy(0.50f, 0.15f);
      std::cout << term::kYellow << "[POLICY_CHANGE_1]" << term::kReset << " wage: " << changed_policy.wage_index 
                << ", tax: " << changed_policy.tax_rate << "\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (tick == 12) {
      const SimSnapshot changed_policy = sim_set_policy(0.60f, 0.35f);
      std::cout << term::kYellow << "[POLICY_CHANGE_2]" << term::kReset << " wage: " << changed_policy.wage_index 
                << ", tax: " << changed_policy.tax_rate << "\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const SimSnapshot snapshot = sim_tick(delta_seconds);
    
    const char* stab_color = snapshot.stability > 0.70f ? term::kGreen : (snapshot.stability > 0.50f ? term::kYellow : term::kRed);
    const char* poll_color = snapshot.pollution < 0.20f ? term::kGreen : (snapshot.pollution < 0.40f ? term::kYellow : term::kRed);
    
    std::cout << term::kCyan << "[T" << tick << "]" << term::kReset 
              << " stab:" << stab_color << snapshot.stability << term::kReset 
              << " poll:" << poll_color << snapshot.pollution << term::kReset
              << " wage:" << snapshot.wage_index 
              << " tax:" << snapshot.tax_rate << "\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  const PlanetSummary world = sim_generate_planet(4242u, 64u, 64u);
  
  std::cout << term::kCyan << term::kBold << ">>> Procedural World Generated <<<" << term::kReset << "\n";
  std::cout << term::kWhite << "Seed: " << world.seed << " | Size: " << world.width << "x" << world.height 
            << " | Height avg: " << world.avg_height << "\n" << term::kReset;
  std::cout << term::kYellow << "Terrain - Low:" << world.lowland_tiles << " Mid:" << world.midland_tiles 
            << " High:" << world.highland_tiles << " Water:" << world.water_tiles << " Mountain:" << world.mountain_tiles << "\n" << term::kReset;
  std::cout << term::kGreen << "Resources - Iron:" << world.iron_tiles << " Copper:" << world.copper_tiles 
            << " Coal:" << world.coal_tiles << "\n" << term::kReset;

  std::cout << term::kCyan << term::kBold << ">>> Starting Game Loop <<<" << term::kReset << "\n";
  std::cout << term::kWhite << "Controls: W/A/S/D move, Space interact, Q quit | Arrow keys: pan camera\n" << term::kReset;

  const int grid_w = 16;
  const int grid_h = 12;
  int player_x = 8, player_y = 6;
  int camera_x = 0, camera_y = 0;
  int game_tick = 0;
  bool running = true;

  while (running && game_tick < 200) {
#ifdef _WIN32
    std::system("cls");
#else
    std::system("clear");
#endif
    
    const SimSnapshot snapshot = sim_tick(0.5f);
    
    std::cout << term::kCyan << term::kBold << "╔════════════════════════════════════════════════╗" << term::kReset << "\n";
    std::cout << term::kCyan << term::kBold << "║ Factorio-pt World - Tick " << std::setw(3) << game_tick << " │ Pos (" << std::setw(2) << player_x << "," 
              << std::setw(2) << player_y << ") │ Cam (" << std::setw(2) << camera_x << "," << std::setw(2) << camera_y << ") ║" << term::kReset << "\n";
    std::cout << term::kCyan << term::kBold << "║ Stability:" << term::kReset;
    
    float stab = snapshot.stability;
    std::cout << (stab > 0.7 ? term::kGreen : (stab > 0.5 ? term::kYellow : term::kRed));
    std::cout << std::fixed << std::setprecision(2) << stab << term::kReset << " │ Pollution:" 
              << (snapshot.pollution < 0.2 ? term::kGreen : (snapshot.pollution < 0.4 ? term::kYellow : term::kRed))
              << std::setprecision(2) << snapshot.pollution << term::kReset 
              << " │ Wage:" << std::setprecision(2) << snapshot.wage_index << " ║\n";
    std::cout << term::kCyan << term::kBold << "╠════════════════════════════════════════════════╣" << term::kReset << "\n";

    for (int y = 0; y < grid_h; ++y) {
      for (int x = 0; x < grid_w; ++x) {
        int world_x = camera_x + x;
        int world_y = camera_y + y;
        
        if (world_x < 0 || world_y < 0 || world_x >= (int)world.width || world_y >= (int)world.height) {
          std::cout << term::kMagenta << "# " << term::kReset;
          continue;
        }

        if (x == (player_x - camera_x) && y == (player_y - camera_y) && camera_x <= player_x && player_x < camera_x + grid_w && 
            camera_y <= player_y && player_y < camera_y + grid_h) {
          std::cout << term::kBold << term::kWhite << "@ " << term::kReset;
        } else {
          uint32_t tile_hash = (world_x * 73856093 ^ world_y * 19349663) % 100;
          
          char tile_char = '.';
          const char* tile_color = term::kWhite;

          if (tile_hash < world.water_tiles * 100 / (world.width * world.height)) {
            tile_char = '~';
            tile_color = term::kCyan;
          } else if (tile_hash < (world.water_tiles + world.mountain_tiles) * 100 / (world.width * world.height)) {
            tile_char = '^';
            tile_color = term::kRed;
          } else if (tile_hash < (world.water_tiles + world.mountain_tiles + world.highland_tiles) * 100 / (world.width * world.height)) {
            tile_char = '+';
            tile_color = term::kYellow;
          } else if (tile_hash < (world.water_tiles + world.mountain_tiles + world.highland_tiles + world.midland_tiles) * 100 / (world.width * world.height)) {
            tile_char = '=';
            tile_color = term::kGreen;
          } else {
            tile_char = '.';
            tile_color = term::kWhite;
          }

          uint32_t res_hash = (world_x * 19349663 ^ world_y * 73856093) % 100;
          if (res_hash < world.iron_tiles * 100 / (world.width * world.height)) {
            tile_char = '#';
            tile_color = term::kYellow;
          } else if (res_hash < (world.iron_tiles + world.copper_tiles) * 100 / (world.width * world.height)) {
            tile_char = 'C';
            tile_color = term::kCyan;
          } else if (res_hash < (world.iron_tiles + world.copper_tiles + world.coal_tiles) * 100 / (world.width * world.height)) {
            tile_char = '*';
            tile_color = term::kRed;
          }

          std::cout << tile_color << tile_char << " " << term::kReset;
        }
      }
      std::cout << "\n";
    }

    std::cout << "\n" << term::kMagenta << "Input (W/A/S/D move, IJKL pan, Space interact, Q quit): " << term::kReset;
    char input;
    std::cin >> input;

    switch (input) {
      case 'W': case 'w': if (player_y > 0) player_y--; break;
      case 'S': case 's': if (player_y < (int)world.height - 1) player_y++; break;
      case 'A': case 'a': if (player_x > 0) player_x--; break;
      case 'D': case 'd': if (player_x < (int)world.width - 1) player_x++; break;
      case 'I': case 'i': if (camera_y > 0) camera_y--; break;
      case 'K': case 'k': if (camera_y < (int)world.height - grid_h) camera_y++; break;
      case 'J': case 'j': if (camera_x > 0) camera_x--; break;
      case 'L': case 'l': if (camera_x < (int)world.width - grid_w) camera_x++; break;
      case ' ': std::cout << term::kGreen << "Interact at (" << player_x << "," << player_y << ")\n" << term::kReset; break;
      case 'Q': case 'q': running = false; break;
    }

    game_tick++;
  }

  std::cout << "\n" << term::kCyan << term::kBold << ">>> Game Loop End <<<" << term::kReset << "\n";
#else
  std::cout << term::kWhite << "[INFO]" << term::kReset << " Windows-only for now.\n";
#endif

  return 0;
}
