#[repr(C)]
pub struct SimSnapshot {
    pub stability: f32,
    pub pollution: f32,
}

#[no_mangle]
pub extern "C" fn sim_bootstrap() -> SimSnapshot {
    SimSnapshot {
        stability: 0.75,
        pollution: 0.10,
    }
}

