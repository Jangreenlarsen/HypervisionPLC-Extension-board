#pragma once

// Seriel provisioning-CLI (EXPANSION_BOARD_DESIGN.md §3.4/§3.4.1) — læser
// linjer fra Serial, kalder ind i lib/provisioning_cli/'s hardware-uafhængige
// parser, og udfører den rigtige handling (WiFi-forbindelsesforsøg) for
// "connect". Kaldes fra main.cpp's setup()/loop().
void provisioning_begin();
void provisioning_poll();
