// Host-side unit tests for the portable C core (no ESPHome needed).
// Build & run:  make -C tests test
//
// Frames below are real captures from a Swedish 3-phase Gen2 TWC (fw 4.5.10.2).

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "twc_core.h"
#include "twc_device.h"
#include "twc_protocol.h"

static int g_failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
      g_failures++;                                                        \
    }                                                                      \
  } while (0)
#define CHECK_NEAR(a, b) CHECK(fabsf((float)(a) - (float)(b)) < 0.01f)

static size_t hex(const char *s, uint8_t *out) {
  size_t n = 0;
  while (*s) {
    while (*s == ' ') s++;
    if (!*s) break;
    unsigned v;
    sscanf(s, "%2x", &v);
    out[n++] = (uint8_t)v;
    s += 2;
  }
  return n;
}

// ---------------------------------------------------------------------------
// Meter decode
// ---------------------------------------------------------------------------
static void test_meter_decode(void) {
  uint8_t f[64];
  size_t n;
  const uint8_t *hdr, *pl;
  size_t pl_len;
  twc_meter_data_t m;

  // Car charging, 0x8988
  n = hex("FD EB 89 88 00 00 87 1A 00 E4 00 E4 00 E4 16 16 17 00 00 8C", f);
  CHECK(twc_decode_frame(f, n, &hdr, &pl, &pl_len));
  CHECK(twc_decode_meter_payload(pl, pl_len, &m));
  CHECK_NEAR(m.total_energy_kwh, 34586);
  CHECK_NEAR(m.phase_l1_v, 228); CHECK_NEAR(m.phase_l2_v, 228); CHECK_NEAR(m.phase_l3_v, 228);
  CHECK_NEAR(m.phase_l1_a, 11.0); CHECK_NEAR(m.phase_l2_a, 11.0); CHECK_NEAR(m.phase_l3_a, 11.5);

  // Idle, 0x9983
  n = hex("FD EB 99 83 00 00 1D 8F 00 E6 00 E6 00 E5 00 00 00 00 00 64", f);
  CHECK(twc_decode_frame(f, n, &hdr, &pl, &pl_len));
  CHECK(twc_decode_meter_payload(pl, pl_len, &m));
  CHECK_NEAR(m.total_energy_kwh, 7567);
  CHECK_NEAR(m.phase_l1_v, 230); CHECK_NEAR(m.phase_l2_v, 230); CHECK_NEAR(m.phase_l3_v, 229);
  CHECK_NEAR(m.phase_l1_a, 0); CHECK_NEAR(m.phase_l2_a, 0); CHECK_NEAR(m.phase_l3_a, 0);

  // Too short payload is rejected
  CHECK(!twc_decode_meter_payload(pl, 14, &m));
}

// ---------------------------------------------------------------------------
// Core: session current (0x09) delivery
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t frames[64][32];
  size_t lens[64];
  int count;
} tx_log_t;

static void tx_cb(const uint8_t *frame, size_t len, void *user) {
  tx_log_t *l = (tx_log_t *)user;
  if (l->count < 64 && len <= 32) {
    memcpy(l->frames[l->count], frame, len);
    l->lens[l->count] = len;
    l->count++;
  }
}

// Last heartbeat (FB E0) sent to `addr`: returns state byte and avail centiamps, or -1.
static int last_hb(const tx_log_t *l, uint16_t addr, unsigned *avail) {
  for (int i = l->count - 1; i >= 0; --i) {
    const uint8_t *f = l->frames[i];
    if (f[0] == 0xFB && f[1] == 0xE0 && f[4] == (addr >> 8) && f[5] == (addr & 0xFF)) {
      *avail = ((unsigned)f[7] << 8) | f[8];
      return f[6];
    }
  }
  return -1;
}

static void rx(twc_core_t *c, const char *h, uint32_t now) {
  uint8_t f[64];
  size_t n = hex(h, f);
  CHECK(twc_core_handle_frame(c, f, n, now));
}

// Inject a valid FD E0 reply from `addr` (master F00D) built with the protocol helpers.
static void rx_e0(twc_core_t *c, uint16_t addr, uint8_t state, unsigned avail_ca,
                  unsigned deliv_ca, uint32_t now) {
  uint8_t pl[11] = {0xF0, 0x0D, state, (uint8_t)(avail_ca >> 8), (uint8_t)avail_ca,
                    (uint8_t)(deliv_ca >> 8), (uint8_t)deliv_ca, 0, 0, 0, 0};
  uint8_t hdr[4] = {0xFD, 0xE0, (uint8_t)(addr >> 8), (uint8_t)addr};
  uint8_t fr[32];
  size_t n = twc_build_frame(hdr, 4, pl, 11, fr, sizeof(fr));
  CHECK(n == 16);
  CHECK(twc_core_handle_frame(c, fr, n, now));
}

