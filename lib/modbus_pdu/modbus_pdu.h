#pragma once

#include <cstddef>
#include <cstdint>

// Transaktions-fejlkoder — matcher PLC-repoets mb_error_code_t 1:1, værdi for
// værdi (se reference-plc-source/include/types.h og EXPANSION_BOARD_DESIGN.md
// §4), udvidet med MB_CHANNEL_UNREACHABLE (ny, expansion-board-specifik).
enum mb_error_code_t {
  MB_OK = 0,
  MB_TIMEOUT = 1,
  MB_CRC_ERROR = 2,
  MB_EXCEPTION = 3,
  MB_MAX_REQUESTS_EXCEEDED = 4,
  MB_NOT_ENABLED = 5,
  MB_INVALID_SLAVE = 6,
  MB_INVALID_ADDRESS = 7,
  MB_BUS_BUSY = 8,
  MB_CHANNEL_UNREACHABLE = 9,
  // v0.28.0 (DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md §2, PLC-
  // udviklingsteamets forslag) — dedikeret til "function code ikke
  // implementeret af boardets gateway", adskilt fra MB_INVALID_ADDRESS
  // (som nu KUN dækker "ugyldig adresse/quantity for en ELLERS kendt FC").
  // Værdien 10 er eksplicit foreslået i designdokumentet.
  MB_UNSUPPORTED_FUNCTION = 10
};

constexpr size_t MB_PDU_MAX_LEN = 253;                          // Modbus-spec: FC + op til 252 databytes
constexpr size_t MB_RTU_FRAME_MAX_LEN = 1 + MB_PDU_MAX_LEN + 2;  // adresse + PDU + CRC(2)
constexpr size_t MB_RTU_EXCEPTION_FRAME_LEN = 5;                 // adresse + (fc|0x80) + exception-kode + CRC(2)

// CRC16 (Modbus RTU) — algoritme og bytefølge (CRC low byte først) er
// byte-for-byte identisk med reference-plc-source/src/modbus_master.cpp's
// modbus_master_calc_crc(), verificeret mod spec-eksemplet i test/test_modbus_pdu.
uint16_t mb_pdu_calc_crc16(const uint8_t *buffer, size_t len);

// Bygger en RTU-forespørgsels-frame (adresse + pdu + CRC) for `slave_id` ud
// fra en allerede opbygget PDU (function code + data, modtaget fra TCP-laget,
// §4.1). Returnerer frame-længden, eller 0 hvis pdu_len er 0/for stor, eller
// out_capacity er for lille.
size_t mb_pdu_build_rtu_request(uint8_t slave_id, const uint8_t *pdu, size_t pdu_len,
                                 uint8_t *out_frame, size_t out_capacity);

enum mb_pdu_validation_t {
  MB_PDU_VALID = 0,
  MB_PDU_UNSUPPORTED_FUNCTION = 1,  // function code udenfor FC01-06/15/16 (§4.1's scope)
  MB_PDU_MALFORMED_REQUEST = 2      // forkert længde, eller quantity/byte_count udenfor Modbus-spec-grænser
};

// v0.28.0 (DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md §1) — den ENESTE
// kilde til "hvilke function codes understøtter boardet", brugt af
// `GET /api/capabilities` (lib/rest_status). SKAL holdes i sync med
// switch-casene i mb_pdu_expected_response_frame_len()s implementering
// (modbus_pdu.cpp) — ingen automatisk afledning (kun 8 værdier, lav
// ændringsfrekvens gør denne manuelle disciplin acceptabel fremfor en
// tungere refleksions-mekanisme). test_modbus_pdu.cpp har en test der
// krydstjekker de to holder sig i sync.
extern const uint8_t MB_PDU_SUPPORTED_FUNCTIONS[8];
constexpr size_t MB_PDU_SUPPORTED_FUNCTION_COUNT = 8;

// Modbus-spec'ens egne pr.-FC quantity-grænser (håndhævet i
// mb_pdu_expected_response_frame_len()'s switch, modbus_pdu.cpp) — navngivet
// her så `GET /api/capabilities` kan rapportere dem uden at duplikere
// magic numbers et andet sted (samme "én kilde"-princip som FC-listen ovenfor).
constexpr uint16_t MB_PDU_MAX_READ_BIT_QUANTITY = 2000;       // FC01/02
constexpr uint16_t MB_PDU_MAX_READ_REGISTER_QUANTITY = 125;   // FC03/04
constexpr uint16_t MB_PDU_MAX_WRITE_COIL_QUANTITY = 1968;     // FC15
constexpr uint16_t MB_PDU_MAX_WRITE_REGISTER_QUANTITY = 123;  // FC16

