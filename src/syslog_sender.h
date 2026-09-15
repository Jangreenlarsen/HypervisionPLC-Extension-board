#pragma once

#include <cstdint>

#include "syslog_client.h"

// ESP32-specifik UDP-afsendelse for lib/syslog_client/'s RFC 3164-pakker —
// v0.26.0 (Jan: "kan vi lave en syslog funktion som vi kan sætte et target
// på som modtager af syslog"). "Fire and forget" (UDP, ingen ACK/retry,
// samme filosofi som syslog-protokollen selv) — en manglende/utilgængelig
// modtager må ALDRIG kunne blokere eller forsinke boardets egentlige drift.

// Indlæser (eller genindlæser efter en "save" der rører syslog-config'en)
// modtager-listen + hostname fra config_get() (src/config.cpp). Kaldes én
// gang ved boot (main.cpp::setup(), EFTER config_begin()) og igen hver gang
// syslog-relevant config persisteres.
void syslog_sender_begin();
void syslog_sender_refresh();

// Sender `message` til hver konfigureret modtager hvis dens `max_level >=
// level` (se lib/syslog_client.h for facility/level-semantik). Ingen
// modtagere konfigureret, eller ingen matcher, er en billig, hurtig no-op.
void syslog_log(mb_syslog_facility_t facility, uint8_t level, const char *message);

// printf-stil bekvemmelighed — bygger `message` i en lille, begrænset lokal
// buffer (§BUGS.md v0.24.0/v0.25.0-lektionen: aldrig en stor stak-buffer i
// et kaldested der selv kan køre på en lille FreeRTOS-task-stak, fx
// channel_task()). Lange beskeder afkortes stille.
void syslog_logf(mb_syslog_facility_t facility, uint8_t level, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
