#include "modbus_channel.h"

#include <Arduino.h>
#include <cstdarg>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "channel_port.h"
#include "config.h"
#include "syslog_sender.h"
#include "uart_expander.h"
#include "uart_port_native.h"

namespace {

// GPIO-allokering — EXPANSION_BOARD_DESIGN.md §2.0.1, ESP32-WROOM-32 DevKit.
// Disse er FYSISK faste (loddet på boardet) og derfor ikke en del af den
// konfigurerbare §4.2-config — kun baudrate/mode/parity/stop-bits/timeout/
// inter-frame-delay/enabled er det.
constexpr int kChannelATx = 17;
constexpr int kChannelARx = 16;
constexpr int kChannelADir = 27;

constexpr int kChannelBTx = 18;
constexpr int kChannelBRx = 19;
constexpr int kChannelBDir = 25;

// Aktivitets-LED, én pr. kanal (EXPANSION_BOARD_DESIGN.md §2.0.1/§2.2,
// "valgfri diagnostik-LED") — v0.23.1 (Jan: "aktivitet LED for de to kanal
// hvordan opføre de sig"): reserveret siden v0.13.0, men ALDRIG faktisk
// drevet af nogen kode før nu. Tændt for den PRÆCISE varighed af en RTU-
// transaktion (se channel_task() nedenfor), uanset udfald (succes ELLER
// fejl/timeout) — samme "der sker noget her lige nu"-filosofi som en
// almindelig RS485/RS232-adapters TX/RX-LED.
constexpr int kChannelALedPin = 26;
constexpr int kChannelBLedPin = 33;

// Hardware-revision 2026-09-14 (Jan, bekræftet): MODE_SEL er ÉN delt GPIO
// for HELE boardet, ikke længere én pr. kanal — kanal A og B kan derfor
// ALDRIG have forskellig RS232/RS485-mode, kun ét fast valg for hele
// boardet. Frigav GPIO23 (kanal B's tidligere MODE_SEL) til W5500's RST
// (se src/eth_driver.cpp).
//
// Hardware-revision 2026-09-14 (2. ændring samme dag, Jan bekræftet): dette
// er en INPUT, ikke en output — et fysisk jumper/strap SAT VED FREMSTILLING
// (installatøren/fabrikanten vælger RS232 eller RS485 for boardet, én gang,
// ved at forbinde GPIO4 til enten 3.3V eller GND), IKKE noget firmwaren selv
// driver ud fra en gemt config. Firmwaren LÆSER pinden ÉN gang ved boot
// (`modbus_channel_init_all()`) og bruger den udlæste værdi resten af
// levetiden — `mode` er derfor IKKE længere et felt PLC-siden kan sætte via
// `PUT /api/channels/{n}/config` (kun læseværdi, se `GET .../{n}` og
// `GET /api/status`s `board_mode`). `INPUT_PULLUP` sikrer en veldefineret
// default (RS485) hvis pinden mod forventning skulle stå uforbundet under
// bring-up, i stedet for at flyde og læse støj.
constexpr int kBoardModeSelPin = 4;

// Læses ÉN gang ved boot (modbus_channel_init_all()) — se kBoardModeSelPin's
// kommentar. `execute_transaction()` og REST-laget (via
// modbus_channel_get_config()) bruger denne, ALDRIG et gemt/PUT'et felt.
mb_channel_mode_t g_hardware_mode = MB_CHANNEL_MODE_RS485;

// v0.31.0 (Jan: "vi skal bruge et jumper mere til at fortælle at vi har den
// ny chip ombord") — EXP_SEL: jumper til 3,3 V = CJMCU-752 (SC16IS752) er
// monteret → kanal C+D aktive (4 kanaler i alt). GPIO36 er input-only UDEN
// intern pull — boardet SKAL have en ekstern 10 kΩ pull-DOWN til GND
// (GPIO_MAPPING.md). Polariteten er valgt så fejlen falder ud til den sikre
// side: live-målt på et board UDEN modstand læste den flydende pin LAV,
// dvs. "ikke monteret" = 2 kanaler som hidtil (med den omvendte polaritet
// påstod det board fejlagtigt 4 kanaler). Læses ÉN gang ved boot, ligesom
// MODE_SEL. Jumperen er den autoritative kilde: siger den "monteret", men
// chippen svarer ikke på I2C, er kanal C+D stadig aktive — men afviser alle
// transaktioner, så hardwarefejlen er synlig i stedet for skjult.
constexpr int kExpanderSelPin = 36;

size_t g_active_channels = 2;
ModbusExpanderStatus g_expander_status = ModbusExpanderStatus::kNotFitted;

mb_channel_mode_t read_board_mode_sel() {
  pinMode(kBoardModeSelPin, INPUT_PULLUP);
  delayMicroseconds(10);  // lad evt. parasitkapacitans paa linjen naa at settle efter pinMode-skiftet
  return digitalRead(kBoardModeSelPin) == HIGH ? MB_CHANNEL_MODE_RS485 : MB_CHANNEL_MODE_RS232;
}

enum class ChannelRequestType : uint8_t { kTransaction, kReconfigure };

struct ChannelRequest {
  ChannelRequestType type;

  // kTransaction:
  uint8_t slave_id;
  const uint8_t *pdu;
  size_t pdu_len;
  uint8_t *out_pdu;
  size_t *out_pdu_len;
  size_t out_pdu_capacity;

  // kReconfigure:
  mb_channel_config_t new_config;