// Beregner hvor mange bytes en komplet, IKKE-exception RTU-svar-frame vil
// være (adresse + svar-pdu + CRC) for en given forespørgsels-PDU. Kun
// FC01/02/03/04/05/06/15/16 genkendes (§4.1) — alt andet giver
// MB_PDU_UNSUPPORTED_FUNCTION, som kalderen bør afvise FØR transmission
// (jf. Modbus exception 0x01 "illegal function", ikke en transportfejl).
mb_pdu_validation_t mb_pdu_expected_response_frame_len(const uint8_t *request_pdu, size_t request_pdu_len,
                                                        size_t *out_expected_len);

// True når `frame_so_far` (de første `received_len` bytes af et RTU-svar)
// udgør en komplet frame — enten den FC-specifikke forventede længde fra
// mb_pdu_expected_response_frame_len(), eller en komplet 5-byte
// exception-frame (høj bit sat i funktionskode-byten). Generaliserer
// længde-logikken fra reference-plc-source/src/modbus_master.cpp's
// modtage-løkke til at virke for enhver af de understøttede function codes
// i stedet for ét fast kald.
bool mb_pdu_response_frame_complete(const uint8_t *request_pdu, size_t request_pdu_len,
                                     const uint8_t *frame_so_far, size_t received_len);

enum mb_pdu_parse_result_t {
  MB_PDU_RESULT_OK = 0,                // gyldigt, ikke-exception svar — out_pdu indeholder svar-PDU'en
  MB_PDU_RESULT_CRC_ERROR = 1,
  MB_PDU_RESULT_SLAVE_MISMATCH = 2,    // frame'ens adresse-byte matcher ikke expected_slave_id
  MB_PDU_RESULT_EXCEPTION = 3,         // slaven svarede med en Modbus-exception — out_pdu[0]=fc|0x80, out_pdu[1]=exception-kode
  MB_PDU_RESULT_TOO_SHORT = 4,         // frame_len < 4 (mindste mulige RTU-frame: adresse+fc+CRC(2))
  MB_PDU_RESULT_BUFFER_TOO_SMALL = 5   // out_pdu_capacity utilstrækkelig til at rumme svar-PDU'en
};

// Validerer en fuldt modtaget RTU-svar-frame (adresse + pdu + CRC) mod den
// forventede slave-adresse, verificerer CRC, og udtrækker svar-PDU'en
// (function code + data, uden adresse og CRC) til out_pdu.
mb_pdu_parse_result_t mb_pdu_parse_rtu_response(uint8_t expected_slave_id, const uint8_t *frame, size_t frame_len,
                                                 uint8_t *out_pdu, size_t *out_pdu_len, size_t out_pdu_capacity);

// v0.28.2/v0.28.3 (Jan: "kan vi ikke få en modbus protocol frame pakke
// decode med i det debug output" / "kan vi gøre det output mere lækket ...
// kompakt felt-format") — KOMPAKT feltbaseret fortolkning af en PDU
// (function code + data, UDEN adresse/CRC — samme "PDU" som resten af
// denne fil), til brug i debug-/syslog-output (src/modbus_channel.cpp,
// som selv tilføjer "ID: <slave>"-feltet foran og "CRC: <hex hex>" bagved
// — begge dele af den FULDE RTU-frame, ikke selve PDU'en, derfor uden for
// denne funktions scope). Format: "FC: <hex>, Addr: <tal>, Qty: <tal>" for
// en forespørgsel, "FC: <hex>, Values: [v1,v2,...]" for et registersvar,
// "FC: <hex>, Exception: <hex> (<navn>)" for en Modbus-exception. Kun
// FC01-06/15/16 (§4.1's scope) genkendes — alt andet giver en tom streng
// (0 bytes skrevet), IKKE en fejl (kaldstedet har allerede sin egen
// ukendt-FC-håndtering, se MB_UNSUPPORTED_FUNCTION). Værdilister afkortes
// ved 20 elementer ("...og N mere") — en fuld ~2000-værdiers FC01/02-liste
// ville gøre én debug-/syslog-linje ubrugeligt lang. Returnerer antal
// skrevne bytes, eller 0 ved ugyldige argumenter/for lille buffer/ukendt FC.
size_t mb_pdu_decode(const uint8_t *pdu, size_t pdu_len, bool is_response, char *out, size_t out_capacity);
