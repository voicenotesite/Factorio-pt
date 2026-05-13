use bevy::prelude::*;
use bevy::window::WindowResolution;

const WORLD_W: i32 = 96;
const WORLD_H: i32 = 64;
const CHUNK_SIZE: f32 = 56.0;
const ISO_Y_RATIO: f32 = 0.60;
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
    ore_richness: f32,
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
    depth: f32,
    shore: f32,
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
                .set(ImagePlugin::default_nearest())   // nearest-neighbor: no blur, no shimmer
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
        .add_systems(Startup, dither_ore_textures.after(setup))
        .add_systems(Update, (player_movement, camera_follow, animate_water, pixel_rounding))
        .run();
}

fn setup(mut commands: Commands, asset_server: Res<AssetServer>) {
    // Use the clean Factorio-like set; realistic set created heavy visual noise at tile scale.
    let textures = TileTextures {
        water: asset_server.load("generated/sd_tiles_factorio_clean/water_1024.png"),
        lowland: asset_server.load("generated/sd_tiles_factorio_clean/lowland_1024.png"),
        midland: asset_server.load("generated/sd_tiles_factorio_clean/midland_1024.png"),
        highland: asset_server.load("generated/sd_tiles_factorio_clean/highland_1024.png"),
        sand: asset_server.load("generated/sd_tiles_factorio_clean/stone_1024.png"),
        stone: asset_server.load("generated/sd_tiles_factorio_clean/stone_1024.png"),
        mountain: asset_server.load("generated/sd_tiles_factorio_clean/mountain_1024.png"),
        iron: asset_server.load("generated/sd_tiles_factorio_clean/iron_1024.png"),
        copper: asset_server.load("generated/sd_tiles_factorio_clean/copper_1024.png"),
        coal: asset_server.load("generated/sd_tiles_factorio_clean/coal_1024.png"),
    };
    let (seed, world) = select_good_world(WORLD_W, WORLD_H);
    println!("Generating world seed: {seed} | {}", world_summary(&world));
    let start = recommended_spawn_point(&world, seed);
    println!("Camera start: x={:.1} y={:.1}", start.x, start.y);
    spawn_world(&mut commands, &world, &textures, seed);

    // Player — bright yellow square with black border, clearly visible on any terrain
    let pr = CHUNK_SIZE * 0.40;
    commands.spawn((
        Sprite::from_color(Color::srgb(1.0, 0.88, 0.10), Vec2::new(pr, pr)),
        Transform::from_xyz(start.x, start.y, 500.0),
        Player,
    ));

    commands.spawn((
        Camera2d,
        Projection::Orthographic(OrthographicProjection {
            near: -5000.0,
            far: 5000.0,
            scale: 0.45,
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

/// Snap every transform to whole pixels — eliminates sub-pixel shimmer on all sprites.
fn pixel_rounding(mut q: Query<&mut Transform>) {
    for mut t in &mut q {
        t.translation.x = t.translation.x.round();
        t.translation.y = t.translation.y.round();
    }
}

/// Apply NeuQuant palette reduction (12 colors) + Floyd-Steinberg dithering
/// to ore textures at startup so they look like clean pixel art.
fn dither_ore_textures(
    textures: Res<TileTextures>,
    mut images: ResMut<Assets<Image>>,
) {
    for handle in [&textures.iron, &textures.copper, &textures.coal] {
        if let Some(img) = images.get_mut(handle) {
            dither_image(img, 12);
        }
    }
}

fn dither_image(img: &mut Image, n_colors: usize) {
    use color_quant::NeuQuant;

    let w = img.width() as usize;
    let h = img.height() as usize;
    if w == 0 || h == 0 { return; }

    // Bevy 0.18: img.data is Option<Vec<u8>>
    let data = match img.data.as_mut() {
        Some(d) => d,
        None => return,
    };
    if data.len() != w * h * 4 { return; }

    let nq = NeuQuant::new(10, n_colors, data.as_slice());

    let mut buf: Vec<[f32; 4]> = data
        .chunks_exact(4)
        .map(|c| [c[0] as f32, c[1] as f32, c[2] as f32, c[3] as f32])
        .collect();

    for y in 0..h {
        for x in 0..w {
            let idx = y * w + x;
            let old = buf[idx];
            let qi = nq.index_of(&[old[0] as u8, old[1] as u8, old[2] as u8, old[3] as u8]);
            let palette = nq.color_map_rgba();
            let pi = qi * 4;
            let new_c = [
                palette[pi]     as f32,
                palette[pi + 1] as f32,
                palette[pi + 2] as f32,
                old[3],
            ];
            buf[idx] = new_c;

            let err = [
                old[0] - new_c[0],
                old[1] - new_c[1],
                old[2] - new_c[2],
                0.0_f32,
            ];
            if x + 1 < w {
                let nb = &mut buf[y * w + x + 1];
                for c in 0..3 { nb[c] = (nb[c] + err[c] * 7.0 / 16.0).clamp(0.0, 255.0); }
            }
            if y + 1 < h {
                if x > 0 {
                    let nb = &mut buf[(y+1) * w + x - 1];
                    for c in 0..3 { nb[c] = (nb[c] + err[c] * 3.0 / 16.0).clamp(0.0, 255.0); }
                }
                {
                    let nb = &mut buf[(y+1) * w + x];
                    for c in 0..3 { nb[c] = (nb[c] + err[c] * 5.0 / 16.0).clamp(0.0, 255.0); }
                }
                if x + 1 < w {
                    let nb = &mut buf[(y+1) * w + x + 1];
                    for c in 0..3 { nb[c] = (nb[c] + err[c] * 1.0 / 16.0).clamp(0.0, 255.0); }
                }
            }
        }
    }

    // Write back into img.data
    let data = img.data.as_mut().unwrap();
    for (i, px) in buf.iter().enumerate() {
        data[i * 4]     = px[0] as u8;
        data[i * 4 + 1] = px[1] as u8;
        data[i * 4 + 2] = px[2] as u8;
        data[i * 4 + 3] = px[3] as u8;
    }
}

fn animate_water(time: Res<Time>, mut q: Query<(&mut Sprite, &WaterTile)>) {
    let t = time.elapsed_secs();
    for (mut sprite, water) in &mut q {
        let base = water_color(water.depth, water.shore * 0.5).to_srgba();
        let wave_a = (t * 1.6 + water.phase).sin();
        let wave_b = (t * 2.7 + water.phase * 0.61).cos();
        let shimmer = (wave_a * 0.020 + wave_b * 0.012) * (0.45 + water.depth * 0.55);
        let foam = water.shore * (0.04 + ((t * 2.2 + water.phase * 1.3).sin() * 0.5 + 0.5) * 0.06);
        sprite.color = Color::srgba(
            (base.red + shimmer + foam).clamp(0.0, 1.0),
            (base.green + shimmer * 0.7 + foam * 0.9).clamp(0.0, 1.0),
            (base.blue + shimmer * 0.4 + foam * 0.6).clamp(0.0, 1.0),
            (0.88 + water.shore * 0.05).clamp(0.0, 1.0),
        );
    }
}

fn spawn_world(commands: &mut Commands, world: &WorldMap, textures: &TileTextures, seed: u32) {
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
            let z = y as f32 * 0.01 + chunk.elevation * 0.005;

            let mut shore = 0.0;
            let mut depth = 0.0;
            if chunk.terrain == Terrain::Water {
                let mut land_neighbors = 0.0;
                let mut total_neighbors = 0.0;
                for oy in -1..=1 {
                    for ox in -1..=1 {
                        if ox == 0 && oy == 0 { continue; }
                        total_neighbors += 1.0;
                        if chunk_at(x + ox, y + oy).map(|c| c.terrain) != Some(Terrain::Water) {
                            land_neighbors += 1.0;
                        }
                    }
                }
                shore = if total_neighbors > 0.0 { land_neighbors / total_neighbors } else { 0.0 };
                depth = ((0.38 - chunk.elevation) / 0.38).clamp(0.0, 1.0);
            }

            // --- BASE TEXTURE with per-tile brightness variation ---
            let var = if chunk.terrain == Terrain::Water {
                0.99 + hash01(x, y, seed ^ 0xF00F) * 0.02
            } else {
                0.98 + hash01(x, y, seed ^ 0xF00F) * 0.04
            };
            let img = terrain_image(textures, chunk.terrain, chunk.biome);
            let terrain_tint = biome_tint(chunk.terrain, chunk.biome, chunk.moisture, chunk.heat);

            // ApplyOreMask: blend ore color INTO terrain tint (no separate z-fighting sprites)
            // strength varies per tile for natural patchiness
            let final_tint = if chunk.terrain == Terrain::Water {
                water_color(depth, shore)
            } else if let Some(ore) = chunk.ore {
                let ore_col = ore_tint(ore);
                let jitter = hash01(x + 5, y + 5, seed ^ 0xBAD0);
                let strength = (0.20 + chunk.ore_richness * 0.45 + jitter * 0.12).clamp(0.18, 0.74);
                mix_color(terrain_tint, ore_col, strength)
            } else {
                terrain_tint
            };

            let tr = (final_tint.to_srgba().red   * var).min(1.0);
            let tg = (final_tint.to_srgba().green * var).min(1.0);
            let tb = (final_tint.to_srgba().blue  * var).min(1.0);
            let mut base = Sprite::from_image(img);
            base.custom_size = Some(tile_size);
            base.color = Color::srgb(tr, tg, tb);
            let mut ec = commands.spawn((base, Transform::from_xyz(sx, sy, z)));
            if chunk.terrain == Terrain::Water {
                ec.insert(WaterTile {
                    phase: hash01(x, y, seed ^ 0x7777) * std::f32::consts::TAU,
                    depth,
                    shore,
                });
                if shore > 0.05 {
                    // soft shoreline foam for better land/water readability
                    commands.spawn((
                        Sprite::from_color(
                            Color::srgba(0.86, 0.92, 0.95, (0.05 + shore * 0.16).clamp(0.0, 0.22)),
                            tile_size,
                        ),
                        Transform::from_xyz(sx, sy, z + 0.0025),
                    ));
                }
            }

            // Ore overlay: small dithered texture patches on top (complement to tint blend)
            if let Some(ore) = chunk.ore {
                let ore_tex = match ore {
                    Ore::Iron   => textures.iron.clone(),
                    Ore::Copper => textures.copper.clone(),
                    Ore::Coal   => textures.coal.clone(),
                };
                let ore_col = ore_tint(ore);
                let count = if chunk.ore_richness > 0.72 {
                    3u32
                } else if chunk.ore_richness > 0.44 {
                    2u32
                } else {
                    1u32
                };
                for i in 0..count {
                    let nx = hash01(x + i as i32 * 3, y,            seed ^ (0xF001 + i * 13)) - 0.5;
                    let ny = hash01(x,                 y + i as i32 * 3, seed ^ (0xF002 + i * 17)) - 0.5;
                    let sc = 0.09 + chunk.ore_richness * 0.13 + hash01(x + i as i32, y + i as i32 * 2, seed ^ 0xF005) * 0.06;
                    let mut spr = Sprite::from_image(ore_tex.clone());
                    spr.custom_size = Some(Vec2::new(tw * sc, th * sc));
                    spr.color = ore_col.with_alpha((0.40 + chunk.ore_richness * 0.36).clamp(0.35, 0.82));
                    commands.spawn((spr, Transform::from_xyz(
                        sx + nx * tw * 0.72,
                        sy + ny * th * 0.72,
                        z + 0.003 + i as f32 * 0.00005,
                    )));
                }
            }

            // --- ELEVATION SHADE (subtle) ---
            if chunk.terrain != Terrain::Water && chunk.elevation < 0.35 {
                let shade_a = (0.35 - chunk.elevation) * 0.22;
                commands.spawn((
                    Sprite::from_color(Color::srgba(0.0, 0.0, 0.0, shade_a), tile_size),
                    Transform::from_xyz(sx, sy, z + 0.001),
                ));
            }

            // --- CLIFF SHADOW + terrain border ---
            if chunk.terrain != Terrain::Water {
                if let Some(south) = chunk_at(x, y - 1) {
                    let drop = chunk.elevation - south.elevation;
                    if drop > 0.07 {
                        let a = (drop * 1.4).clamp(0.0, 0.45);
                        commands.spawn((
                            Sprite::from_color(Color::srgba(0.0, 0.0, 0.0, a),
                                Vec2::new(tw, 3.0)),
                            Transform::from_xyz(sx, sy - th * 0.5 - 1.5, z + 0.002),
                        ));
                    }
                }
                if chunk_at(x, y - 1).map(|c| c.terrain) != Some(chunk.terrain) {
                    commands.spawn((
                        Sprite::from_color(Color::srgba(0.0, 0.0, 0.0, 0.20),
                            Vec2::new(tw, 1.5)),
                        Transform::from_xyz(sx, sy - th * 0.5 - 0.5, z + 0.0018),
                    ));
                }
            }

            // --- TREES: recognizable canopy dots with highlight ---
            if chunk.tree_density > 0.62 && chunk.terrain != Terrain::Water {
                let tree_col = tree_color(chunk.biome, chunk.moisture);
                let count = if chunk.tree_density > 0.82 { 3u32 } else if chunk.tree_density > 0.70 { 2u32 } else { 1u32 };
                for t in 0..count {
                    let hx = hash01(x + t as i32, y,         seed ^ (0xBEEF + t * 7)) - 0.5;
                    let hy = hash01(x, y + t as i32,         seed ^ (0xCAFE + t * 5)) - 0.5;
                    if hash01(x + t as i32, y + t as i32, seed ^ 0xDEAD) > chunk.tree_density { continue; }
                    let cw = tw * (0.18 + hash01(x + t as i32, y + 77, seed ^ 0xCE01) * 0.10);
                    let ch = cw * 0.75;
                    let px = sx + hx * tw * 0.68;
                    let py = sy + hy * th * 0.68;
                    // drop shadow
                    commands.spawn((
                        Sprite::from_color(Color::srgba(0.0, 0.0, 0.0, 0.14),
                            Vec2::new(cw * 1.15, ch * 0.50)),
                        Transform::from_xyz(px + cw * 0.12, py - ch * 0.28, z + 0.0038 + t as f32 * 0.0001),
                    ));
                    // canopy
                    commands.spawn((
                        Sprite::from_color(tree_col, Vec2::new(cw, ch)),
                        Transform::from_xyz(px, py, z + 0.004 + t as f32 * 0.0001),
                    ));
                    // highlight
                    let tc = tree_col.to_srgba();
                    let hcol = Color::srgba(
                        (tc.red   + 0.18).min(1.0),
                        (tc.green + 0.22).min(1.0),
                        (tc.blue  + 0.10).min(1.0),
                        0.60,
                    );
                    commands.spawn((
                        Sprite::from_color(hcol, Vec2::new(cw * 0.45, ch * 0.45)),
                        Transform::from_xyz(px - cw * 0.08, py + ch * 0.12, z + 0.0041 + t as f32 * 0.0001),
                    ));
                }
            }

            // --- LANDMARK MARKER ---
            if let Some(lm) = chunk.landmark {
                let (col, glow) = landmark_colors(lm);
                commands.spawn((
                    Sprite::from_color(glow.with_alpha(0.18), Vec2::new(tw * 0.70, th * 0.70)),
                    Transform::from_xyz(sx, sy, z + 0.005),
                ));
                commands.spawn((
                    Sprite::from_color(col, Vec2::new(tw * 0.20, th * 0.24)),
                    Transform::from_xyz(sx, sy + th * 0.05, z + 0.006),
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

// Factorio-inspired tints: multiply texture by these to get correct biome feel
fn biome_tint(terrain: Terrain, biome: Biome, moisture: f32, heat: f32) -> Color {
    match terrain {
        Terrain::Water => Color::srgb(0.70, 0.88, 1.00), // blue water tint
        Terrain::Lowland => match biome {
            Biome::Frozen    => Color::srgb(0.85, 0.92, 0.98),
            Biome::Desert    => Color::srgb(0.96, 0.88, 0.64),
            Biome::Wetland   => Color::srgb(0.68, 0.90, 0.62),
            Biome::Volcanic  => Color::srgb(0.78, 0.70, 0.64),
            Biome::Temperate => Color::srgb(0.72, 0.90, 0.58),
        },
        Terrain::Midland => match biome {
            Biome::Frozen    => Color::srgb(0.82, 0.90, 0.95),
            Biome::Desert    => Color::srgb(0.88, 0.80, 0.58),
            Biome::Wetland   => Color::srgb(0.62, 0.84, 0.60),
            Biome::Volcanic  => Color::srgb(0.72, 0.64, 0.58),
            Biome::Temperate => {
                let g = (0.76 + moisture * 0.14).min(0.92);
                Color::srgb(0.62 + heat * 0.08, g, 0.50)
            }
        },
        Terrain::Highland => match biome {
            Biome::Frozen    => Color::srgb(0.90, 0.94, 1.00),
            Biome::Desert    => Color::srgb(0.80, 0.72, 0.52),
            _                => Color::srgb(0.72, 0.76, 0.68),
        },
        Terrain::Mountain => Color::srgb(0.78, 0.78, 0.80),
    }
}

fn ore_tint(ore: Ore) -> Color {
    match ore {
        Ore::Iron   => Color::srgb(0.82, 0.86, 0.90),   // metallic gray-blue
        Ore::Copper => Color::srgb(0.88, 0.52, 0.22),   // distinct orange-brown
        Ore::Coal   => Color::srgb(0.18, 0.18, 0.22),   // near-black
    }
}

fn water_color(depth: f32, shore: f32) -> Color {
    let shallow = Color::srgb(0.17, 0.43, 0.58);
    let deep = Color::srgb(0.05, 0.20, 0.35);
    let shore_tint = Color::srgb(0.34, 0.56, 0.60);
    let base = mix_color(shallow, deep, depth.clamp(0.0, 1.0));
    mix_color(base, shore_tint, (shore * 0.35).clamp(0.0, 0.35))
}

/// Linear blend: Mix(base, ore, strength) — same as Color Mix() from C# pseudocode
fn mix_color(base: Color, overlay: Color, strength: f32) -> Color {
    let b = base.to_srgba();
    let o = overlay.to_srgba();
    let s = strength.clamp(0.0, 1.0);
    Color::srgb(
        (b.red   * (1.0 - s) + o.red   * s).min(1.0),
        (b.green * (1.0 - s) + o.green * s).min(1.0),
        (b.blue  * (1.0 - s) + o.blue  * s).min(1.0),
    )
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

    for y in 0..height {
        for x in 0..width {
            // Normalized [0,1] coords for noise sampling
            let fx = x as f32 / width as f32;
            let fy = y as f32 / height as f32;

            // --- ELEVATION: pure noise, no island mask ---
            let base = fbm(fx * 3.8 + 0.5, fy * 3.8 + 1.3, seed ^ 0x1101, 5, 2.1, 0.52);
            let detail = fbm(fx * 8.2 - 4.0, fy * 8.2 + 2.7, seed ^ 0x2202, 3, 2.0, 0.48);
            let elevation = clamp01(base * 0.78 + detail * 0.22);

            // --- MOISTURE ---
            let wet = fbm(fx * 4.5 + 7.0, fy * 4.5 - 3.0, seed ^ 0x3303, 4, 2.0, 0.54);
            let moisture = clamp01(wet * 0.72 + (1.0 - elevation) * 0.28);

            // --- HEAT ---
            let heat_noise = fbm(fx * 3.1 - 9.0, fy * 3.1 + 5.0, seed ^ 0x4404, 3, 2.0, 0.50);
            let temp = clamp01(heat_noise * 0.65 + 0.35 - elevation * 0.25);

            // --- TERRAIN ---
            let terrain = if elevation < 0.38 {
                Terrain::Water
            } else if elevation < 0.52 {
                Terrain::Lowland
            } else if elevation < 0.68 {
                Terrain::Midland
            } else if elevation < 0.84 {
                Terrain::Highland
            } else {
                Terrain::Mountain
            };

            // --- BIOME ---
            let biome = if terrain == Terrain::Water {
                if temp < 0.28 { Biome::Frozen } else { Biome::Wetland }
            } else if temp < 0.26 {
                Biome::Frozen
            } else if moisture < 0.25 && temp > 0.58 {
                Biome::Desert
            } else if moisture > 0.68 {
                Biome::Wetland
            } else if temp > 0.74 && moisture < 0.44 {
                Biome::Volcanic
            } else {
                Biome::Temperate
            };

            // --- ORES: Factorio-style round patches ---
            let (ore, ore_richness) = if terrain == Terrain::Water || terrain == Terrain::Mountain {
                (None, 0.0)
            } else {
                // Each ore type has a patch noise — high value = dense patch center
                let iron_n   = fbm(fx * 9.5 + 3.1,  fy * 9.5 - 1.7, seed ^ 0xA110, 2, 2.0, 0.5);
                let copper_n = fbm(fx * 8.0 - 7.3,  fy * 8.0 + 4.2, seed ^ 0xB220, 2, 2.0, 0.5);
                let coal_n   = fbm(fx * 10.5 + 11.0, fy * 10.5 - 8.0, seed ^ 0xC330, 2, 2.0, 0.5);

                // Bias based on terrain/biome to give Factorio-like distribution
                let iron_score = iron_n
                    + if terrain == Terrain::Highland { 0.10 } else { 0.0 }
                    + if biome == Biome::Frozen { 0.06 } else { 0.0 };
                let copper_score = copper_n
                    + if biome == Biome::Desert || biome == Biome::Volcanic { 0.10 } else { 0.0 };
                let coal_score = coal_n
                    + if moisture > 0.55 { 0.08 } else { 0.0 };

                let threshold = 0.84; // tight threshold = small dense patches like Factorio
                if iron_score > threshold && iron_score >= copper_score && iron_score >= coal_score {
                    (
                        Some(Ore::Iron),
                        ((iron_score - threshold) / 0.26).clamp(0.0, 1.0),
                    )
                } else if copper_score > threshold && copper_score >= coal_score {
                    (
                        Some(Ore::Copper),
                        ((copper_score - threshold) / 0.26).clamp(0.0, 1.0),
                    )
                } else if coal_score > threshold {
                    (
                        Some(Ore::Coal),
                        ((coal_score - threshold) / 0.26).clamp(0.0, 1.0),
                    )
                } else {
                    (None, 0.0)
                }
            };

            // --- TREE DENSITY ---
            let tree_density = if terrain == Terrain::Water || terrain == Terrain::Mountain {
                0.0
            } else {
                let td = fbm(fx * 6.0 + 21.0, fy * 6.0 - 13.0, seed ^ 0xE5E5, 3, 2.0, 0.5);
                let mut d = td * moisture * 1.1;
                if terrain == Terrain::Highland { d *= 0.5; }
                if biome == Biome::Desert || biome == Biome::Volcanic { d *= 0.12; }
                if biome == Biome::Wetland { d *= 1.2; }
                if ore.is_some() { d *= 0.25; } // less trees on ore patches
                d.clamp(0.0, 1.0)
            };

            chunks.push(Chunk {
                terrain,
                biome,
                ore,
                ore_richness,
                landmark: None,
                elevation,
                moisture,
                heat: temp,
                tree_density,
            });
        }
    }

    // --- LANDMARKS: one per LANDMARK_REGION x LANDMARK_REGION area ---
    for ry in (0..height).step_by(LANDMARK_REGION as usize) {
        for rx in (0..width).step_by(LANDMARK_REGION as usize) {
            let mut best_idx: Option<usize> = None;
            let mut best_score = 0.0f32;
            for y in ry..(ry + LANDMARK_REGION).min(height) {
                for x in rx..(rx + LANDMARK_REGION).min(width) {
                    let idx = (y * width + x) as usize;
                    let c = chunks[idx];
                    if c.terrain == Terrain::Water || c.ore.is_some() { continue; }
                    let marker = fbm(x as f32 * 0.28, y as f32 * 0.28, seed ^ 0xD00D, 2, 2.0, 0.5);
                    let score = marker * 0.6 + c.elevation * 0.2 + c.moisture * 0.1 + c.heat * 0.1;
                    if score > best_score { best_score = score; best_idx = Some(idx); }
                }
            }
            if let Some(idx) = best_idx.filter(|_| best_score > 0.60) {
                let c = chunks[idx];
                chunks[idx].landmark = Some(match c.biome {
                    Biome::Wetland   => Landmark::CrystalLake,
                    Biome::Volcanic  => Landmark::GeothermalVent,
                    Biome::Desert    => Landmark::ImpactCrater,
                    Biome::Frozen    => Landmark::AncientRuins,
                    Biome::Temperate => if c.elevation > 0.7 { Landmark::AncientRuins } else { Landmark::CrystalLake },
                });
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
    rand::random::<u32>()
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
        if c.terrain == Terrain::Water { water += 1; }
        match c.ore {
            Some(Ore::Iron)   => iron += 1,
            Some(Ore::Copper) => copper += 1,
            Some(Ore::Coal)   => coal += 1,
            None => {}
        }
        if c.landmark.is_some() { landmarks += 1; }
    }
    let total = world.chunks.len().max(1) as f32;
    let water_ratio = water as f32 / total;
    let mut score = 0.0f32;
    // prefer 15-35% water (lakes+rivers feel)
    score += 1.0 - (water_ratio - 0.25).abs() * 3.0;
    // reward good ore spread
    score += ((iron.min(10) + copper.min(10) + coal.min(10)) as f32 / 30.0).min(1.0) * 1.5;
    // reward landmarks
    score += ((landmarks as f32) / 15.0).min(1.0) * 0.8;
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