  mb_error_code_t result;
  SemaphoreHandle_t done;
};

struct ChannelContext {
  const char *name;
  size_t config_index;  // 0=A, 1=B, 2=C, 3=D — index ind i mb_board_config_t::channel[]
  ChannelPort *port;    // v0.31.0: ESP32-UART (A/B) eller SC16IS752 (C/D), se channel_port.h
  mb_channel_config_t config;
  mb_channel_stats_t stats;
  QueueHandle_t queue;
  // v0.25.0 (Jan: "lave en debug som outputer til console") — 0=fra,
  // 1-8=stigende detaljeniveau. Kun læst/skrevet som en enkelt uint8_t —
  // ATOMISK på ESP32 (naturligt alignet, enkelt load/store-instruktion) —
  // derfor ingen kø/lås nødvendig selvom CLI-tasken skriver den mens
  // channel_task() samtidig læser den, jf. modbus_channel_set_debug_level().
  volatile uint8_t debug_level;
};

HardwareSerial g_serialA(1);  // UART-periferi #1 (§2.0: udelukkende brugt her, ikke af CLI'en som ejer UART0)
HardwareSerial g_serialB(2);  // UART-periferi #2

NativeUartPort g_portA(g_serialA, kChannelATx, kChannelARx, kChannelADir, kChannelALedPin);
NativeUartPort g_portB(g_serialB, kChannelBTx, kChannelBRx, kChannelBDir, kChannelBLedPin);

ChannelContext g_channels[MB_CHANNEL_COUNT];

ChannelContext &context_for(ModbusChannelId channel) {
  const size_t index = static_cast<size_t>(channel);
  return g_channels[index < MB_CHANNEL_COUNT ? index : 0];
}

bool is_active(ModbusChannelId channel) { return static_cast<size_t>(channel) < g_active_channels; }

// §BUGS.md v0.9.0.1: kanal-fejl skal logges struktureret via seriel konsol
// (CLAUDE.md regel 11) — uden dette er en fejlende RTU-transaktion usynlig
// for installatøren, der kun ser en generisk Modbus TCP-gateway-exception.
const char *error_name(mb_error_code_t error) {
  switch (error) {
    case MB_OK: return "MB_OK";
    case MB_TIMEOUT: return "MB_TIMEOUT";
    case MB_CRC_ERROR: return "MB_CRC_ERROR";
    case MB_EXCEPTION: return "MB_EXCEPTION";
    case MB_MAX_REQUESTS_EXCEEDED: return "MB_MAX_REQUESTS_EXCEEDED";
    case MB_NOT_ENABLED: return "MB_NOT_ENABLED";
    case MB_INVALID_SLAVE: return "MB_INVALID_SLAVE";
    case MB_INVALID_ADDRESS: return "MB_INVALID_ADDRESS";
    case MB_BUS_BUSY: return "MB_BUS_BUSY";
    case MB_CHANNEL_UNREACHABLE: return "MB_CHANNEL_UNREACHABLE";
    case MB_UNSUPPORTED_FUNCTION: return "MB_UNSUPPORTED_FUNCTION";
    default: return "?";
  }
}

// v0.28.3 (Jan: "kan vi gøre det output mere lækket med en mere klar
// afgrænsning af de modtage data samt sende data ... pakke output ikke er
// kocistent") — FAST, ensrettet linjeformat for ALT debug-/syslog-output i
// denne fil: `DEBUG <kanal> <retning> <label>: <indhold>`, hvor `<retning>`
// altid er ÉN af `kTxArrow`/`kRxArrow` (aldrig udeladt, som hex-dump-
// linjerne fejlagtigt gjorde før denne version — den inkonsistens var
// netop Jans konkrete observation: "DEBUG RX: ..." manglede kanalnavnet de
// øvrige linjer altid har). Genbruges af alle hjælpefunktionerne nedenfor
// OG direkte i execute_transaction() for de linjer der ikke har deres egen
// hjælper (start/error/dir/noise/delay/parse/result).
constexpr const char *kTxArrow = ">TX>";
constexpr const char *kRxArrow = "<RX<";

// v0.28.5 (Jan: "kan vi ikke gør den timestamp mere pæn at se på ... dag
// timer sekundær millisekundær på [d:t:s:m]") — formaterer et `millis()`-
// uptime-tidsstempel som "D:HH:MM:SS.mmm" (dage:timer:minutter:sekunder.
// millisekunder) i stedet for det rå, svært-læselige millisekund-tal fra
// v0.28.4. `out_capacity` skal være mindst 20 bytes (det længst mulige
// tal, dage, kan i praksis blive flercifret ved lang oppetid).
void format_uptime(unsigned long ms, char *out, size_t out_capacity) {
  const unsigned long total_s = ms / 1000UL;
  const unsigned ms_part = static_cast<unsigned>(ms % 1000UL);
  const unsigned s_part = static_cast<unsigned>(total_s % 60UL);
  const unsigned long total_min = total_s / 60UL;
  const unsigned min_part = static_cast<unsigned>(total_min % 60UL);
  const unsigned long total_h = total_min / 60UL;
  const unsigned h_part = static_cast<unsigned>(total_h % 24UL);
  const unsigned long d_part = total_h / 24UL;
  snprintf(out, out_capacity, "%lu:%02u:%02u:%02u.%03u", d_part, h_part, min_part, s_part, ms_part);
}

void debug_line(ChannelContext &ctx, uint8_t dbg, uint8_t min_level, const char *direction, const char *label,
                 const char *fmt, ...) {
  char content[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(content, sizeof(content), fmt, args);
  va_end(args);

  // v0.28.4/v0.28.5 (Jan: "kan vi få timestamp på debug" / "kan vi ikke
  // gør den timestamp mere pæn ... dag timer sekundær millisekundær på
  // [d:t:s:m]") — boardet har ingen RTC/NTP (samme begrænsning som
  // syslog-headerens pseudo-dato, v0.26.0), saa `millis()` (ms siden boot)
  // er det eneste RIGTIGE, altid-tilgængelige tidsstempel — nu formateret
  // menneskelæseligt som "D:HH:MM:SS.mmm" (`format_uptime()` ovenfor) i
  // stedet for et råt millisekund-tal, som et absolut referencepunkt PR.
  // linje (til at se afstanden MELLEM to linjer, ikke kun varigheden af
  // ét trin).
  char ts_str[24];
  format_uptime(millis(), ts_str, sizeof(ts_str));
  if (dbg >= min_level) {
    Serial.printf("DEBUG [%s] %s %s %s: %s\n", ts_str, ctx.name, direction, label, content);
  }
  syslog_logf(MB_SYSLOG_FACILITY_MODBUS, min_level, "[%s] %s %s %s: %s", ts_str, ctx.name, direction, label, content);
}

// Rå hex-dump af en frame, byte-for-byte til Serial (§BUGS.md v0.24.0-
// lektion: channel_task() kører på en LILLE 4096-byte FreeRTOS-stack —
// INGEN stor lokal streng-buffer må bygges her til SERIAL-udgaven), men ÉT
// samlet syslog-linje (en UDP-pakke pr. byte ville oversvømme netværket/
// modtageren for enhver ikke-triviel respons) via en begrænset lokal buffer
// (samme stak-forsigtighed, se syslog_sender.h). Samme `DEBUG <kanal>
// <retning> packet: ...`-præfiks som alt andet output nu bruger.
void debug_packet(ChannelContext &ctx, uint8_t dbg, uint8_t min_level, const char *direction, const uint8_t *data,
                   size_t len) {
  char ts_str[24];
  format_uptime(millis(), ts_str, sizeof(ts_str));
  if (dbg >= min_level) {
    Serial.printf("DEBUG [%s] %s %s packet: ", ts_str, ctx.name, direction);
    for (size_t i = 0; i < len; i++) {
      Serial.printf("%02X ", data[i]);
    }
    Serial.println();
  }
  char hex[220];
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 3 < sizeof(hex); i++) {
    pos += static_cast<size_t>(snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[i]));
  }
  syslog_logf(MB_SYSLOG_FACILITY_MODBUS, min_level, "[%s] %s %s packet: %s", ts_str, ctx.name, direction, hex);
}

