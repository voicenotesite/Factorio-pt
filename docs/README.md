# Dokumentacja projektu

Ten katalog zawiera dokumentację techniczną modułów oraz zasady utrzymania dokumentacji.

## Zasada główna

1. **Moduł -> dokumentacja modułu**
2. **Skończony moduł -> update głównego README**
3. **Gigantyczny moduł -> osobne README modułu + link z README głównego**

## Struktura

```text
docs/
  README.md
  modules/
    TEMPLATE.md
    runtime/
      README.md
    simulation-core/
      README.md
    tooling-pipeline/
      README.md
    world-generation/
      README.md
```

## Kiedy moduł jest „gigantyczny”

Tworzymy osobne README modułu, jeśli spełnia co najmniej 1 warunek:

- ma wiele podsystemów (np. procgen + biomy + reguły symulacji + walidacja),
- wymaga osobnych kontraktów danych/API,
- ma własny pipeline lub złożone zależności.

## Minimalny standard dokumentacji modułu

Każdy moduł powinien mieć:

- cel i zakres,
- odpowiedzialność technologii (C++/Rust/C#),
- kontrakty danych (input/output),
- zależności,
- status (planned/in progress/done),
- checklistę integracji.

Szablon: [docs/modules/TEMPLATE.md](modules/TEMPLATE.md)