static void init_core(twc_core_t *c, tx_log_t *l) {
  twc_core_init(c);
  memset(l, 0, sizeof(*l));
  twc_core_set_master_address(c, 0xF00D);
  twc_core_set_global_max_current(c, 15.0f);
  twc_core_set_tx_callback(c, tx_cb, l);
  twc_core_set_max_current(c, 0x8988, 15.0f);
  twc_core_set_master_mode(c, true);
}

static void run_ticks(twc_core_t *c, uint32_t from, uint32_t to) {
  for (uint32_t t = from; t <= to; t += 100) twc_core_master_tick(c, t);
}

static void test_session_current_reaches_twc(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);

  // Peripheral 0x8988 reports charging with its own 15.00 A (as in the field log).
  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 1000);
  twc_core_device_t *d = twc_core_get_device_by_address(&c, 0x8988);
  CHECK(d != NULL);

  // evcc wants 12 A
  twc_core_set_desired_session_current(&c, 0x8988, 12.0f);
  CHECK_NEAR(d->applied_session_current_a, 12.0f);

  run_ticks(&c, 1000, 4000);
  unsigned avail = 0;
  bool seen09 = false;
  for (int i = 0; i < l.count; i++)
    if (l.frames[i][0] == 0xFB && l.frames[i][1] == 0xE0 && l.frames[i][6] == 0x09) {
      seen09 = true;
      avail = ((unsigned)l.frames[i][7] << 8) | l.frames[i][8];
    }
  CHECK(seen09);
  CHECK(avail == 1200);

  // TWC keeps reporting 15.00 A (did not take the limit): master must resend.
  int before = 0;
  for (int i = 0; i < l.count; i++) if (l.frames[i][1] == 0xE0 && l.frames[i][6] == 0x09) before++;
  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 4100);
  run_ticks(&c, 4100, 9000);
  int after = 0;
  for (int i = 0; i < l.count; i++) if (l.frames[i][1] == 0xE0 && l.frames[i][6] == 0x09) after++;
  CHECK(after > before);
}

static void test_resend_stops_when_matching(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 1000);
  twc_core_set_desired_session_current(&c, 0x8988, 12.0f);
  twc_core_device_t *d = twc_core_get_device_by_address(&c, 0x8988);

  // Build a valid FD E0 reply that echoes 12.00 A (0x04B0).
  uint8_t pl[11] = {0xF0, 0x0D, 0x01, 0x04, 0xB0, 0x04, 0x92, 0, 0, 0, 0};
  uint8_t hdr[4] = {0xFD, 0xE0, 0x89, 0x88};
  uint8_t fr[32];
  size_t n = twc_build_frame(hdr, 4, pl, 11, fr, sizeof(fr));
  CHECK(n == 16);

  run_ticks(&c, 1000, 1500);          // sends the pending 0x09
  CHECK(d->last_session_cmd_tx_ms != 0);
  CHECK(twc_core_handle_frame(&c, fr, n, 1600));
  CHECK_NEAR(d->current_available_a, 12.0f);

  int sent09 = 0;
  for (int i = 0; i < l.count; i++) if (l.frames[i][1] == 0xE0 && l.frames[i][6] == 0x09) sent09++;
  run_ticks(&c, 1600, 8000);          // < refresh interval, TWC matches: nothing more
  int sent09_after = 0;
  for (int i = 0; i < l.count; i++) if (l.frames[i][1] == 0xE0 && l.frames[i][6] == 0x09) sent09_after++;
  CHECK(sent09_after == sent09);

  run_ticks(&c, 8000, 14000);         // refresh after TWC_SESSION_REFRESH_INTERVAL_MS
  int sent09_refresh = 0;
  for (int i = 0; i < l.count; i++) if (l.frames[i][1] == 0xE0 && l.frames[i][6] == 0x09) sent09_refresh++;
  CHECK(sent09_refresh > sent09);
}

static void test_zero_amps_is_sent_while_charging(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 1000);
  twc_core_set_desired_session_current(&c, 0x8988, 12.0f);
  run_ticks(&c, 1000, 2000);
  // evcc disables charging / watchdog trips -> desired 0 A
  twc_core_set_desired_session_current(&c, 0x8988, 0.0f);
  run_ticks(&c, 2000, 5000);
  unsigned avail = 99;
  int st = last_hb(&l, 0x8988, &avail);
  CHECK(st == 0x09);
  CHECK(avail == 0);
}

