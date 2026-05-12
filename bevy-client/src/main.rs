use bevy::prelude::*;
use bevy::window::WindowResolution;

const WORLD_W: i32 = 96;
const WORLD_H: i32 = 64;
const CHUNK_SIZE: f32 = 56.0;
const DRAW_SIZE: f32 = 56.8;
// 2.5D: tiles sit on a flat grid; elevation only shifts depth/shade, NOT position.
const ISO_Y_RATIO: f32 = 0.60;   // screen-Y shrink for the iso look (height of tile vs width)
const BASE_SEED: u32 = 0xBEE5_2026;
const LANDMARK_REGION: i32 = 14;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Terrain {
    Water,
    Lowland,
    Midland,
    Highland,
    Mountain,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Ore {
    Iron,
    Copper,
    Coal,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Biome {
    Frozen,
    Temperate,
    Wetland,
    Desert,
    Volcanic,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Landmark {
    CrystalLake,
    AncientRuins,
    GeothermalVent,
    ImpactCrater,
}

#[derive(Clone, Copy, Debug)]
struct Chunk {
    terrain: Terrain,
    biome: Biome,
    ore: Option<Ore>,
    landmark: Option<Landmark>,
    elevation: f32,
    moisture: f32,
    heat: f32,
    tree_density: f32,
}

#[derive(Resource)]
struct WorldMap {
    width: i32,
    height: i32,
    chunks: Vec<Chunk>,
}

#[derive(Component)]
struct CameraRig;

#[derive(Component)]
struct Player;

#[derive(Component)]
struct WaterTile {
    phase: f32,
}

#[derive(Resource)]
struct TileTextures {
    water: Handle<Image>,
    lowland: Handle<Image>,
    midland: Handle<Image>,
    highland: Handle<Image>,
    sand: Handle<Image>,
    stone: Handle<Image>,
    mountain: Handle<Image>,
    iron: Handle<Image>,
    copper: Handle<Image>,
    coal: Handle<Image>,
}

fn main() {
    App::new()
        .insert_resource(ClearColor(Color::srgb(0.06, 0.08, 0.11)))
        .add_plugins(
            DefaultPlugins
                .set(AssetPlugin {
                    file_path: "../assets".to_string(),
                    ..default()
                })
                .set(WindowPlugin {
                    primary_window: Some(Window {
                        title: "Orbitum (Bevy migration)".into(),
                        resolution: WindowResolution::new(1600, 900),
                        resizable: true,
                        ..default()
                    }),
                    ..default()
                }),
        )
        .add_systems(Startup, setup)
        .add_systems(Update, (player_movement, camera_follow, animate_water))
        .run();
}

fn setup(mut commands: Commands, asset_server: Res<AssetServer>) {
    let textures = TileTextures {
        water: asset_server.load("generated/sd_tiles_qhd_realistic/water_1024.png"),
        lowland: asset_server.load("generated/sd_tiles_qhd_realistic/lowland_1024.png"),
        midland: asset_server.load("generated/sd_tiles_qhd_realistic/midland_1024.png"),
        highland: asset_server.load("generated/sd_tiles_qhd_realistic/highland_1024.png"),
        sand: asset_server.load("generated/sd_tiles_qhd_realistic/sand_1024.png"),
        stone: asset_server.load("generated/sd_tiles_qhd_realistic/stone_1024.png"),
        mountain: asset_server.load("generated/sd_tiles_qhd_realistic/mountain_1024.png"),
        iron: asset_server.load("generated/sd_tiles_qhd_realistic/iron_1024.png"),
        copper: asset_server.load("generated/sd_tiles_qhd_realistic/copper_1024.png"),
        coal: asset_server.load("generated/sd_tiles_qhd_realistic/coal_1024.png"),
    };
    let (seed, world) = select_good_world(WORLD_W, WORLD_H);
    println!("Generating world seed: {seed} | {}", world_summary(&world));
    let start = recommended_spawn_point(&world, seed);
    println!("Camera start: x={:.1} y={:.1}", start.x, start.y);
    spawn_world(&mut commands, &world, &textures, seed);

    // Player sprite — white body + colored outline to stand out on any biome
    let player_tex = asset_server.load("generated/sd_tiles_qhd_realistic/player_1024.png");
    let pw = CHUNK_SIZE * 0.55;
    let ph = CHUNK_SIZE * 0.70;
    commands.spawn((
        {
            let mut s = Sprite::from_image(player_tex);
            s.custom_size = Some(Vec2::new(pw, ph));
            s
        },
        Transform::from_xyz(start.x, start.y + ph * 0.25, 500.0),
        Player,
    ));

    commands.spawn((
        Camera2d,
        Projection::Orthographic(OrthographicProjection {
            near: -5000.0,
            far: 5000.0,
            scale: 0.85,
            ..OrthographicProjection::default_2d()
        }),
        Transform::from_xyz(start.x, start.y, 1000.0),
        CameraRig,
    ));

    commands.insert_resource(textures);
    commands.insert_resource(world);
}

fn player_movement(
    time: Res<Time>,
    keys: Res<ButtonInput<KeyCode>>,
    mut player_q: Query<&mut Transform, With<Player>>,
    mut cam_q: Query<&mut Projection, With<CameraRig>>,
) {
    let Ok(mut pt) = player_q.single_mut() else { return; };

    let mut dir = Vec3::ZERO;
    if keys.pressed(KeyCode::KeyW) { dir.y += 1.0; }
    if keys.pressed(KeyCode::KeyS) { dir.y -= 1.0; }
    if keys.pressed(KeyCode::KeyA) { dir.x -= 1.0; }
    if keys.pressed(KeyCode::KeyD) { dir.x += 1.0; }

    if dir.length_squared() > 0.0 {
        let speed = 220.0 * time.delta_secs();
        pt.translation += dir.normalize() * speed;
    }

    // Zoom Q/E
    if let Ok(mut proj) = cam_q.single_mut() {
        let zoom_speed = 1.4 * time.delta_secs();
        if keys.pressed(KeyCode::KeyQ) {
            if let Projection::Orthographic(o) = &mut *proj {
                o.scale = (o.scale * (1.0 + zoom_speed)).clamp(0.20, 8.0);
            }
        }
        if keys.pressed(KeyCode::KeyE) {
            if let Projection::Orthographic(o) = &mut *proj {
                o.scale = (o.scale * (1.0 - zoom_speed)).clamp(0.20, 8.0);
            }
        }
    }
}

fn camera_follow(
    player_q: Query<&Transform, With<Player>>,
    mut cam_q: Query<&mut Transform, (With<CameraRig>, Without<Player>)>,
) {
    let Ok(pt) = player_q.single() else { return; };
    let Ok(mut ct) = cam_q.single_mut() else { return; };
    // Smooth follow — lerp camera toward player
    let target = Vec3::new(pt.translation.x, pt.translation.y, ct.translation.z);
    ct.translation = ct.translation.lerp(target, 0.12);
}

fn animate_water(time: Res<Time>, mut q: Query<(&mut Sprite, &WaterTile)>) {
    let t = time.elapsed_secs();
    for (mut sprite, water) in &mut q {
        let wave = (t * 1.7 + water.phase).sin() * 0.08;
        sprite.color = Color::srgba(0.88 + wave * 0.2, 0.97 + wave * 0.15, 1.0, 0.92);
    }
}

fn spawn_world(commands: &mut Commands, world: &WorldMap, textures: &TileTextures, seed: u32) {
    // Tile dimensions: full width, squashed height for 2.5D top-down iso feel
    let tw = CHUNK_SIZE;
    let th = CHUNK_SIZE * ISO_Y_RATIO;
    let tile_size = Vec2::new(tw, th);

    let offset_x = -(world.width  as f32 * tw) * 0.5 + tw * 0.5;
    let offset_y = -(world.height as f32 * th) * 0.5 + th * 0.5;

    let chunk_at = |cx: i32, cy: i32| -> Option<Chunk> {
        if cx < 0 || cy < 0 || cx >= world.width || cy >= world.height {
            None
        } else {
            Some(world.chunks[(cy * world.width + cx) as usize])
        }
    };

    for y in 0..world.height {
        for x in 0..world.width {
            let chunk = world.chunks[(y * world.width + x) as usize];
            let sx = offset_x + x as f32 * tw;
            let sy = offset_y + y as f32 * th;
            // Y-sort depth: rows drawn back-to-front; elevation slightly lifts in z only
            let z = y as f32 * 0.01 + chunk.elevation * 0.005;

            // --- BASE TEXTURE TILE (fills the entire cell, no gap) ---
            let img = terrain_image(textures, chunk.terrain, chunk.biome);
            let tint = biome_tint(chunk.terrain, chunk.biome, chunk.moisture, chunk.heat);
            let mut base = Sprite::from_image(img);
            base.custom_size = Some(tile_size);
            base.color = tint;
            let mut ec = commands.spawn((base, Transform::from_xyz(sx, sy, z)));
            if chunk.terrain == Terrain::Water {
                ec.insert(WaterTile {
                    phase: hash01(x, y, seed ^ 0x7777) * std::f32::consts::TAU,
                });
            }

            // --- ELEVATION SHADING: darker for lower, slightly brighter for peaks ---
            if chunk.terrain != Terrain::Water {
                let shade_a = ((0.55 - chunk.elevation) * 0.38).clamp(0.0, 0.22);
                if shade_a > 0.01 {
                    commands.spawn((
                        Sprite::from_color(Color::srgba(0.0, 0.0, 0.0, shade_a), tile_size),
                        Transform::from_xyz(sx, sy, z + 0.001),
                    ));
                }
            }

            // --- SOUTH-EDGE WALL: dark strip below tile when neighbour is lower ---
            let wall_h = 6.0; // pixels of "cliff" visible below each tile
            if chunk.terrain != Terrain::Water {
                if let Some(south) = chunk_at(x, y - 1) {
                    let drop = chunk.elevation - south.elevation;
                    if drop > 0.06 {
                        let a = (drop * 1.8).clamp(0.0, 0.55);
                        commands.spawn((
                            Sprite::from_color(
                                Color::srgba(0.07, 0.06, 0.05, a),
                                Vec2::new(tw, wall_h),
                            ),
                            Transform::from_xyz(sx, sy - th * 0.5 - wall_h * 0.5, z + 0.002),
                        ));
                    }
                }
            }

            // --- ORE: 3-4 small ore-texture patches scattered on terrain ---
            if let Some(ore) = chunk.ore {
                let ore_tex = match ore {
                    Ore::Iron   => textures.iron.clone(),
                    Ore::Copper => textures.copper.clone(),
                    Ore::Coal   => textures.coal.clone(),
                };
                let base_tint = ore_tint(ore);
                let count = 3u32 + (hash01(x, y, seed ^ 0xAB01) * 2.0) as u32;
                for i in 0..count {
                    let nx = hash01(x + i as i32 * 3, y,            seed ^ (0xF001 + i * 13)) - 0.5;
                    let ny = hash01(x,                 y + i as i32 * 3, seed ^ (0xF002 + i * 17)) - 0.5;
                    let scale = 0.28 + hash01(x + i as i32, y + 55, seed ^ 0xF005) * 0.14;
                    let pw = tw * scale;
                    let ph = th * scale * 0.75;
                    let mut spr = Sprite::from_image(ore_tex.clone());
                    spr.custom_size = Some(Vec2::new(pw, ph));
                    spr.color = base_tint.with_alpha(0.90);
                    commands.spawn((spr, Transform::from_xyz(
                        sx + nx * tw * 0.72,
                        sy + ny * th * 0.72,
                        z + 0.003 + i as f32 * 0.00005,
                    )));
                }
            }

            // --- TREES: small scattered dots, NOT large blocks ---
            if chunk.tree_density > 0.62 && chunk.terrain != Terrain::Water {
                let tree_col = tree_color(chunk.biome, chunk.moisture);
                let count = if chunk.tree_density > 0.80 { 4u32 } else { 2u32 };
                for t in 0..count {
                    let hx = hash01(x + t as i32, y,          seed ^ (0xBEEF + t * 7)) - 0.5;
                    let hy = hash01(x,             y + t as i32, seed ^ (0xCAFE + t * 5)) - 0.5;
                    if hash01(x + t as i32, y + t as i32, seed ^ 0xDEAD) > chunk.tree_density { continue; }
                    // canopy circle (wider than tall to look top-down)
                    let cw = tw * (0.16 + hash01(x + t as i32, y + 77, seed ^ 0xCE01) * 0.10);
                    let ch = cw * 0.65;
                    // darker trunk dot below canopy
                    let trunk_col = Color::srgb(0.22, 0.16, 0.09);
                    commands.spawn((
                        Sprite::from_color(trunk_col.with_alpha(0.80),
                            Vec2::new(cw * 0.28, ch * 0.55)),
                        Transform::from_xyz(
                            sx + hx * tw * 0.72,
                            sy + hy * th * 0.60,
                            z + 0.004 + t as f32 * 0.0001,
                        ),
                    ));
                    commands.spawn((
                        Sprite::from_color(tree_col.with_alpha(0.85), Vec2::new(cw, ch)),
                        Transform::from_xyz(
                            sx + hx * tw * 0.72,
                            sy + hy * th * 0.60 + ch * 0.30,
                            z + 0.0041 + t as f32 * 0.0001,
                        ),
                    ));
                }
            }

            // --- LANDMARK: small distinct marker, subtle glow ring ---
            if let Some(lm) = chunk.landmark {
                let (col, glow) = landmark_colors(lm);
                // Outer glow ring (semi-transparent, ~half tile)
                commands.spawn((
                    Sprite::from_color(glow.with_alpha(0.22), Vec2::new(tw * 0.55, th * 0.55)),
                    Transform::from_xyz(sx, sy, z + 0.005),
                ));
                // Inner icon (small, ~1/6 tile)
                commands.spawn((
                    Sprite::from_color(col.with_alpha(0.92), Vec2::new(tw * 0.14, th * 0.18)),
                    Transform::from_xyz(sx, sy, z + 0.006),
                ));
            }
        }
    }
}

fn terrain_image(textures: &TileTextures, terrain: Terrain, biome: Biome) -> Handle<Image> {
    match terrain {
        Terrain::Water => textures.water.clone(),
        Terrain::Lowland => match biome {
            Biome::Desert => textures.sand.clone(),
            _ => textures.lowland.clone(),
        },
        Terrain::Midland => match biome {
            Biome::Desert => textures.sand.clone(),
            _ => textures.midland.clone(),
        },
        Terrain::Highland => match biome {
            Biome::Volcanic => textures.stone.clone(),
            _ => textures.highland.clone(),
        },
        Terrain::Mountain => match biome {
            Biome::Volcanic => textures.stone.clone(),
            _ => textures.mountain.clone(),
        },
    }
}

// Unified tint: texture is multiplied by this color to express biome character
fn biome_tint(terrain: Terrain, biome: Biome, moisture: f32, heat: f32) -> Color {
    match terrain {
        Terrain::Water => match biome {
            Biome::Frozen => Color::srgb(0.76, 0.90, 1.00),
            _ => Color::srgb(0.82, 0.95, 1.00),
        },
            _ => {
            let (r, g, b): (f32, f32, f32) = match biome {
                Biome::Frozen    => (0.88, 0.94, 1.00),
                Biome::Desert    => (1.10, 0.95, 0.72),
                Biome::Wetland   => (0.80, 1.05, 0.80),
                Biome::Volcanic  => (0.90, 0.82, 0.76),
                Biome::Temperate => {
                    let g = 0.92 + moisture * 0.10;
                    let r = 0.92 + heat * 0.06;
                    (r, g, 0.86_f32)
                }
            };
            Color::srgb(r.min(1.0), g.min(1.0), b.min(1.0))
        }
    }
}

fn ore_tint(ore: Ore) -> Color {
    match ore {
        Ore::Iron   => Color::srgb(0.82, 0.86, 0.90),   // metallic gray-blue
        Ore::Copper => Color::srgb(0.88, 0.52, 0.22),   // distinct orange-brown
        Ore::Coal   => Color::srgb(0.18, 0.18, 0.22),   // near-black
    }
}

fn tree_color(biome: Biome, moisture: f32) -> Color {
    let g = 0.38 + moisture * 0.25;
    match biome {
        Biome::Frozen    => Color::srgb(0.72, 0.84, 0.72),
        Biome::Desert    => Color::srgb(0.55, 0.52, 0.28),
        Biome::Volcanic  => Color::srgb(0.35, 0.30, 0.22),
        Biome::Wetland   => Color::srgb(0.18, g + 0.08, 0.22),
        Biome::Temperate => Color::srgb(0.20, g,        0.18),
    }
}

fn landmark_colors(lm: Landmark) -> (Color, Color) {
    match lm {
        Landmark::CrystalLake    => (Color::srgb(0.20, 0.80, 1.00), Color::srgb(0.30, 0.70, 0.90)),
        Landmark::AncientRuins   => (Color::srgb(0.90, 0.84, 0.60), Color::srgb(0.70, 0.65, 0.45)),
        Landmark::GeothermalVent => (Color::srgb(1.00, 0.40, 0.15), Color::srgb(0.80, 0.30, 0.10)),
        Landmark::ImpactCrater   => (Color::srgb(0.60, 0.55, 0.52), Color::srgb(0.45, 0.42, 0.40)),
    }
}

fn generate_world(seed: u32, width: i32, height: i32) -> WorldMap {
    let mut chunks = Vec::with_capacity((width * height) as usize);
    let region_size = 9;

    for y in 0..height {
        for x in 0..width {
            let rx = x / region_size;
            let ry = y / region_size;
            let region_seed = hash2d(rx as u32, ry as u32, seed ^ 0x41C1);

            let fx = x as f32 / (width - 1).max(1) as f32;
            let fy = y as f32 / (height - 1).max(1) as f32;
            let px = fx * 2.0 - 1.0;
            let py = fy * 2.0 - 1.0;

            let planet_mask = clamp01(1.0 - (px * px + py * py).sqrt());
            let continent = fbm(fx * 2.3 + 0.4, fy * 2.3 - 0.2, region_seed ^ 0x1101, 4, 2.05, 0.56);
            let tectonic = fbm(fx * 1.3 - 8.0, fy * 1.3 + 6.0, region_seed ^ 0x1201, 3, 2.0, 0.54);
            let ridge = fbm(fx * 5.4 + 11.0, fy * 5.4 - 5.0, region_seed ^ 0x2202, 3, 2.10, 0.58);
            let wet = fbm(fx * 3.8 + 19.0, fy * 3.8 + 7.0, region_seed ^ 0x3303, 3, 2.00, 0.56);
            let heat = fbm(fx * 2.6 - 13.0, fy * 2.6 + 9.0, region_seed ^ 0x4404, 3, 2.00, 0.56);

            let elevation = clamp01(
                planet_mask * 0.70
                    + continent * 0.22
                    + tectonic * 0.07
                    + ridge * 0.16
                    - (1.0 - planet_mask) * 0.18
                    - 0.04,
            );
            let moisture = clamp01(wet * 0.66 + continent * 0.14 + (1.0 - elevation) * 0.20);
            let temp = clamp01((1.0 - py.abs()) * 0.62 + heat * 0.38 - elevation * 0.24);

            let terrain = if elevation < 0.24 || planet_mask < 0.09 {
                Terrain::Water
            } else if elevation < 0.42 {
                Terrain::Lowland
            } else if elevation < 0.63 {
                Terrain::Midland
            } else if elevation < 0.82 {
                Terrain::Highland
            } else {
                Terrain::Mountain
            };

            let biome = if terrain == Terrain::Water {
                if temp < 0.30 {
                    Biome::Frozen
                } else {
                    Biome::Wetland
                }
            } else if temp < 0.28 {
                Biome::Frozen
            } else if moisture < 0.27 && temp > 0.55 {
                Biome::Desert
            } else if moisture > 0.67 {
                Biome::Wetland
            } else if temp > 0.72 && moisture < 0.46 {
                Biome::Volcanic
            } else {
                Biome::Temperate
            };

            let ore = if terrain == Terrain::Water {
                None
            } else {
                let belt_iron = 1.0 - ((fbm(fx * 6.2 + 4.0, fy * 2.5 - 3.0, seed ^ 0xA110, 2, 2.0, 0.5) - 0.5).abs() * 2.0);
                let belt_copper =
                    1.0 - ((fbm(fx * 3.2 - 7.0, fy * 5.7 + 2.0, seed ^ 0xB220, 2, 2.0, 0.5) - 0.5).abs() * 2.0);
                let belt_coal = 1.0 - ((fbm(fx * 4.8 + 9.0, fy * 4.8 - 9.0, seed ^ 0xC330, 2, 2.0, 0.5) - 0.5).abs() * 2.0);

                let iron_bias = belt_iron * 0.42
                    + elevation * 0.26
                    + if terrain == Terrain::Mountain { 0.14 } else { 0.0 }
                    + if biome == Biome::Frozen { 0.06 } else { 0.0 };
                let copper_bias = belt_copper * 0.42
                    + temp * 0.25
                    + if biome == Biome::Desert || biome == Biome::Volcanic { 0.12 } else { 0.0 };
                let coal_bias = belt_coal * 0.42
                    + moisture * 0.28
                    + if biome == Biome::Wetland || terrain == Terrain::Lowland { 0.10 } else { 0.0 };

                let ore_density = hash01(x, y, seed ^ 0x0E50);
                if iron_bias >= copper_bias && iron_bias >= coal_bias && iron_bias > 0.66 && ore_density > 0.56 {
                    Some(Ore::Iron)
                } else if copper_bias >= iron_bias && copper_bias >= coal_bias && copper_bias > 0.66 && ore_density > 0.56 {
                    Some(Ore::Copper)
                } else if coal_bias > 0.66 && ore_density > 0.56 {
                    Some(Ore::Coal)
                } else {
                    None
                }
            };

            let tree_density = if terrain == Terrain::Water {
                0.0
            } else {
                let mut density = moisture * 0.74 + (1.0 - (temp - 0.52).abs() * 1.35) * 0.18;
                if terrain == Terrain::Mountain {
                    density *= 0.10;
                } else if terrain == Terrain::Highland {
                    density *= 0.45;
                }
                if biome == Biome::Desert || biome == Biome::Volcanic {
                    density *= 0.15;
                } else if biome == Biome::Wetland {
                    density *= 1.18;
                }
                if ore.is_some() {
                    density *= 0.30;
                }
                density
            };

            chunks.push(Chunk {
                terrain,
                biome,
                ore,
                landmark: None,
                elevation,
                moisture,
                heat: temp,
                tree_density: clamp01(tree_density),
            });
        }
    }

    for ry in (0..height).step_by(LANDMARK_REGION as usize) {
        for rx in (0..width).step_by(LANDMARK_REGION as usize) {
            let mut best_idx: Option<usize> = None;
            let mut best_score = 0.0f32;
            let y_end = (ry + LANDMARK_REGION).min(height);
            let x_end = (rx + LANDMARK_REGION).min(width);
            for y in ry..y_end {
                for x in rx..x_end {
                    let idx = (y * width + x) as usize;
                    let c = chunks[idx];
                    if c.terrain == Terrain::Water || c.ore.is_some() {
                        continue;
                    }
                    let marker = fbm(x as f32 * 0.33, y as f32 * 0.33, seed ^ 0xD00D, 2, 2.0, 0.5);
                    let score = marker * 0.58 + c.elevation * 0.20 + c.moisture * 0.11 + c.heat * 0.11;
                    if score > best_score {
                        best_score = score;
                        best_idx = Some(idx);
                    }
                }
            }
            if let Some(idx) = best_idx.filter(|_| best_score > 0.62) {
                let c = chunks[idx];
                let landmark = match c.biome {
                    Biome::Wetland => Landmark::CrystalLake,
                    Biome::Volcanic => Landmark::GeothermalVent,
                    Biome::Desert => Landmark::ImpactCrater,
                    Biome::Frozen => Landmark::AncientRuins,
                    Biome::Temperate => {
                        if c.elevation > 0.7 {
                            Landmark::AncientRuins
                        } else {
                            Landmark::CrystalLake
                        }
                    }
                };
                chunks[idx].landmark = Some(landmark);
            }
        }
    }

    WorldMap { width, height, chunks }
}

fn hash2d(x: u32, y: u32, seed: u32) -> u32 {
    let mut h = seed ^ x.wrapping_mul(73856093) ^ y.wrapping_mul(19349663);
    h ^= h >> 13;
    h = h.wrapping_mul(1274126177);
    h ^ (h >> 16)
}

fn hash01(x: i32, y: i32, seed: u32) -> f32 {
    let hx = hash2d(x as u32, 0, seed ^ 0x9E3779B9);
    let hy = hash2d(0, y as u32, seed ^ 0x85EBCA6B);
    let h = hash2d(hx, hy, seed ^ 0x27D4EB2D);
    (h & 0x00FF_FFFF) as f32 / 16_777_216.0
}

fn smooth_step(t: f32) -> f32 {
    let t = clamp01(t);
    t * t * (3.0 - 2.0 * t)
}

fn value_noise(x: f32, y: f32, seed: u32) -> f32 {
    let x0 = x.floor() as i32;
    let y0 = y.floor() as i32;
    let x1 = x0 + 1;
    let y1 = y0 + 1;
    let tx = smooth_step(x - x0 as f32);
    let ty = smooth_step(y - y0 as f32);

    let n00 = hash01(x0, y0, seed);
    let n10 = hash01(x1, y0, seed);
    let n01 = hash01(x0, y1, seed);
    let n11 = hash01(x1, y1, seed);

    let nx0 = n00 + (n10 - n00) * tx;
    let nx1 = n01 + (n11 - n01) * tx;
    nx0 + (nx1 - nx0) * ty
}

fn fbm(x: f32, y: f32, seed: u32, octaves: usize, lacunarity: f32, gain: f32) -> f32 {
    let mut sum = 0.0;
    let mut amp = 1.0;
    let mut freq = 1.0;
    let mut norm = 0.0;
    for i in 0..octaves {
        sum += value_noise(x * freq, y * freq, seed ^ ((i as u32) * 977)) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    if norm > 0.0 { sum / norm } else { 0.0 }
}

fn clamp01(v: f32) -> f32 {
    v.clamp(0.0, 1.0)
}


fn runtime_seed() -> u32 {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0xA11CE55E);
    let mixed = nanos ^ (nanos >> 33) ^ ((WORLD_W as u64) << 17) ^ ((WORLD_H as u64) << 5);
    (mixed as u32).wrapping_mul(1664525).wrapping_add(1013904223) ^ BASE_SEED
}

fn world_offsets(world: &WorldMap) -> (f32, f32) {
    (
        -(world.width as f32 * CHUNK_SIZE) * 0.5 + CHUNK_SIZE * 0.5,
        -(world.height as f32 * CHUNK_SIZE) * 0.5 + CHUNK_SIZE * 0.5,
    )
}

fn project_world_pos(world_x: f32, world_y: f32, _elevation: f32) -> Vec2 {
    Vec2::new(world_x, world_y)
}

fn chunk_screen_pos(world: &WorldMap, x: i32, y: i32) -> Vec2 {
    let (ox, oy) = world_offsets(world);
    let world_x = ox + x as f32 * CHUNK_SIZE;
    let world_y = oy + y as f32 * CHUNK_SIZE;
    let idx = (y * world.width + x) as usize;
    let elevation = world.chunks[idx].elevation;
    project_world_pos(world_x, world_y, elevation)
}

fn recommended_spawn_point(world: &WorldMap, seed: u32) -> Vec2 {
    let center_x = (world.width - 1) as f32 * 0.5;
    let center_y = (world.height - 1) as f32 * 0.5;
    let mut best_idx = 0usize;
    let mut best_score = f32::MIN;
    for y in 0..world.height {
        for x in 0..world.width {
            let idx = (y * world.width + x) as usize;
            let c = world.chunks[idx];
            if c.terrain == Terrain::Water {
                continue;
            }
            let dx = x as f32 - center_x;
            let dy = y as f32 - center_y;
            let dist_center = (dx * dx + dy * dy).sqrt();
            let mut score = 2.0 - dist_center * 0.03;
            if c.landmark.is_some() {
                score += 2.4;
            }
            if c.ore.is_some() {
                score += 0.8;
            }
            score += c.tree_density * 0.6;
            score += hash01(x, y, seed ^ 0x5151) * 0.2;
            if score > best_score {
                best_score = score;
                best_idx = idx;
            }
        }
    }
    let x = (best_idx as i32) % world.width;
    let y = (best_idx as i32) / world.width;
    chunk_screen_pos(world, x, y)
}

fn world_summary(world: &WorldMap) -> String {
    let mut water = 0usize;
    let mut iron = 0usize;
    let mut copper = 0usize;
    let mut coal = 0usize;
    let mut landmarks = 0usize;
    for c in &world.chunks {
        if c.terrain == Terrain::Water {
            water += 1;
        }
        match c.ore {
            Some(Ore::Iron) => iron += 1,
            Some(Ore::Copper) => copper += 1,
            Some(Ore::Coal) => coal += 1,
            None => {}
        }
        if c.landmark.is_some() {
            landmarks += 1;
        }
    }
    let total = world.chunks.len().max(1);
    let water_pct = (water as f32 / total as f32) * 100.0;
    format!(
        "water:{water_pct:.1}% ores(I:{iron} C:{copper} Co:{coal}) landmarks:{landmarks}"
    )
}

fn world_score(world: &WorldMap) -> f32 {
    let mut water = 0usize;
    let mut iron = 0usize;
    let mut copper = 0usize;
    let mut coal = 0usize;
    let mut landmarks = 0usize;
    for c in &world.chunks {
        if c.terrain == Terrain::Water {
            water += 1;
        }
        match c.ore {
            Some(Ore::Iron) => iron += 1,
            Some(Ore::Copper) => copper += 1,
            Some(Ore::Coal) => coal += 1,
            None => {}
        }
        if c.landmark.is_some() {
            landmarks += 1;
        }
    }
    let total = world.chunks.len().max(1) as f32;
    let water_ratio = water as f32 / total;
    let land_ratio = 1.0 - water_ratio;
    let mut score = 0.0;
    score += 1.0 - (water_ratio - 0.40).abs() * 2.2;
    score += (land_ratio - 0.30).max(0.0) * 0.8;
    score += ((iron.min(copper).min(coal) as f32) / 180.0).min(1.0) * 1.0;
    score += ((landmarks as f32) / 18.0).min(1.0) * 0.8;
    score
}

fn select_good_world(width: i32, height: i32) -> (u32, WorldMap) {
    let base = runtime_seed();
    let mut best_seed = base;
    let mut best_world = generate_world(base, width, height);
    let mut best_score = world_score(&best_world);
    for i in 1..48u32 {
        let seed = base.wrapping_add(i.wrapping_mul(2654435761));
        let world = generate_world(seed, width, height);
        let score = world_score(&world);
        if score > best_score {
            best_seed = seed;
            best_world = world;
            best_score = score;
        }
    }
    (best_seed, best_world)
}
