# Factorio-pt — dokumentacja techniczna

Projekt gry typu automation/strategy w klimacie Factorio, rozszerzony o politykę, socjologię, symulację planet i generację tekstur przez AI.

## Status

**Faza:** preprodukcja MVP (vertical slice)\
**Tryb:** single-player (co-op/P2P później)\
**Platformy:** Windows + Linux\
**Cel wydajnościowy MVP:** stabilne 60 FPS

## Aktualny kamień milowy

**M1: foundation skeleton (zakończony)**  
Utworzono bazę techniczną:

- C++ runtime (`runtime/`)
- Rust simulation crate (`sim-rust/`)
- C# tools app (`tools-csharp/`)
- wspólny skrypt build (`scripts/build.ps1`)

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

