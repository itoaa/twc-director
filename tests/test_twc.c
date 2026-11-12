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
// Meter and heartbeat current scaling
// ---------------------------------------------------------------------------

// Build a 15-byte EB payload with known volts / current-units and decode it.
static void decode_meter_units(uint8_t i1, uint8_t i2, uint8_t i3, twc_meter_data_t *m) {
  uint8_t pl[15] = {
      0, 0, 0, 0,
      0x00, 0xE4, 0x00, 0xE4, 0x00, 0xE4,
      i1, i2, i3, 0, 0};
  CHECK(twc_decode_meter_payload(pl, sizeof(pl), m));
  CHECK_NEAR(m->phase_l1_v, 228);
  CHECK_NEAR(m->phase_l2_v, 228);
  CHECK_NEAR(m->phase_l3_v, 228);
}

static void test_meter_decode(void) {
  uint8_t f[64];
  size_t n;
  const uint8_t *hdr, *pl;
  size_t pl_len;
  twc_meter_data_t m;

  // Real capture, car charging, 0x8988. Currents 0x16,0x16,0x17 = 11.0, 11.0, 11.5 A
  // (uint8 in 0.5 A units). 0x16 as 22 A, 1.6 A or 0.22 A would not match ~7.6 kW.
  n = hex("FD EB 89 88 00 00 87 1A 00 E4 00 E4 00 E4 16 16 17 00 00 8C", f);
  CHECK(twc_decode_frame(f, n, &hdr, &pl, &pl_len));
  CHECK(twc_decode_meter_payload(pl, pl_len, &m));
  CHECK_NEAR(m.total_energy_kwh, 34586);
  CHECK_NEAR(m.phase_l1_v, 228); CHECK_NEAR(m.phase_l2_v, 228); CHECK_NEAR(m.phase_l3_v, 228);
  CHECK_NEAR(m.phase_l1_a, 11.0); CHECK_NEAR(m.phase_l2_a, 11.0); CHECK_NEAR(m.phase_l3_a, 11.5);
  // Guard wrong scales: byte 0x16 is 11.0 A, not 22 / 1.6 / 0.22.
  CHECK(fabsf(m.phase_l1_a - 22.0f) > 1.0f);
  CHECK(fabsf(m.phase_l1_a - 1.6f) > 0.1f);
  CHECK(fabsf(m.phase_l1_a - 0.22f) > 0.1f);

  // Real capture, idle, 0x9983
  n = hex("FD EB 99 83 00 00 1D 8F 00 E6 00 E6 00 E5 00 00 00 00 00 64", f);
  CHECK(twc_decode_frame(f, n, &hdr, &pl, &pl_len));
  CHECK(twc_decode_meter_payload(pl, pl_len, &m));
  CHECK_NEAR(m.total_energy_kwh, 7567);
  CHECK_NEAR(m.phase_l1_v, 230); CHECK_NEAR(m.phase_l2_v, 230); CHECK_NEAR(m.phase_l3_v, 229);
  CHECK_NEAR(m.phase_l1_a, 0); CHECK_NEAR(m.phase_l2_a, 0); CHECK_NEAR(m.phase_l3_a, 0);

  // Synthetic unit scale: 0, 0.5, 13.0, 13.5, 20.0, 127.5 A (max uint8 / 2)
  decode_meter_units(0x00, 0x01, 0x1A, &m);
  CHECK_NEAR(m.phase_l1_a, 0.0f);
  CHECK_NEAR(m.phase_l2_a, 0.5f);
  CHECK_NEAR(m.phase_l3_a, 13.0f);
  decode_meter_units(0x1B, 0x28, 0xFF, &m);
  CHECK_NEAR(m.phase_l1_a, 13.5f);
  CHECK_NEAR(m.phase_l2_a, 20.0f);
  CHECK_NEAR(m.phase_l3_a, 127.5f);

  // Too short payload is rejected
  CHECK(!twc_decode_meter_payload(pl, 14, &m));
}