// v0.28.2/v0.28.3 (Jan: "kan vi ikke få en modbus protocol frame pakke
// decode med i det debug output" / "kompakt felt-format") — menneskelæselig
// (men kompakt) fortolkning af en HEL RTU-FRAME (adresse+PDU+CRC — IKKE kun
// selve PDU'en, se lib/modbus_pdu's mb_pdu_decode(), som kun kender PDU-
// delen). Denne funktion udtrækker adresse/PDU/CRC fra `frame` og
// sammensætter "ID: <hex>, <mb_pdu_decode-indhold>, CRC: <hex hex>[,
// Status: <resultat>]" — `status` er kun relevant for RX (det endelige
// udfald er endnu ukendt når TX-framen afkodes) og udelades da (nullptr).
// Samme "altid til syslog, kun Serial bag dbg>=1"-mønster som resten af
// filen. Skriver INGEN linje hvis PDU-delen ikke kan afkodes (ukendt FC,
// for lille buffer) — bevidst stille fallback.
void debug_decode(ChannelContext &ctx, uint8_t dbg, const char *direction, const uint8_t *frame, size_t frame_len,
                   bool is_response, const char *status) {
  if (frame_len < 4) return;  // adresse(1) + fc(1) + CRC(2) er det absolutte minimum
  const uint8_t slave = frame[0];
  const uint8_t *pdu = frame + 1;
  const size_t pdu_len = frame_len - 3;
  const uint8_t crc_lo = frame[frame_len - 2];
  const uint8_t crc_hi = frame[frame_len - 1];

  char decoded[140];
  if (mb_pdu_decode(pdu, pdu_len, is_response, decoded, sizeof(decoded)) == 0) return;

  char line[200];
  int written;
  if (status != nullptr) {
    written = snprintf(line, sizeof(line), "ID: %02X, %s, CRC: %02X %02X, Status: %s", slave, decoded, crc_lo, crc_hi,
                        status);
  } else {
    written = snprintf(line, sizeof(line), "ID: %02X, %s, CRC: %02X %02X", slave, decoded, crc_lo, crc_hi);
  }
  if (written <= 0 || static_cast<size_t>(written) >= sizeof(line)) return;

  char ts_str[24];
  format_uptime(millis(), ts_str, sizeof(ts_str));
  if (dbg >= 1) {
    Serial.printf("DEBUG [%s] %s %s decode: %s\n", ts_str, ctx.name, direction, line);
  }
  syslog_logf(MB_SYSLOG_FACILITY_MODBUS, 1, "[%s] %s %s decode: %s", ts_str, ctx.name, direction, line);
}

