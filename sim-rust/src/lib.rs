use std::sync::{Mutex, OnceLock};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SimSnapshot {
    pub stability: f32,
    pub pollution: f32,
    pub wage_index: f32,
    pub tax_rate: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PlanetSummary {
    pub seed: u32,
    pub core_profile_id: u32,
    pub width: u32,
    pub height: u32,
    pub avg_height: f32,
    pub lowland_tiles: u32,
    pub midland_tiles: u32,
    pub highland_tiles: u32,
    pub water_tiles: u32,
    pub mountain_tiles: u32,
    pub iron_tiles: u32,
    pub copper_tiles: u32,
    pub coal_tiles: u32,
    pub unique_sites: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SystemSummary {
    pub seed: u32,
    pub planet_count: u32,
    pub core_planets: u32,
    pub procedural_planets: u32,
    pub avg_height_across_planets: f32,
    pub total_unique_sites: u32,
}

#[derive(Clone, Copy)]
struct SimState {
    stability: f32,
    pollution: f32,
    wage_index: f32,
    tax_rate: f32,
}

impl SimState {
    fn bootstrap() -> Self {
        Self {
            stability: 0.75,
            pollution: 0.10,
            wage_index: 0.50,
            tax_rate: 0.20,
        }
    }

    fn tick(&mut self, delta_seconds: f32) {
        let dt = delta_seconds.clamp(0.0, 10.0);
        let production_intensity = 0.60 + (1.0 - self.tax_rate) * 0.60 + self.wage_index * 0.20;
        let industrial_pressure = 0.018 * dt * production_intensity;
        let mitigation = 0.007 * dt + self.tax_rate * 0.020 * dt;
        self.pollution = (self.pollution + industrial_pressure - mitigation).clamp(0.0, 1.0);

        let pollution_penalty = self.pollution * 0.040 * dt;
        let wage_bonus = (self.wage_index - 0.50) * 0.040 * dt;
        let tax_penalty = self.tax_rate * 0.020 * dt;
        let civic_recovery = 0.005 * dt;
        self.stability =
            (self.stability + civic_recovery + wage_bonus - tax_penalty - pollution_penalty).clamp(0.0, 1.0);
    }

    fn set_policy(&mut self, wage_index: f32, tax_rate: f32) {
        self.wage_index = wage_index.clamp(0.0, 1.0);
        self.tax_rate = tax_rate.clamp(0.0, 1.0);
    }

    fn snapshot(self) -> SimSnapshot {
        SimSnapshot {
            stability: self.stability,
            pollution: self.pollution,
            wage_index: self.wage_index,
            tax_rate: self.tax_rate,
        }
    }
}

static SIM_STATE: OnceLock<Mutex<SimState>> = OnceLock::new();

fn sim_state() -> &'static Mutex<SimState> {
    SIM_STATE.get_or_init(|| Mutex::new(SimState::bootstrap()))
}

fn hash32(mut value: u32) -> u32 {
    value ^= value >> 16;
    value = value.wrapping_mul(0x7FEB_352D);
    value ^= value >> 15;
    value = value.wrapping_mul(0x846C_A68B);
    value ^= value >> 16;
    value
}

fn core_profile_bias(core_profile_id: u32) -> (f32, f32, f32, f32, u32) {
    match core_profile_id % 10 {
        0 => (0.10, -0.08, 0.10, -0.02, 2),
        1 => (-0.08, 0.10, -0.02, 0.08, 2),
        2 => (0.18, -0.10, 0.12, -0.04, 3),
        3 => (-0.12, 0.12, -0.06, 0.14, 2),
        4 => (0.00, 0.00, 0.04, 0.04, 1),
        5 => (0.06, -0.02, 0.08, 0.00, 1),
        6 => (-0.04, 0.08, -0.04, 0.10, 2),
        7 => (0.14, -0.06, 0.14, -0.08, 3),
        8 => (-0.10, 0.14, -0.08, 0.12, 2),
        _ => (0.04, -0.01, 0.02, 0.02, 1),
    }
}

#[no_mangle]
pub extern "C" fn sim_bootstrap() -> SimSnapshot {
    let mut state = sim_state().lock().expect("sim state poisoned");
    *state = SimState::bootstrap();
    state.snapshot()
}

#[no_mangle]
pub extern "C" fn sim_tick(delta_seconds: f32) -> SimSnapshot {
    let mut state = sim_state().lock().expect("sim state poisoned");
    state.tick(delta_seconds);
    state.snapshot()
}

#[no_mangle]
pub extern "C" fn sim_set_policy(wage_index: f32, tax_rate: f32) -> SimSnapshot {
    let mut state = sim_state().lock().expect("sim state poisoned");
    state.set_policy(wage_index, tax_rate);
    state.snapshot()
}

#[no_mangle]
pub extern "C" fn sim_get_snapshot() -> SimSnapshot {
    let state = sim_state().lock().expect("sim state poisoned");
    state.snapshot()
}

#[no_mangle]
pub extern "C" fn sim_generate_planet(seed: u32, width: u32, height: u32) -> PlanetSummary {
    sim_generate_planet_from_core(seed, seed % 10, width, height)
}

#[no_mangle]
pub extern "C" fn sim_generate_planet_from_core(
    seed: u32,
    core_profile_id: u32,
    width: u32,
    height: u32,
) -> PlanetSummary {
    let safe_width = width.clamp(16, 1024);
    let safe_height = height.clamp(16, 1024);
    let (height_bias, water_bias, metal_richness, coal_richness, min_unique_sites) =
        core_profile_bias(core_profile_id);
    let mut summary = PlanetSummary {
        seed,
        core_profile_id: core_profile_id % 10,
        width: safe_width,
        height: safe_height,
        avg_height: 0.0,
        lowland_tiles: 0,
        midland_tiles: 0,
        highland_tiles: 0,
        water_tiles: 0,
        mountain_tiles: 0,
        iron_tiles: 0,
        copper_tiles: 0,
        coal_tiles: 0,
        unique_sites: 0,
    };

    let mut height_sum = 0.0f32;
    let mut unique_candidates = 0u32;
    for y in 0..safe_height {
        for x in 0..safe_width {
            let mixed = seed
                ^ x.wrapping_mul(374_761_393)
                ^ y.wrapping_mul(668_265_263)
                ^ (safe_width.wrapping_mul(31))
                ^ (safe_height.wrapping_mul(131));
            let noise = hash32(mixed);
            let h_noise = ((noise & 1023) as f32 / 1023.0 + height_bias).clamp(0.0, 1.0);
            let biome_noise = ((noise >> 10) & 1023) as f32 / 1023.0;
            let res_noise = ((noise >> 20) & 1023) as f32 / 1023.0;
            let water_threshold = (0.20 + water_bias).clamp(0.05, 0.55);
            let mountain_threshold = (0.82 + height_bias * 0.20).clamp(0.68, 0.95);

            height_sum += h_noise;
            if h_noise < 0.33 {
                summary.lowland_tiles += 1;
            } else if h_noise < 0.66 {
                summary.midland_tiles += 1;
            } else {
                summary.highland_tiles += 1;
            }

            if h_noise < water_threshold {
                summary.water_tiles += 1;
                continue;
            }
            if h_noise > mountain_threshold {
                summary.mountain_tiles += 1;
            }

            if res_noise > (0.86 - metal_richness).clamp(0.50, 0.92) && h_noise > 0.25 {
                summary.iron_tiles += 1;
            } else if res_noise > (0.76 - metal_richness * 0.8).clamp(0.44, 0.90) && h_noise > 0.20 {
                summary.copper_tiles += 1;
            } else if res_noise > (0.62 - coal_richness).clamp(0.35, 0.88)
                && biome_noise > 0.45
                && h_noise > 0.22
            {
                summary.coal_tiles += 1;
            }

            if biome_noise > 0.97 && h_noise > water_threshold && h_noise < mountain_threshold {
                unique_candidates += 1;
            }
        }
    }

    let total_tiles = (safe_width as f32) * (safe_height as f32);
    summary.avg_height = height_sum / total_tiles;
    summary.unique_sites = unique_candidates.max(min_unique_sites);
    summary
}

#[no_mangle]
pub extern "C" fn sim_generate_system(seed: u32) -> SystemSummary {
    let mut avg_height_sum = 0.0f32;
    let mut unique_sum = 0u32;
    for core_id in 0..10u32 {
        let planet_seed = seed ^ core_id.wrapping_mul(265_443_5761);
        let planet = sim_generate_planet_from_core(planet_seed, core_id, 128, 128);
        avg_height_sum += planet.avg_height;
        unique_sum = unique_sum.saturating_add(planet.unique_sites);
    }

    SystemSummary {
        seed,
        planet_count: 10,
        core_planets: 10,
        procedural_planets: 0,
        avg_height_across_planets: avg_height_sum / 10.0,
        total_unique_sites: unique_sum,
    }
}

