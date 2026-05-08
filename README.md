# Factorio-pt — dokumentacja techniczna

Projekt gry typu automation/strategy w klimacie Factorio, rozszerzony o politykę, socjologię, symulację planet i generację tekstur przez AI.

## Status

**Faza:** preprodukcja MVP (vertical slice)\
**Tryb:** single-player (co-op/P2P później)\
**Platformy:** Windows + Linux\
**Cel wydajnościowy MVP:** stabilne 60 FPS

## Aktualny kamień milowy

**M7: runtime visual pass (zakończony)**  
Mamy działające okno gry z renderingiem pseudo-izometrycznym i AI-like teksturami:

- C++ runtime (`runtime/`) z pętlą ~60 FPS,
- pseudo-izometryczny render świata (height levels + side shading),
- AI-like generator tekstur materiałowych (deterministyczny per seed uruchomienia),
- wyraźne sygnatury surowców (iron/copper/coal),
- HUD runtime (metryki symulacji, kontrolki, status),
- integracja C++ <-> Rust (`sim_bootstrap`, `sim_tick`, `sim_set_policy`, `sim_generate_planet`, `sim_generate_system`).

**Następny milestone (M8):** gameplay layer (wydobycie/crafting/placement) na obecnym rendererze.

## M8 gameplay layer (w toku)

Do runtime został dodany pierwszy grywalny rdzeń pętli:

- `E` — ręczne wydobycie surowca z pola gracza,
- `F` — prosty smelting (`2x iron ore + 1x coal -> 1x iron plate`),
- `B` — postawienie/usunięcie extractora na złożu,
- extractory wydobywają automatycznie w czasie (tick runtime),
- HUD pokazuje inwentarz, liczbę maszyn i komunikaty akcji.
- runtime został podzielony na moduły (`world`, `gameplay`, `render`, `runtime_state`) dla porządku kodu.
- render mapy został uspokojony: terrain-first + kontekstowe patche surowców (mniej chaosu wizualnego).

## Struktura repo

```text
runtime/        # C++ runtime/silnik 2.5D
sim-rust/       # Rust: symulacja i procgen
tools-csharp/   # C#: narzędzia i pipeline
docs/           # dokumentacja modułów
scripts/        # skrypty developerskie
```

## Build lokalny

PowerShell:

```powershell
.\scripts\build.ps1 -Configuration Debug
```

Skrypt używa toolchainu MinGW/Ninja (`C:\msys64\ucrt64\bin`) oraz Rust target `x86_64-pc-windows-gnu`.

## Runtime release (Windows)

PowerShell:

```powershell
.\scripts\package-runtime-release.ps1
```

Skrypt buduje tylko runtime + bibliotekę symulacji Rust i tworzy paczkę:

- `dist\runtime-win64\` (pliki do uruchomienia),
- `dist\factorio-pt-runtime-win64.zip` (artefakt pod GitHub Release).

## Runtime visual pass (M7)

Co jest już zaimplementowane:

- render pseudo-2.5D (izometryczna kompozycja kafli),
- wysokości terenu (ekstruzja i cieniowanie boków),
- animacja wody i mgła zanieczyszczeń zależna od `pollution`,
- generator stylu świata per run (`R` = nowy seed stylu),
- HUD z paskami metryk (`stability`, `pollution`, `wage`, `tax`),
- płynne sterowanie i kamera (`WASD` + `IJKL`).

Szybkie uruchomienie:

```powershell
.\scripts\build.ps1 -Configuration Debug
Copy-Item ".\sim-rust\target\x86_64-pc-windows-gnu\debug\factorio_pt_sim.dll" ".\build\runtime\" -Force
.\build\runtime\factorio_pt_runtime.exe
```

## MVP (v0.1)

1. 1 układ planetarny.
2. 2 grywalne planety.
3. Pętla: wydobycie -> crafting -> automatyzacja -> presja społeczna/polityczna -> decyzje -> skutki środowiskowe.
4. Podstawowa polityka: frakcje, klasy społeczne, stabilność państwa.
5. AI generuje tekstury lokalnie.

## Stack technologiczny (podział odpowiedzialności)

| Warstwa | Technologia | Odpowiedzialność |
|---|---|---|
| Runtime/silnik 2.5D | C++ | render, pętla gry, input, streaming świata, integracja systemów |
| Symulacja | Rust | ekonomia, polityka, procgen planet/biomów, reguły systemowe |
| Narzędzia i pipeline | C# | tooling developerskie, import/eksport danych, utility do contentu |

**Zasada:** brak duplikacji logiki gameplay między językami. Każdy moduł ma jednego właściciela technologicznego.

## Generacja świata

- 10 planet core jako wzorce.
- 990 planet proceduralnych opartych o core.
- 1 układ = 10 planet.
- Każdy układ ma unikalny charakter biomów i minimum 1 obszar unikalny na planetę.

## AI i assety

- Domyślnie: lokalna generacja tekstur przy starcie świata i przy dużych zmianach środowiska.
- Tryb adaptacyjny:
  - mocny sprzęt: preload całego układu,
  - słabszy sprzęt: tylko aktywna planeta + streaming.
- Fallback: API dla słabszego sprzętu.

## Reguła rozpoznawalności surowców

Każdy surowiec musi pozostać czytelny dla gracza (np. żelazo ma dalej wyglądać jak żelazo). Dla MVP przyjmujemy walidację mieszaną:

1. reguły kształtu/koloru/materialu,
2. automatyczna klasyfikacja referencyjna,
3. szybki test użytkownika.

## Architektura dokumentacji

Twoja propozycja **pasuje i przyjmujemy ją jako standard**:

- **Moduł -> dokumentacja modułu**
- **Skończony moduł -> wpis i aktualizacja w głównym README**
- **Gigantyczny moduł -> osobne README modułu + link z głównego README**

Szczegóły i szablony: **[docs/README.md](docs/README.md)**

## Moduły (indeks)

- World Generation (duży moduł): [docs/modules/world-generation/README.md](docs/modules/world-generation/README.md)
- Runtime 2.5D: [docs/modules/runtime/README.md](docs/modules/runtime/README.md)
- Simulation Core: [docs/modules/simulation-core/README.md](docs/modules/simulation-core/README.md)
- Tooling Pipeline: [docs/modules/tooling-pipeline/README.md](docs/modules/tooling-pipeline/README.md)