// Anvender en (ny eller initial) config på kanalens rigtige UART/GPIO'er.
// Kaldes UDELUKKENDE fra kanalens egen task (channel_task) — aldrig direkte
// fra REST-/TCP-lagets tasks, jf. modbus_channel_apply_config()'s
// kø-baserede design. `mode` i `new_config` IGNORERES bevidst — MODE_SEL er
// en input (fabriksvalgt, se kBoardModeSelPin), ikke noget en reconfigure
// kan ændre; `g_hardware_mode` (læst ved boot) bruges altid i stedet.
void apply_config_now(ChannelContext &ctx, const mb_channel_config_t &new_config) {
  ctx.config = new_config;
  ctx.config.mode = g_hardware_mode;
  // v0.31.0: en fejl her (SC16IS752 svarer ikke) fanges pr. transaktion via
  // port->present() i execute_transaction() — baudrate-grænsen håndhæves
  // allerede i REST-laget (modbus_channel_max_baudrate()) før vi når hertil.
  ctx.port->configure(ctx.config);
}

// Selve RTU-transaktionen — direkte portering af mønsteret i
// reference-plc-source/src/modbus_master.cpp:modbus_master_send_request()
// (to-fase timeout, DE/RE-toggling), men genbruger lib/modbus_pdu til
// framing/CRC/svar-komplethed i stedet for at gentage den logik.
mb_error_code_t execute_transaction(ChannelContext &ctx, uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                     uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity) {
  const uint8_t dbg = ctx.debug_level;
  const uint32_t txn_start = millis();

  // v0.28.3: "start" er bevidst den ENESTE linje der stadig fyrer FØR selve
  // PDU'en er valideret — resten af TX-siden (decode/packet) venter til
  // frame'en rent faktisk er bygget nedenfor, så de kan vise ID/CRC.
  debug_line(ctx, dbg, 1, kTxArrow, "start", "FC: %02X, Len: %u", pdu_len > 0 ? pdu[0] : 0,
             static_cast<unsigned>(pdu_len));

  size_t expected_len = 0;
  const mb_pdu_validation_t validation = mb_pdu_expected_response_frame_len(pdu, pdu_len, &expected_len);
  if (validation != MB_PDU_VALID) {
    // v0.28.0 (DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md §2) — et
    // GENKENDT function code med en ugyldig quantity/PDU-længde
    // (MB_PDU_MALFORMED_REQUEST) er en anden situation end en HELT UKENDT
    // function code (MB_PDU_UNSUPPORTED_FUNCTION) — adskilt her, så
    // REST-diagnostikken og Modbus TCP-gateway-exceptionen (§4.1) kan skelne
    // "boardet forstod ikke denne FC" fra "ugyldig adresse i en ellers
    // kendt FC", i stedet for at begge dele fremstår som MB_INVALID_ADDRESS.
    const mb_error_code_t err = (validation == MB_PDU_UNSUPPORTED_FUNCTION) ? MB_UNSUPPORTED_FUNCTION : MB_INVALID_ADDRESS;
    debug_line(ctx, dbg, 1, kTxArrow, "error", "%s (ukendt/ugyldig function code eller quantity)", error_name(err));
    return err;
  }

  uint8_t frame[MB_RTU_FRAME_MAX_LEN];
  const size_t frame_len = mb_pdu_build_rtu_request(slave_id, pdu, pdu_len, frame, sizeof(frame));
  if (frame_len == 0) {
    debug_line(ctx, dbg, 1, kTxArrow, "error", "%s (kunne ikke bygge RTU-frame)", error_name(MB_INVALID_ADDRESS));
    return MB_INVALID_ADDRESS;
  }

  debug_decode(ctx, dbg, kTxArrow, frame, frame_len, false, nullptr);

  // v0.31.0: EXP_SEL-jumperen siger at CJMCU-752 er monteret, men chippen
  // svarede ikke ved boot — afvis tydeligt i stedet for at "sende" ud i intet.
  if (!ctx.port->present()) {
    debug_line(ctx, dbg, 1, kTxArrow, "error", "%s (UART-expander CJMCU-752 ikke fundet paa I2C - tjek modul/ledninger)",
               error_name(MB_CHANNEL_UNREACHABLE));
    return MB_CHANNEL_UNREACHABLE;
  }

  const bool is_rs485 = ctx.config.mode == MB_CHANNEL_MODE_RS485;
  // SC16IS752 styrer selv DE/RE via sit RTS-ben (auto-RS485) — kun ESP32-
  // UART'erne (A/B) skal have manuel retnings-toggling.
  const bool manual_dir = is_rs485 && !ctx.port->handles_direction();

  // Tøm evt. støj fra bussen inden vi selv sender — TIDSBEGRÆNSET (§BUGS.md
  // v0.9.0.1): uden denne grænse kan en kontinuerligt støjende/floating
  // RX-linje (fx manglende terminering/bias-modstande på en RS485-bus)
  // holde denne løkke kørende for evigt, og dermed hænge HELE kanal-tasken
  // permanent efter blot ét kald.
  {
    const uint32_t drain_start = millis();
    size_t drained = 0;
    while (ctx.port->available() > 0 && millis() - drain_start < 50) {
      ctx.port->read();
      drained++;
    }
    if (drained > 0) {
      debug_line(ctx, dbg, 2, kRxArrow, "noise", "draining %u byte(s) from bus", static_cast<unsigned>(drained));
    }
  }

  // RS485 (§2.2.1/§2.0.1): DE/RE toggles omkring selve sendingen. RS232:
  // fuld-duplex, dir-pinden røres slet ikke (§4.2's afklaring — mode er en
  // deployment-tids-beslutning, ikke noget der skifter live).
  if (manual_dir) {
    ctx.port->set_direction_tx(true);
    debug_line(ctx, dbg, 3, kTxArrow, "dir", "DE/RE -> TX (dir_pin HIGH)");
    delayMicroseconds(50);
  } else if (is_rs485) {
    debug_line(ctx, dbg, 3, kTxArrow, "dir", "DE/RE styres automatisk af UART-expanderens RTS");
  }

  debug_packet(ctx, dbg, 7, kTxArrow, frame, frame_len);

  const size_t written = ctx.port->write(frame, frame_len);
  ctx.port->flush_tx();

  if (manual_dir) {
    const uint32_t byte_us = (11UL * 1000000UL) / ctx.config.baudrate;
    delayMicroseconds(byte_us + 100);
    ctx.port->set_direction_tx(false);
    debug_line(ctx, dbg, 3, kRxArrow, "dir", "DE/RE -> RX (dir_pin LOW)");
  }

  if (written != frame_len) {
    debug_line(ctx, dbg, 1, kTxArrow, "error", "%s (kun %u af %u byte(s) sendt)", error_name(MB_CHANNEL_UNREACHABLE),
               static_cast<unsigned>(written), static_cast<unsigned>(frame_len));
    return MB_CHANNEL_UNREACHABLE;
  }

  // To-fase timeout: fuld timeout til FØRSTE byte, kort inter-character-
  // timeout herefter (samme filosofi som reference-implementeringen).
  uint8_t response[MB_RTU_FRAME_MAX_LEN];
  size_t received = 0;
  uint32_t interchar_ms = 38500UL / ctx.config.baudrate;
  if (interchar_ms < 2) interchar_ms = 2;
  if (interchar_ms > 20) interchar_ms = 20;

  // BUGS.md v0.9.0.1: `last_byte_time` opdateres PR. modtaget byte — måles
  // inter-character-timeouten mod den samlede transaktions starttidspunkt
  // (fast `start`) i stedet, udløber den næsten altid med det samme efter
  // FØRSTE byte, og en ægte fler-byte-respons ville aldrig kunne læses færdig.
  // dbg>=4: RX-timing pr. byte — kun REGISTRERET her (billig, ingen I/O),
  // aldrig printet INDE i selve løkken. §BUGS.md-klasse-lektion opdaget
  // live under test af denne feature: et Serial.printf() pr. modtaget byte
  // her tog længere end `interchar_ms` (helt ned til 2ms ved høje
  // baudrates) — debug-outputtet ÆNDREDE dermed den faktiske transaktions
  // udfald (spurious MB_TIMEOUT på en ellers gyldig, rettidig respons).
  // Ventetiderne gemmes i stedet i en lille satureret uint8_t-array (0-255ms,
  // rigeligt til at se et mønster) og printes SAMLET efter løkken er
  // afsluttet, uanset udfald.
  uint8_t rx_wait_ms[MB_RTU_FRAME_MAX_LEN];
  uint32_t last_byte_time = millis();
  bool timed_out = false;
  while (received < sizeof(response)) {
    const uint32_t active_timeout = (received == 0) ? ctx.config.timeout_ms : interchar_ms;
    if (millis() - last_byte_time > active_timeout) {
      timed_out = true;
      break;
    }
    if (ctx.port->available() > 0) {
      const uint32_t wait_ms = millis() - last_byte_time;
      rx_wait_ms[received] = static_cast<uint8_t>(wait_ms > 255 ? 255 : wait_ms);
      response[received++] = static_cast<uint8_t>(ctx.port->read());
      last_byte_time = millis();
      if (mb_pdu_response_frame_complete(pdu, pdu_len, response, received)) {
        break;
      }
    } else {
      delay(1);
    }
  }

  // v0.28.3: pr.-byte RX-timing forbliver Serial-only (kun under dbg>=4) —
  // syslog faar i stedet ÉT samlet resumé, samme "flood ikke netværket
  // pr. byte"-hensyn som debug_packet() ovenfor. Bruger DERFOR ikke den
  // fælles debug_line()-hjælper (som altid ville sende BEGGE veje 1:1).
  //
  // v0.28.4 (Jan: "kan vi få timestamp på debug") — `ts` er et absolut
  // uptime-tidsstempel (txn_start + kumuleret ventetid), IKKE blot
  // millis() ved selve print-tidspunktet (loopet printer alle linjer
  // samlet EFTER RX er afsluttet — et fladt millis()-kald her ville derfor
  // givet samme (forkerte) tidsstempel til alle bytes). Bemærk: `rx_wait_ms`
  // er satureret ved 255ms — kun byte[0] (som bruger den fulde
  // `timeout_ms`, ofte langt over 255ms) kan reelt ramme loftet;
  // efterfølgende bytes bruger `interchar_ms` (maks 20ms) og saturerer
  // derfor aldrig. En evt. unøjagtighed er dermed en KONSTANT forskydning
  // fra byte[0] og frem, ikke en voksende fejl — acceptabelt for et
  // debug-hjælpemiddel.
  if (dbg >= 4) {
    unsigned long ts = txn_start;
    char ts_str[24];
    for (size_t i = 0; i < received; i++) {
      ts += rx_wait_ms[i];
      format_uptime(ts, ts_str, sizeof(ts_str));
      Serial.printf("DEBUG [%s] %s %s byte[%u]: 0x%02X (ventede %ums)\n", ts_str, ctx.name, kRxArrow,
                    static_cast<unsigned>(i), response[i], static_cast<unsigned>(rx_wait_ms[i]));
    }
  }
  if (received > 0) {
    char ts_str[24];
    format_uptime(txn_start + rx_wait_ms[0], ts_str, sizeof(ts_str));
    syslog_logf(MB_SYSLOG_FACILITY_MODBUS, 4, "[%s] %s %s byte-summary: %u byte(s), foerste byte ventede %ums",
                ts_str, ctx.name, kRxArrow, static_cast<unsigned>(received), static_cast<unsigned>(rx_wait_ms[0]));
  }

  if (ctx.config.inter_frame_delay_ms > 0) {
    debug_line(ctx, dbg, 5, kRxArrow, "delay", "inter-frame-delay %ums",
               static_cast<unsigned>(ctx.config.inter_frame_delay_ms));
    delay(ctx.config.inter_frame_delay_ms);
  }

  if (timed_out || received == 0) {
    debug_line(ctx, dbg, 1, kRxArrow, "result", "%s (modtog %u byte(s), %ums)", error_name(MB_TIMEOUT),
               static_cast<unsigned>(received), static_cast<unsigned>(millis() - txn_start));
    return MB_TIMEOUT;
  }

  debug_packet(ctx, dbg, 8, kRxArrow, response, received);

  const mb_pdu_parse_result_t parse_result =
      mb_pdu_parse_rtu_response(slave_id, response, received, out_pdu, out_pdu_len, out_pdu_capacity);

  mb_error_code_t final_result;
  switch (parse_result) {
    case MB_PDU_RESULT_OK:
    case MB_PDU_RESULT_EXCEPTION:
      // MB_PDU_RESULT_EXCEPTION er IKKE en kanal-fejl — det er stadig et
      // gyldigt, modtaget svar (en Modbus-exception er semantisk indhold,
      // ikke en transportfejl) — out_pdu er allerede udfyldt med
      // exception-PDU'en, og TCP-laget skal blot relaye den uændret.
      final_result = MB_OK;
      break;
    case MB_PDU_RESULT_CRC_ERROR:
      final_result = MB_CRC_ERROR;
      break;
    case MB_PDU_RESULT_SLAVE_MISMATCH:
      final_result = MB_INVALID_SLAVE;
      break;
    case MB_PDU_RESULT_TOO_SHORT:
    case MB_PDU_RESULT_BUFFER_TOO_SMALL:
    default:
      final_result = MB_CHANNEL_UNREACHABLE;
      break;
  }

  // v0.28.3: decode af den RAA modtagne frame (`response`, adresse+PDU+CRC
  // — IKKE `out_pdu`, som allerede har adresse/CRC strippet), saa "ID:"/
  // "CRC:"-felterne har noget at vise. Kun forsøgt for MB_PDU_RESULT_OK/
  // _EXCEPTION — for CRC_ERROR/SLAVE_MISMATCH/TOO_SHORT er `response`
  // stadig de faktisk modtagne bytes, men CRC'en (eller adressen) har IKKE
  // valideret at det er et ægte, intakt svar fra den forespurgte slave, så
  // en decode af det ville kunne vise vildledende/tilfældige feltværdier
  // som om de var pålidelige — bevidst udeladt for de tilfælde.
  if (parse_result == MB_PDU_RESULT_OK || parse_result == MB_PDU_RESULT_EXCEPTION) {
    debug_decode(ctx, dbg, kRxArrow, response, received, true, error_name(final_result));
  }

  debug_line(ctx, dbg, 6, kRxArrow, "parse", "parse_result=%d -> final_result=%s", static_cast<int>(parse_result),
             error_name(final_result));
  debug_line(ctx, dbg, 1, kRxArrow, "result", "%s (modtog %u byte(s), %ums)", error_name(final_result),
             static_cast<unsigned>(received), static_cast<unsigned>(millis() - txn_start));
  return final_result;
}

