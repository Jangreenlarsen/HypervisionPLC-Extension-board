#pragma once

#include <cstddef>
#include "provisioning_cli.h"

// Seriel provisioning-CLI (EXPANSION_BOARD_DESIGN.md §3.4/§3.4.1) — læser
// linjer fra Serial, kalder ind i lib/provisioning_cli/'s hardware-uafhængige
// parser, og udfører den rigtige handling (WiFi-forbindelsesforsøg) for
// "connect". Kaldes fra main.cpp's setup()/loop().
void provisioning_begin();
void provisioning_poll();

// v0.32.0: holder CLI'ens arbejdskopi (g_state) i sync, når hostnamet
// ændres udefra (POST /api/hostname) — ellers ville et senere 'save' i den
// serielle CLI skrive det gamle navn tilbage (samme klasse som v0.29.1).
void provisioning_sync_hostname(const char *hostname);
// v0.34.0: samme sync for plc ip og syslog-modtagere sat via REST
void provisioning_sync_plc_ip(const char *ip);
void provisioning_sync_syslog(const mb_syslog_target_t *targets, size_t count);
