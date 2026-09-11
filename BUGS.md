# Bugs

Format: `[status] vX.X.X — beskrivelse`
Status: `open` | `investigating` | `fixed`

---

- [fixed] v0.4.0 — `MB_PROV_MSG_MAX_LEN` (96 bytes) var for lille til `help`-kommandoens fulde svartekst (159 tegn) — `snprintf` afkortede beskeden midt i en sætning (`show`/`connect`/`factory-reset confirm`/`help` selv manglede i output). Ikke fanget af den oprindelige unit-test, som kun tjekkede `strlen(msg) > 0` i stedet for det faktiske indhold. Fundet ved manuel test mod fysisk hardware (`test_serial_cli.py`-script over rigtig seriel forbindelse), IKKE af `pio test -e native` — testen brugte samme (for lille) buffer-konstant som produktionskoden, så den kunne aldrig have opdaget det uanset hvor grundig den ellers var. Rettet: `MB_PROV_MSG_MAX_LEN` hævet til 192, og testen skærpet til at tjekke alle kommandonavne er til stede i `help`-output. Samtidig fundet: `help`-teksten nævnte ikke sig selv som kommando — tilføjet.