void record_stats(ChannelContext &ctx, const ChannelRequest &req) {
  ctx.stats.total_requests++;
  if (req.result == MB_OK) {
    ctx.stats.successful_requests++;
    return;
  }

  switch (req.result) {
    case MB_TIMEOUT:
      ctx.stats.timeout_errors++;
      break;
    case MB_CRC_ERROR:
      ctx.stats.crc_errors++;
      break;
    case MB_EXCEPTION:
      ctx.stats.exception_errors++;
      break;
    default:
      break;
  }

  ctx.stats.has_last_error = true;
  ctx.stats.last_error_slave_id = req.slave_id;
  // FC01-06/16's adresse ligger altid i PDU-byte 1-2 (big-endian) — se
  // lib/modbus_pdu's understøttede function codes.
  ctx.stats.last_error_address = (req.pdu_len >= 3) ? static_cast<uint16_t>((req.pdu[1] << 8) | req.pdu[2]) : 0;
  ctx.stats.last_error_type = static_cast<uint8_t>(req.result);
  ctx.stats.last_error_at_uptime_s = millis() / 1000;
}


void channel_task(void *param) {
  ChannelContext *ctx = static_cast<ChannelContext *>(param);
  for (;;) {
    ChannelRequest *req = nullptr;
    if (xQueueReceive(ctx->queue, &req, portMAX_DELAY) != pdTRUE || req == nullptr) {
      continue;
    }

    if (req->type == ChannelRequestType::kReconfigure) {
      apply_config_now(*ctx, req->new_config);
      req->result = MB_OK;
      xSemaphoreGive(req->done);
      continue;
    }

    if (!ctx->config.enabled) {
      req->result = MB_NOT_ENABLED;  // ingen reel bus-aktivitet — LED'en blinker bevidst IKKE for dette
      // v0.28.1 (Jan: "vi har i dag output ved fejl til console lave det om
      // sådan vi ikke har det output men kun hvis vi bruger debug til at
      // output til console") — Serial-output for en kanal-fejl vises nu
      // UDELUKKENDE når debug er slået til for DEN kanal (samme "<< RESULT"-
      // stil som execute_transaction()s egne dbg>=1-linjer) — INGEN
      // ubetinget fejl-print til konsollen længere. syslog er UAFHÆNGIG
      // heraf (egen verbositet pr. modtager, se lib/syslog_client) og
      // rammes ikke af denne ændring.
      // v0.28.3/v0.28.4: samme "DEBUG [ts] <kanal> <retning> <label>:
      // <indhold>"-skabelon som execute_transaction() (denne gren kalder
      // aldrig execute_transaction() selv, saa den skal formatere sit eget
      // fejl-udfald manuelt, jf. v0.28.0's kommentar ovenfor).
      {
        char ts_str[24];
        format_uptime(millis(), ts_str, sizeof(ts_str));
        if (ctx->debug_level >= 1) {
          Serial.printf("DEBUG [%s] %s <RX< result: MB_NOT_ENABLED (kanalen er deaktiveret, slave=%u fc=%u)\n", ts_str,
                        ctx->name, req->slave_id, req->pdu_len > 0 ? req->pdu[0] : 0);
        }
        syslog_logf(MB_SYSLOG_FACILITY_MODBUS, 1, "[%s] %s <RX< result: MB_NOT_ENABLED (kanalen er deaktiveret, slave=%u fc=%u)",
                    ts_str, ctx->name, req->slave_id, req->pdu_len > 0 ? req->pdu[0] : 0);
      }
    } else {
      ctx->port->set_activity_led(true);
      req->result = execute_transaction(*ctx, req->slave_id, req->pdu, req->pdu_len, req->out_pdu, req->out_pdu_len,
                                         req->out_pdu_capacity);
      ctx->port->set_activity_led(false);
    }

    record_stats(*ctx, *req);
    xSemaphoreGive(req->done);
  }
}

