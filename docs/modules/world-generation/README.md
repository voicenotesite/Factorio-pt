# World Generation (duży moduł)

## Cel

Generowanie układów i planet: 10 planet core + planety proceduralne, z zachowaniem unikalności biomów i czytelności gameplayowej.

## Zakres MVP

- 1 układ, 2 planety grywalne.
- Fundament pod model: 1 układ = 10 planet.
- Reguły biomów i co najmniej 1 miejsce unikalne na planetę.

## Właściciel technologiczny

- Główna logika: **Rust** (procgen + reguły).
- Integracja runtime: **C++**.
- Narzędzia authoringu danych: **C#**.

## Struktura (robocza)

1. Generator seedów i wariantów planet.
2. Generator biomów.
3. Generator punktów unikalnych.
4. Walidacja grywalności i czytelności map.

## Kontrakty danych (wstępne)

- Input: seed układu, profil planety core, parametry klimatu.
- Output: opis planety (biomy, zasoby, punkty unikalne, metadane).

## Status

**planned**

