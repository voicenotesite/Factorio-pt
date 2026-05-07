# Simulation Core

## Cel

Silnik symulacji ekonomii, polityki, społeczeństwa i reguł świata.

## Zakres MVP

- podstawowe wskaźniki (stabilność, zanieczyszczenie),
- reguły wpływu decyzji gracza (płace/podatki),
- kontrakt danych dla runtime.

## Właściciel technologiczny

- Główna technologia: **Rust**
- Integracje: C++ runtime

## Status

**in_progress**

## Aktualnie zaimplementowane

- `sim_bootstrap()`
- `sim_tick(delta_seconds)`
- `sim_set_policy(wage_index, tax_rate)`
- `sim_get_snapshot()`

