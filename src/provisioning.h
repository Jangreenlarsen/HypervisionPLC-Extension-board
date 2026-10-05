#pragma once

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
