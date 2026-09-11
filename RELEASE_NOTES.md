# Release Notes

---

## v0.1.0 — 2026-09-11 — Projektinitialisering

Projektstruktur og dokumentation oprettet. Design-dokumentet ([EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md)) er læst og lagt til grund for `ARCHITECTURE.md` og `FEATURES.md`. Ingen firmware-kode skrevet endnu.

**Næste skridt**: Fase 1 (Hardware-bring-up) — anskaf en ESP32 gateway-MCU + én UART-expander-chip (MAX14830 eller SC16IS752) og verificér SPI-kommunikation samt én RS485-kanal mod en kendt Modbus RTU-slave, jf. designdokumentets §9. Kræver fysisk hardware og kan ikke udføres af Claude alene.
