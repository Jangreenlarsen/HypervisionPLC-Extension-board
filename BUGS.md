# Bugs

Format: `[status] vX.X.X — beskrivelse`
Status: `open` | `investigating` | `fixed`

---

- [fixed] v0.4.0 — `MB_PROV_MSG_MAX_LEN` (96 bytes) var for lille til `help`-kommandoens fulde svartekst (159 tegn) — `snprintf` afkortede beskeden midt i en sætning (`show`/`connect`/`factory-reset confirm`/`help` selv manglede i output). Ikke fanget af den oprindelige unit-test, som kun tjekkede `strlen(msg) > 0` i stedet for det faktiske indhold. Fundet ved manuel test mod fysisk hardware (`test_serial_cli.py`-script over rigtig seriel forbindelse), IKKE af `pio test -e native` — testen brugte samme (for lille) buffer-konstant som produktionskoden, så den kunne aldrig have opdaget det uanset hvor grundig den ellers var. Rettet: `MB_PROV_MSG_MAX_LEN` hævet til 192, og testen skærpet til at tjekke alle kommandonavne er til stede i `help`-output. Samtidig fundet: `help`-teksten nævnte ikke sig selv som kommando — tilføjet.
- [fixed] v0.6.0 — `show`/`status` viste ikke reelt WiFi-forbindelsesstatus (Jan: "viser ikke connect status") — `status`s `WiFi.status()`-switch faldt til et uinformativt "ukendt/fejl" for almindelige før-forbindelse-tilstande (fx `WL_NO_SHIELD` ved boot), og `show` (hardware-uafhængig, `lib/provisioning_cli`) havde slet ingen adgang til live WiFi-data. Rettet: ny `wifi_status_text()`/`print_wifi_connection_status()` i `src/provisioning.cpp` dækker alle reelle `wl_status_t`-værdier eksplicit, og kaldes efter BÅDE `show`- og `status`-output.
- [fixed] v0.6.0 — Første boot nogensinde (fabriksnyt/lige-factory-reset'et board) printede en ESP-IDF-fejl-log-linje (`Preferences.cpp: nvs_open failed: NOT_FOUND`), fordi `config_begin()` åbnede NVS-namespacet read-only før det var oprettet. Ufarligt (fallback til defaults virkede korrekt), men skræmmende at se. Rettet: `config_begin()` åbner nu read-write (opretter namespacet stille, skriver intet før et eksplicit `save`/`connect`).