static void test_global_max_scaling_kept(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  twc_core_set_max_current(&c, 0x9983, 15.0f);
  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 1000);
  rx_e0(&c, 0x9983, 0x01, 1500, 1170, 1000);
  twc_core_set_desired_session_current(&c, 0x8988, 12.0f);
  twc_core_set_desired_session_current(&c, 0x9983, 12.0f);  // priority device gets 12, other 3 (global 15)
  twc_core_device_t *a = twc_core_get_device_by_address(&c, 0x8988);
  twc_core_device_t *b = twc_core_get_device_by_address(&c, 0x9983);
  CHECK(a->applied_session_current_a + b->applied_session_current_a <= 15.01f);
  CHECK_NEAR(b->applied_session_current_a, 12.0f);
}

// ---------------------------------------------------------------------------
// RX diagnostics: checksum_fail, unhandled types, first-time WARN
// ---------------------------------------------------------------------------
typedef struct {
  twc_log_level_t levels[32];
  char msgs[32][192];
  int count;
} log_cap_t;

static void log_cap_cb(twc_log_level_t level, const char *message, void *user) {
  log_cap_t *c = (log_cap_t *)user;
  if (!c || c->count >= 32) return;
  c->levels[c->count] = level;
  snprintf(c->msgs[c->count], sizeof(c->msgs[c->count]), "%s", message ? message : "");
  c->count++;
}

static size_t build_cmd(uint8_t marker, uint8_t cmd, uint16_t src,
                        const uint8_t *payload, size_t payload_len,
                        uint8_t *out, size_t out_cap) {
  uint8_t hdr[4] = {marker, cmd, (uint8_t)(src >> 8), (uint8_t)src};
  return twc_build_frame(hdr, 4, payload, payload_len, out, out_cap);
}

static void test_rx_diag_checksum_and_unhandled(void) {
  twc_core_t c;
  log_cap_t logs;
  twc_core_init(&c);
  memset(&logs, 0, sizeof(logs));
  twc_core_set_log_callback(&c, log_cap_cb, &logs);

  const twc_rx_diag_t *d = twc_core_get_rx_diag(&c);
  CHECK(d != NULL);
  CHECK(d->checksum_fail == 0);
  CHECK(d->unhandled == 0);
  CHECK(d->n_types == 0);
  char list[64];
  CHECK(twc_core_format_unhandled_types(&c, list, sizeof(list)) > 0);
  CHECK(strcmp(list, "unknown=[]") == 0);

  // Known handled types (E0, E1, EB) must not increment unhandled.
  uint8_t fr[32];
  uint8_t pl_e0[11] = {0xF0, 0x0D, 0x00, 0x05, 0xDC, 0x00, 0x00, 0, 0, 0, 0};
  size_t n = build_cmd(0xFD, 0xE0, 0x8988, pl_e0, sizeof(pl_e0), fr, sizeof(fr));
  CHECK(n == 16);
  CHECK(twc_core_handle_frame(&c, fr, n, 1000));
  CHECK(d->unhandled == 0);
  CHECK(d->checksum_fail == 0);

  n = build_cmd(0xFC, 0xE1, 0xF00D, NULL, 0, fr, sizeof(fr));
  CHECK(n >= 5);
  CHECK(twc_core_handle_frame(&c, fr, n, 1100));
  CHECK(d->unhandled == 0);

  // Real captured meter frame
  n = hex("FD EB 89 88 00 00 87 1A 00 E4 00 E4 00 E4 16 16 17 00 00 8C", fr);
  CHECK(twc_core_handle_frame(&c, fr, n, 1200));
  CHECK(d->unhandled == 0);
  CHECK(d->checksum_fail == 0);

  // Checksum mismatch on a known type: counted as checksum_fail, not unhandled.
  n = hex("FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", fr);
  fr[n - 1] ^= 0xFF;
  int logs_before = logs.count;
  CHECK(!twc_core_handle_frame(&c, fr, n, 1300));
  CHECK(d->checksum_fail == 1);
  CHECK(d->unhandled == 0);
  CHECK(logs.count == logs_before + 1);
  CHECK(logs.levels[logs.count - 1] == TWC_LOG_WARNING);
  CHECK(strstr(logs.msgs[logs.count - 1], "Checksum FAILED") != NULL);

  // Too-short / malformed is not a checksum failure.
  uint8_t short_fr[3] = {0xFD, 0xE0, 0x00};
  CHECK(!twc_core_handle_frame(&c, short_fr, 3, 1400));
  CHECK(d->checksum_fail == 1);
  CHECK(d->unhandled == 0);

  // Unknown type FDEE? Wait, EE is VIN_HI and IS handled. Use FD F2 (not in enum).
  uint8_t pl[4] = {0x00, 0x00, 0x00, 0x00};
  n = build_cmd(0xFD, 0xF2, 0x8988, pl, sizeof(pl), fr, sizeof(fr));
  CHECK(n == 9);
  logs_before = logs.count;
  CHECK(twc_core_handle_frame(&c, fr, n, 1500));  // checksum ok, still returns true
  CHECK(d->checksum_fail == 1);
  CHECK(d->unhandled == 1);
  CHECK(d->n_types == 1);
  CHECK(d->types[0].type_id == 0xFDF2);
  CHECK(d->types[0].count == 1);
  CHECK(logs.count == logs_before + 1);
  CHECK(logs.levels[logs.count - 1] == TWC_LOG_WARNING);
  CHECK(strcmp(logs.msgs[logs.count - 1],
               "Unhandled type=FDF2 len=9 src=8988 first=FD F2 89 88 00 00 00 00") == 0);

  // Same type again: DEBUG, count increments, no new table slot.
  logs_before = logs.count;
  CHECK(twc_core_handle_frame(&c, fr, n, 1600));
  CHECK(d->unhandled == 2);
  CHECK(d->n_types == 1);
  CHECK(d->types[0].count == 2);
  CHECK(logs.count == logs_before + 1);
  CHECK(logs.levels[logs.count - 1] == TWC_LOG_DEBUG);
  CHECK(strstr(logs.msgs[logs.count - 1], "Unhandled type=FDF2") != NULL);

  // A second unknown type (FB B4 plug-state request is known in the enum
  // but has no RX handler — counts as unhandled).
  n = build_cmd(0xFB, 0xB4, 0xF00D, pl, sizeof(pl), fr, sizeof(fr));
  logs_before = logs.count;
  CHECK(twc_core_handle_frame(&c, fr, n, 1700));
  CHECK(d->unhandled == 3);
  CHECK(d->n_types == 2);
  CHECK(d->types[1].type_id == 0xFBB4);
  CHECK(d->types[1].count == 1);
  CHECK(logs.levels[logs.count - 1] == TWC_LOG_WARNING);
  CHECK(twc_core_format_unhandled_types(&c, list, sizeof(list)) > 0);
  CHECK(strcmp(list, "unknown=[FDF2:2,FBB4:1]") == 0);
}