void init_channel(ChannelContext &ctx, ChannelPort &port, size_t config_index, const char *task_name,
                   const mb_channel_config_t &initial_config) {
  ctx.name = task_name;
  ctx.config_index = config_index;
  ctx.port = &port;
  ctx.stats = mb_channel_stats_t{};
  ctx.debug_level = 0;  // v0.25.0: altid FRA ved boot, bevidst ikke persisteret

  apply_config_now(ctx, initial_config);

  ctx.queue = xQueueCreate(4, sizeof(ChannelRequest *));
  xTaskCreate(channel_task, task_name, 4096, &ctx, tskIDLE_PRIORITY + 1, nullptr);
}

}  // namespace

void modbus_channel_init_all() {
  // Hardware-revision 2026-09-14: MODE_SEL (GPIO4) er en INPUT, sat ved
  // fremstilling — læses HER, én gang, og gælder resten af boardets
  // levetid (indtil næste genstart). `mode` i den persisterede config
  // (fra en evt. ældre firmware-version, hvor det var software-sat)
  // IGNORERES bevidst og overskrives altid med den fabriksvalgte værdi.
  g_hardware_mode = read_board_mode_sel();
  Serial.print("MODE_SEL (GPIO4) laest ved boot: ");
  Serial.println(g_hardware_mode == MB_CHANNEL_MODE_RS485 ? "RS485" : "RS232");

  // v0.31.0: EXP_SEL (GPIO36) — se kExpanderSelPin. INPUT uden intern pull
  // (GPIO34-39 har ingen) — den eksterne pull-down er påkrævet.
  pinMode(kExpanderSelPin, INPUT);
  delayMicroseconds(10);
  const bool expander_fitted = digitalRead(kExpanderSelPin) == HIGH;
  if (expander_fitted) {
    const bool found = uart_expander_begin();
    g_active_channels = 4;
    g_expander_status = found ? ModbusExpanderStatus::kOk : ModbusExpanderStatus::kNotFound;
    if (found) {
      Serial.printf("EXP_SEL (GPIO36): CJMCU-752 monteret - fundet paa I2C-adresse 0x%02X, 4 kanaler (A-D)\r\n",
                    uart_expander_i2c_address());
    } else {
      Serial.println("EXP_SEL (GPIO36): CJMCU-752 monteret ifoelge jumperen, men SVARER IKKE paa I2C (SDA=21, SCL=22) - "
                     "kanal C/D afviser alle transaktioner");
      syslog_log(MB_SYSLOG_FACILITY_MODBUS, 1,
                 "UART-expander CJMCU-752 ikke fundet paa I2C selvom EXP_SEL-jumperen siger monteret - kanal C/D utilgaengelige");
    }
  } else {
    g_active_channels = 2;
    g_expander_status = ModbusExpanderStatus::kNotFitted;
    Serial.println("EXP_SEL (GPIO36): ingen UART-expander - 2 kanaler (A-B)");
  }

  mb_channel_config_t configs[MB_CHANNEL_COUNT];
  for (size_t i = 0; i < MB_CHANNEL_COUNT; i++) {
    configs[i] = config_get().channel[i];  // §4.2: persisteret config, ikke hardkodet
    configs[i].mode = g_hardware_mode;     // alle kanaler følger den ene MODE_SEL-jumper
  }

  // v0.28.6 (Jan: "ændre i debug output tekst 'mb_ch_a' til 'mb_ch_A' det
  // samme for b til B") — samme streng bruges BÅDE som denne kanals navn i
  // alt debug-/syslog-output OG som selve FreeRTOS-task-navnet (se
  // init_channel()s xTaskCreate()-kald nedenfor) — et rent kosmetisk valg,
  // ingen kode andetsteds sammenligner disse strenge (kun til visning).
  g_portA.init_pins();
  g_portB.init_pins();
  init_channel(g_channels[0], g_portA, 0, "mb_ch_A", configs[0]);
  init_channel(g_channels[1], g_portB, 1, "mb_ch_B", configs[1]);
  if (g_active_channels == 4) {
    init_channel(g_channels[2], uart_expander_port(0), 2, "mb_ch_C", configs[2]);
    init_channel(g_channels[3], uart_expander_port(1), 3, "mb_ch_D", configs[3]);
  }
}