static void test_heartbeat_current_scale(void) {
  // E0: current_available / current_delivered are uint16 centiamps (0.01 A).
  uint8_t pl[11] = {0xF0, 0x0D, 0x01, 0x05, 0x14, 0x05, 0x47, 0, 0, 0, 0};
  twc_heartbeat_data_t hb;
  CHECK(twc_decode_heartbeat_payload(pl, sizeof(pl), &hb));
  CHECK(hb.current_available_centiamps == 1300);   // 0x0514 = 13.00 A
  CHECK(hb.current_delivered_centiamps == 1351);   // 0x0547 = 13.51 A
  CHECK_NEAR((float)hb.current_available_centiamps / 100.0f, 13.00f);
  CHECK_NEAR((float)hb.current_delivered_centiamps / 100.0f, 13.51f);

  // E2: peripheral negotiation current_available is also centiamps.
  uint8_t e2[11] = {0x01, 0x0C, 0x80, 0, 0, 0, 0, 0, 0, 0, 0};
  twc_peripheral_negotiation_data_t pn;
  CHECK(twc_decode_peripheral_negotiation_payload(e2, sizeof(e2), &pn));
  CHECK(pn.current_available_centiamps == 3200);
  CHECK_NEAR((float)pn.current_available_centiamps / 100.0f, 32.00f);
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

// Last 0x05 or 0x09 heartbeat sent to addr. Ignores later NOP (state 0) beats.
static int last_setpoint_hb(const tx_log_t *l, uint16_t addr, unsigned *avail) {
  for (int i = l->count - 1; i >= 0; --i) {
    const uint8_t *f = l->frames[i];
    if (f[0] == 0xFB && f[1] == 0xE0 && f[4] == (addr >> 8) && f[5] == (addr & 0xFF) &&
        (f[6] == 0x05 || f[6] == 0x09)) {
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

typedef struct {
  twc_log_level_t levels[16];
  char msgs[16][256];
  int count;
} log_cap_t;

static void log_cap_cb(twc_log_level_t level, const char *message, void *user) {
  log_cap_t *c = (log_cap_t *)user;
  if (!c || c->count >= 16) return;
  c->levels[c->count] = level;
  snprintf(c->msgs[c->count], sizeof(c->msgs[c->count]), "%s", message ? message : "");
  c->count++;
}

static void test_meter_debug_log(void) {
  twc_core_t c;
  tx_log_t l;
  log_cap_t logs;
  init_core(&c, &l);
  memset(&logs, 0, sizeof(logs));
  twc_core_set_log_callback(&c, log_cap_cb, &logs);

  // Real charging capture: must log type, hex, and decoded 11.0/11.0/11.5 A at DEBUG.
  rx(&c, "FD EB 89 88 00 00 87 1A 00 E4 00 E4 00 E4 16 16 17 00 00 8C", 1000);
  int found = 0;
  for (int i = 0; i < logs.count; i++) {
    if (logs.levels[i] != TWC_LOG_DEBUG) continue;
    if (strstr(logs.msgs[i], "EB meter") == NULL) continue;
    found++;
    CHECK(strstr(logs.msgs[i], "src=0x8988") != NULL);
    CHECK(strstr(logs.msgs[i], "00 00 87 1A 00 E4 00 E4 00 E4 16 16 17 00 00") != NULL);
    CHECK(strstr(logs.msgs[i], "L1=228V/11.0A") != NULL);
    CHECK(strstr(logs.msgs[i], "L2=228V/11.0A") != NULL);
    CHECK(strstr(logs.msgs[i], "L3=228V/11.5A") != NULL);
    CHECK(strstr(logs.msgs[i], "energy=34586kWh") != NULL);
  }
  CHECK(found == 1);
}

// ---------------------------------------------------------------------------
// Last sent 0x05 / 0x09 setpoint (centiamp-rounded, after clamp)
// ---------------------------------------------------------------------------
static void test_setpoint_sent_not_before_tx(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  CHECK(!twc_core_has_sent_setpoint(twc_core_get_device_by_address(&c, 0x8988)));

  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 1000);
  twc_core_device_t *d = twc_core_get_device_by_address(&c, 0x8988);
  CHECK(d != NULL);
  CHECK(!twc_core_has_sent_setpoint(d));

  twc_core_set_desired_session_current(&c, 0x8988, 12.0f);
  CHECK_NEAR(d->applied_session_current_a, 12.0f);
  // Applied is computed immediately; nothing has been placed on the bus yet.
  CHECK(!twc_core_has_sent_setpoint(d));
}

static void test_setpoint_sent_09_centiamps(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 1000);
  twc_core_set_desired_session_current(&c, 0x8988, 13.51f);
  run_ticks(&c, 1000, 4000);

  twc_core_device_t *d = twc_core_get_device_by_address(&c, 0x8988);
  CHECK(twc_core_has_sent_setpoint(d));
  CHECK(twc_core_get_last_sent_setpoint_cmd(d) == 0x09u);
  CHECK_NEAR(twc_core_get_last_sent_setpoint_a(d), 13.51f);

  unsigned avail = 0;
  int st = last_setpoint_hb(&l, 0x8988, &avail);
  CHECK(st == 0x09);
  CHECK(avail == 1351);
  CHECK_NEAR((float)avail / 100.0f, twc_core_get_last_sent_setpoint_a(d));
}

static void test_setpoint_sent_05_centiamps(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  // Ready / not charging, so 0x09 is not pending; 0x05 can go out.
  rx_e0(&c, 0x8988, 0x00, 0, 0, 1000);
  twc_core_set_desired_initial_current(&c, 0x8988, 13.51f);
  twc_core_device_t *d = twc_core_get_device_by_address(&c, 0x8988);
  CHECK(!twc_core_has_sent_setpoint(d));
  run_ticks(&c, 1000, 4000);

  CHECK(twc_core_has_sent_setpoint(d));
  CHECK(twc_core_get_last_sent_setpoint_cmd(d) == 0x05u);
  CHECK_NEAR(twc_core_get_last_sent_setpoint_a(d), 13.51f);

  bool seen05 = false;
  unsigned avail = 0;
  for (int i = 0; i < l.count; i++) {
    const uint8_t *f = l.frames[i];
    if (f[0] == 0xFB && f[1] == 0xE0 && f[6] == 0x05) {
      seen05 = true;
      avail = ((unsigned)f[7] << 8) | f[8];
    }
  }
  CHECK(seen05);
  CHECK(avail == 1351);
}

static void test_setpoint_sent_clamped(void) {
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  twc_core_set_max_current(&c, 0x9983, 15.0f);
  rx(&c, "FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66", 1000);
  rx_e0(&c, 0x9983, 0x01, 1500, 1170, 1000);
  // 9983 is set last, so it is the priority device and keeps 12 A; 8988 is clamped to 3 A.
  twc_core_set_desired_session_current(&c, 0x8988, 12.0f);
  twc_core_set_desired_session_current(&c, 0x9983, 12.0f);
  twc_core_device_t *a = twc_core_get_device_by_address(&c, 0x8988);
  twc_core_device_t *b = twc_core_get_device_by_address(&c, 0x9983);
  CHECK_NEAR(b->applied_session_current_a, 12.0f);
  CHECK_NEAR(a->applied_session_current_a, 3.0f);
  CHECK(!twc_core_has_sent_setpoint(a));

  run_ticks(&c, 1000, 8000);
  CHECK(twc_core_has_sent_setpoint(a));
  CHECK(twc_core_get_last_sent_setpoint_cmd(a) == 0x09u);
  CHECK_NEAR(twc_core_get_last_sent_setpoint_a(a), 3.00f);
  CHECK(twc_core_has_sent_setpoint(b));
  CHECK_NEAR(twc_core_get_last_sent_setpoint_a(b), 12.00f);

  unsigned avail_a = 0, avail_b = 0;
  CHECK(last_setpoint_hb(&l, 0x8988, &avail_a) == 0x09);
  CHECK(last_setpoint_hb(&l, 0x9983, &avail_b) == 0x09);
  CHECK(avail_a == 300);
  CHECK(avail_b == 1200);
}


static void test_zero_amps_is_sent_as_initial_05(void) {
  // 0 A is a real initial-current value (not "unset"). If desired and applied
  // both already start at 0, pending_initial_current_cmd is not set until a
  // car connects or applied is reset to -1. This test first applies a non-zero
  // initial so the subsequent change to 0 A actually queues a 0x05.
  twc_core_t c; tx_log_t l;
  init_core(&c, &l);
  rx_e0(&c, 0x8988, 0x00, 0, 0, 1000);
  twc_core_set_desired_initial_current(&c, 0x8988, 6.0f);
  run_ticks(&c, 1000, 4000);

  twc_core_device_t *d = twc_core_get_device_by_address(&c, 0x8988);
  CHECK(twc_core_get_last_sent_setpoint_cmd(d) == 0x05u);
  CHECK_NEAR(twc_core_get_last_sent_setpoint_a(d), 6.0f);

  twc_core_set_desired_initial_current(&c, 0x8988, 0.0f);
  CHECK_NEAR(d->desired_initial_current_a, 0.0f);
  rx_e0(&c, 0x8988, 0x00, 0, 0, 5000);
  run_ticks(&c, 5000, 9000);

  CHECK(twc_core_has_sent_setpoint(d));
  CHECK(twc_core_get_last_sent_setpoint_cmd(d) == 0x05u);
  CHECK_NEAR(twc_core_get_last_sent_setpoint_a(d), 0.0f);

  unsigned avail = 0;
  CHECK(last_setpoint_hb(&l, 0x8988, &avail) == 0x05);
  CHECK(avail == 0);
}

int main(void) {
  test_meter_decode();
  test_heartbeat_current_scale();
  test_meter_debug_log();
  test_session_current_reaches_twc();
  test_resend_stops_when_matching();
  test_zero_amps_is_sent_while_charging();
  test_global_max_scaling_kept();
  test_setpoint_sent_not_before_tx();
  test_setpoint_sent_09_centiamps();
  test_setpoint_sent_05_centiamps();
  test_setpoint_sent_clamped();
  test_zero_amps_is_sent_as_initial_05();
  if (g_failures) {
    printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