static void test_rx_diag_unhandled_table_overflow(void) {
  twc_core_t c;
  twc_core_init(&c);
  const twc_rx_diag_t *d = twc_core_get_rx_diag(&c);
  uint8_t fr[32];
  uint8_t pl[1] = {0x00};

  for (int i = 0; i < TWC_RX_UNHANDLED_MAX_TYPES; ++i) {
    // Distinct unknown cmds 0x20..0x2F (not in the handled RX set)
    size_t n = build_cmd(0xFD, (uint8_t)(0x20 + i), 0x8988, pl, 1, fr, sizeof(fr));
    CHECK(twc_core_handle_frame(&c, fr, n, 2000 + (uint32_t)i));
  }
  CHECK(d->n_types == TWC_RX_UNHANDLED_MAX_TYPES);
  CHECK(d->unhandled == (uint32_t)TWC_RX_UNHANDLED_MAX_TYPES);
  CHECK(d->unhandled_overflow == 0);

  // 17th distinct type: table stays full, overflow increments, unhandled still grows.
  size_t n = build_cmd(0xFD, 0x30, 0x8988, pl, 1, fr, sizeof(fr));
  CHECK(twc_core_handle_frame(&c, fr, n, 3000));
  CHECK(d->n_types == TWC_RX_UNHANDLED_MAX_TYPES);
  CHECK(d->unhandled == (uint32_t)TWC_RX_UNHANDLED_MAX_TYPES + 1);
  CHECK(d->unhandled_overflow == 1);

  char list[320];
  twc_core_format_unhandled_types(&c, list, sizeof(list));
  CHECK(strstr(list, "overflow=1") != NULL);
  CHECK(list[0] == 'u');
  // A type already in the table still increments its own counter, not overflow.
  n = build_cmd(0xFD, 0x20, 0x8988, pl, 1, fr, sizeof(fr));
  CHECK(twc_core_handle_frame(&c, fr, n, 3100));
  CHECK(d->types[0].count == 2);
  CHECK(d->unhandled_overflow == 1);
  CHECK(d->unhandled == (uint32_t)TWC_RX_UNHANDLED_MAX_TYPES + 2);
}

int main(void) {
  test_meter_decode();
  test_session_current_reaches_twc();
  test_resend_stops_when_matching();
  test_zero_amps_is_sent_while_charging();
  test_global_max_scaling_kept();
  test_rx_diag_checksum_and_unhandled();
  test_rx_diag_unhandled_table_overflow();
  if (g_failures) {
    printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