size_t modbus_channel_active_count() { return g_active_channels; }

bool modbus_channel_hardware_present(ModbusChannelId channel) {
  return is_active(channel) && context_for(channel).port->present();
}

ModbusExpanderStatus modbus_channel_expander_status() { return g_expander_status; }

uint32_t modbus_channel_max_baudrate(ModbusChannelId channel) {
  if (!is_active(channel)) return 0;
  return context_for(channel).port->max_baud();
}

mb_error_code_t modbus_channel_submit(ModbusChannelId channel, uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                       uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity) {
  if (!is_active(channel)) {
    return MB_CHANNEL_UNREACHABLE;  // v0.31.0: kanal C/D uden monteret UART-expander
  }
  ChannelContext &ctx = context_for(channel);

  if (!ctx.config.enabled) {
    return MB_NOT_ENABLED;  // hurtig afvisning — ingen grund til at vaekke kanal-tasken
  }

  ChannelRequest req;
  req.type = ChannelRequestType::kTransaction;
  req.slave_id = slave_id;
  req.pdu = pdu;
  req.pdu_len = pdu_len;
  req.out_pdu = out_pdu;
  req.out_pdu_len = out_pdu_len;
  req.out_pdu_capacity = out_pdu_capacity;
  req.result = MB_CHANNEL_UNREACHABLE;
  req.done = xSemaphoreCreateBinary();
  if (req.done == nullptr) {
    return MB_CHANNEL_UNREACHABLE;
  }

  ChannelRequest *req_ptr = &req;
  if (xQueueSend(ctx.queue, &req_ptr, pdMS_TO_TICKS(1000)) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return MB_BUS_BUSY;  // kanalens kø var fuld — en anden transaktion optager den
  }

  // Vent lidt laengere end kanalens egen timeout, saa vi altid faar
  // kanal-tasken resultat (MB_TIMEOUT) fremfor selv at time ud foerst.
  if (xSemaphoreTake(req.done, pdMS_TO_TICKS(ctx.config.timeout_ms + 1000)) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return MB_TIMEOUT;
  }

  vSemaphoreDelete(req.done);
  return req.result;
}

