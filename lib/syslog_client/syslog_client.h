#pragma once

#include <cstddef>
#include <cstdint>

// Hardware-uafhængig RFC 3164 ("BSD syslog")-pakkeformatering, native-
// testbar (se ARCHITECTURE.md) — selve UDP-afsendelsen (kræver WiFi/
// Ethernet) ligger i src/syslog_sender.cpp. v0.26.0 (Jan: "kan vi lave en
// syslog funktion som vi kan sætte et target på som modtager af syslog").

// RFC 3164/5424's "local"-facility-kodeområde (16-23) — de eneste
// facility-værdier der giver mening for et embedded/applikations-device
// som dette (facility 0-15 er reserveret til OS-kernen/standard Unix-
// tjenester, som boardet ikke er). Fast pr. delsystem i firmwaren (se
// kaldsteder i src/), IKKE brugerkonfigurerbart — så en syslog-server kan
// filtrere/route efter oprindelse med almindelige, statiske regler.
enum class mb_syslog_facility_t : uint8_t {
  kLocal0 = 16,
  kLocal1 = 17,
  kLocal2 = 18,
  kLocal3 = 19,
  kLocal4 = 20,
  kLocal5 = 21,
  kLocal6 = 22,
  kLocal7 = 23,
};

// Kanoniske delsystem-tildelinger (§ARCHITECTURE.md-lagene) — én facility
// pr. delsystem, så en installatør kan opsætte routing/filtrering på
// syslog-serveren uden at skulle kende boardets interne kildekode.
constexpr mb_syslog_facility_t MB_SYSLOG_FACILITY_MODBUS = mb_syslog_facility_t::kLocal0;   // kanal A/B, §4.1
constexpr mb_syslog_facility_t MB_SYSLOG_FACILITY_NETWORK = mb_syslog_facility_t::kLocal1;  // WiFi/Ethernet
constexpr mb_syslog_facility_t MB_SYSLOG_FACILITY_REST = mb_syslog_facility_t::kLocal2;     // REST-management-API, §4.2/§4.4
constexpr mb_syslog_facility_t MB_SYSLOG_FACILITY_SYSTEM = mb_syslog_facility_t::kLocal3;   // boot/CLI/OTA/factory-reset

// Genbruger v0.25.0's allerede etablerede 1-8-verbositetsskala
// (`debug modbus ... level <1-8>`) som severity-akse for ALLE syslog-
// beskeder, ikke kun Modbus-debug — niveau 1 = mest kritisk/altid
// interessant, niveau 8 = mest detaljeret/støjende. `severity = level - 1`
// er en bevidst, simpel, bijektiv mapping til RFC 3164's severity 0-7
// (0=Emergency ... 7=Debug) — samme retning (stigende tal = stigende
// verbositet/faldende alvor), ingen oversættelsestabel nødvendig. `level`
// klampes til [1,8] hvis en kaldsted skulle give en værdi udenfor.
uint8_t mb_syslog_severity_from_level(uint8_t level);

// Maks. UDP-payload-størrelse denne builder nogensinde skriver — rigeligt
// til RFC 3164's praktiske grænser og en enkelt Modbus-debug-/hex-dump-linje
// (se src/modbus_channel.cpp), uden at nærme sig UDP's egen ~65KB-grænse.
constexpr size_t MB_SYSLOG_PACKET_MAX_LEN = 512;

// Bygger én RFC 3164-pakke: "<PRI>Mmm dd hh:mm:ss HOSTNAME TAG: MESSAGE".
// Boardet har ingen RTC/NTP — TIMESTAMP er derfor en syntaktisk gyldig, men
// IKKE en reel kalenderdato, pseudo-dato afledt af `uptime_s` (de fleste
// syslog-servere, fx rsyslog, erstatter alligevel headerens tidsstempel med
// egen modtagelsestid når det ikke stemmer overens med "nu" — praksis for
// embedded syslog-klienter uden NTP). PRI = facility*8 + severity(level).
// Returnerer antal skrevne bytes (afkortet, ALDRIG > out_capacity-1, hvis
// den fulde besked ikke kan være der), eller 0 ved ugyldige argumenter.
size_t mb_syslog_build_packet(mb_syslog_facility_t facility, uint8_t level, const char *tag, const char *hostname,
                               uint32_t uptime_s, const char *message, char *out_buf, size_t out_capacity);
