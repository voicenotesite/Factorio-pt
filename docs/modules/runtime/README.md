# Runtime 2.5D

## Cel

Uruchamianie gry, pętla frame, rendering 2.5D, input i streaming danych świata.

## Zakres MVP

- start aplikacji i pętla runtime,
- podpięcie danych symulacji,
- fundament pod 60 FPS.

## Aktualna implementacja (M8/M9 gameplay + factorio-like pass)

- okno Win32 + software rendering (GDI + backbuffer DIB),
- top-down grid (Factorio-like), kafle kwadratowe + proste nakładki (belty/items),
- duży pass grafiki/FPS:
  - adaptacyjna jakość renderu (HIGH/BALANCED/PERF-CRITICAL) zależna od FPS,
  - szybsze prymitywy pikselowe (clipped fill dla prostokątów/okręgów, mniej kosztownego `PutPixel`),
  - bardziej organiczne patch'e rud (maska radialna + noise),
  - mocniejsze odszumienie siatki kafli (flip/offset/transpose tekstur + blend narożników),
  - tańsze shoreline i tańszy haze przy niskim FPS (checkerboard w trybie krytycznym),
- AI-like generator tekstur materiałowych oparty o latent seed,
- opcjonalny loader zewnętrznego atlasu tekstur (`assets/generated/runtime_texture_atlas.bin`),
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

- `W/A/S/D` — płynny ruch gracza
- `Shift` — sprint
- kamera płynnie podąża za graczem (smooth follow)
- `E` — ręczne wydobycie
- `F` — wytop `iron plate`
- `B` — postaw/usun extractor
- `N` — postaw/usun furnace
- `T` — postaw/usun belt
- `C` — zetnij drzewo (wood)
- `R` — obrót kierunku budowy (belty/wyjścia maszyn)
- `P` — nowy seed stylu świata (wizualny reroll)
- `H` — przełącz HUD (hidden/compact/debug)
- `F7` — auto governor jakości/FPS (ON/OFF)
- `F8` — render resolution: fixed (szybciej) ↔ native (ostrzej, wolniej)
- HUD pokazuje aktualny profil jakości renderu: `HIGH`, `BALANCED` lub `PERF-CRITICAL`
- `Q` / `Esc` — wyjście

## M8 gameplay core (pierwszy etap)

- inwentarz runtime: iron/copper/coal + iron plates,
- wydobycie ręczne z pola gracza,
- smelting iron plates,
- proste maszyny wydobywcze (extractor) działające w czasie,
- HUD z widokiem zasobów, maszyn i statusu akcji.

## Refactor kodu (anti-chaos)

Runtime został podzielony na moduły:

- `runtime_state.h` — wspólny stan i typy runtime,
- `world.*` — generacja świata, tile access, status i seed stylu,
- `gameplay.*` — akcje gracza i logika maszyn,
- `render.*` — rendering mapy/HUD i obsługa backbuffera,
- `main.cpp` — bootstrap DLL + pętla aplikacji.

Dzięki temu kolejne iteracje M8/M9 są prostsze i mniej ryzykowne.

## Czytelniejsza mapa i factorio-like look

- render jest teraz **terrain-first** (biom najpierw, surowiec jako patch),
- mapa jest bardziej płaska (2.5D), z okazjonalnymi górami,
- surowce tworzą bardziej spójne klastry i są czytelniejsze.

## Pipeline tekstur z datasetu referencyjnego

Runtime wspiera dwa tryby:

1. **Procedural fallback** (bez plików wejściowych) — obecny generator C++.
2. **External atlas mode** — atlas wygenerowany skryptem Python i ładowany automatycznie przy starcie.

Przygotowanie atlasu (osobny trainer AI):

```powershell
dotnet run --project .\tools-csharp\ai-trainer\FactorioPt.AiTrainer.csproj -- `
  --dataset-root .\assets\style-dataset `
  --output .\assets\generated\runtime_texture_atlas.bin `
  --variants 8
```

Generowanie **seamless HQ tiles** (32/64, teren + rudy):

```powershell
dotnet run --project .\tools-csharp\ai-trainer\FactorioPt.AiTrainer.csproj -- `
  --export-hq-tiles .\assets\generated\hq-tiles `
  --tile-sizes 64 `
  --tile-variants 12 `
  --style-shots-dir .\ `
  --style-blend 0.32 `
  --seed 20260508
```

Uruchomienie:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-runtime-release.ps1
.\dist\runtime-win64\factorio_pt_runtime.exe
```

## Build i run

```powershell
.\scripts\build.ps1 -Configuration Debug
.\build\runtime\factorio_pt_runtime.exe
```

## Właściciel technologiczny

- Główna technologia: **C++**
- Integracje: Rust (symulacja), C# (asset/data tooling output)

## Status

**in progress** (M7 complete + M8 gameplay core; kolejny etap: sprite atlas + obiekty produkcyjne)