namespace {

bool apply_config_to_channel(ChannelContext &ctx, const mb_channel_config_t &new_config) {
  ChannelRequest req;
  req.type = ChannelRequestType::kReconfigure;
  req.new_config = new_config;
  req.result = MB_CHANNEL_UNREACHABLE;
  req.done = xSemaphoreCreateBinary();
  if (req.done == nullptr) {
    return false;
  }

  ChannelRequest *req_ptr = &req;
  if (xQueueSend(ctx.queue, &req_ptr, pdMS_TO_TICKS(1000)) != pdTRUE) {
    vSemaphoreDelete(req.done);
    return false;
  }

  const bool ok = xSemaphoreTake(req.done, pdMS_TO_TICKS(2000)) == pdTRUE && req.result == MB_OK;
  vSemaphoreDelete(req.done);
  return ok;
}

}  // namespace

bool modbus_channel_apply_config(ModbusChannelId channel, const mb_channel_config_t &new_config) {
  if (!is_active(channel)) return false;
  ChannelContext &ctx = context_for(channel);
  if (!apply_config_to_channel(ctx, new_config)) {
    return false;
  }
  // Persistér ctx.config (EFTER apply_config_now() har tvunget mode til
  // g_hardware_mode), ikke den rå new_config-parameter — ellers ville en
  // forældet/vilkårlig mode-værdi fra kaldsleddet (fx en tom/default-
  // initialiseret struct, nu hvor mode ikke længere parses fra PUT-JSON,
  // se lib/channel_config) kunne blive skrevet til NVS i stedet for den
  // faktiske MODE_SEL-hardwareværdi.
  config_set_channel(ctx.config_index, ctx.config);
  return true;
}

mb_channel_config_t modbus_channel_get_config(ModbusChannelId channel) { return context_for(channel).config; }

mb_channel_stats_t modbus_channel_get_stats(ModbusChannelId channel) { return context_for(channel).stats; }

void modbus_channel_set_debug_level(ModbusChannelId channel, uint8_t level) {
  if (!is_active(channel)) return;
  context_for(channel).debug_level = level;
}

uint8_t modbus_channel_get_debug_level(ModbusChannelId channel) { return context_for(channel).debug_level; }
