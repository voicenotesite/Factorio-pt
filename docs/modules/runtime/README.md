# Runtime 2.5D

## Cel

Uruchamianie gry, pętla frame, rendering 2.5D, input i streaming danych świata.

## Zakres MVP

- start aplikacji i pętla runtime,
- podpięcie danych symulacji,
- fundament pod 60 FPS.

## Aktualna implementacja (M7 visual pass)

- okno Win32 + software rendering (GDI + backbuffer DIB),
- pseudo-izometryczny świat (tile projection, wysokości, side shading),
- AI-like generator tekstur materiałowych oparty o latent seed,
- cache tekstur per `VisualKind` + wariant,
- sygnatury rozpoznawalności surowców:
  - iron: chłodne żyły,
  - copper: ciepły odcień + oksydacja,
  - coal: ciemny materiał z pęknięciami,
- HUD runtime (metryki, paski stanu, skróty sterowania),
- integracja live z Rust sym:
  - `sim_tick(1/60)`,
  - `sim_set_policy`,
  - `sim_generate_planet`,
  - `sim_generate_system`.

## Sterowanie

- `W/A/S/D` — ruch gracza
- `I/J/K/L` — kamera
- `R` — nowy seed stylu świata (wizualny reroll)
- `E` — ręczne wydobycie
- `F` — wytop `iron plate`
- `B` — postaw/usun extractor
- `Q` / `Esc` — wyjście

## M8 gameplay core (pierwszy etap)

- inwentarz runtime: iron/copper/coal + iron plates,
- wydobycie ręczne z pola gracza,
- smelting iron plates,
- proste maszyny wydobywcze (extractor) działające w czasie,
- HUD z widokiem zasobów, maszyn i statusu akcji.

## Build i run

```powershell
.\scripts\build.ps1 -Configuration Debug
Copy-Item ".\sim-rust\target\x86_64-pc-windows-gnu\debug\factorio_pt_sim.dll" ".\build\runtime\" -Force
.\build\runtime\factorio_pt_runtime.exe
```

## Właściciel technologiczny

- Główna technologia: **C++**
- Integracje: Rust (symulacja), C# (asset/data tooling output)

## Status

**in progress** (M7 complete + M8 gameplay core; kolejny etap: sprite atlas + obiekty produkcyjne)

