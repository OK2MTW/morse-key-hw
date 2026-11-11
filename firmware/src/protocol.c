// SPDX-License-Identifier: MIT
#include "protocol.h"
#include <string.h>
#include "config.h"
#include "settings.h"

// Frame: F0 7D 43 4B <ver> <cmd> <seq> <payload...> F7  (all inner bytes 7-bit)
#define SYSEX_START 0xF0
#define SYSEX_END   0xF7
#define MFR_ID      0x7D   // non-commercial / educational
#define TAG_C       0x43
#define TAG_K       0x4B
#define MAX_FRAME   48

static const ProtocolHost *host;
static uint8_t frame[MAX_FRAME];
static uint8_t frameLen;
static bool inSysex, overflow;

void protocol_init(const ProtocolHost *h) {
  host = h;
  inSysex = false;
  frameLen = 0;
}

static void send_frame(uint8_t rsp, uint8_t seq, const uint8_t *payload, uint8_t len) {
  uint8_t out[MAX_FRAME + 8];
  uint8_t n = 0;
  out[n++] = SYSEX_START;
  out[n++] = MFR_ID;
  out[n++] = TAG_C;
  out[n++] = TAG_K;
  out[n++] = PROTO_VERSION;
  out[n++] = rsp;
  out[n++] = seq & 0x7F;
  for (uint8_t i = 0; i < len && n < sizeof out - 1; i++) out[n++] = payload[i] & 0x7F;
  out[n++] = SYSEX_END;
  host->send(out, n);
}

void protocol_send_ack(uint8_t cmd, uint8_t seq) {
  uint8_t p[] = {cmd};
  send_frame(RSP_ACK, seq, p, sizeof p);
}

void protocol_send_nak(uint8_t cmd, uint8_t seq, uint8_t err) {
  uint8_t p[] = {cmd, err};
  send_frame(RSP_NAK, seq, p, sizeof p);
}

static void send_value(uint8_t rsp, uint8_t seq, uint8_t id) {
  uint16_t v = settings_get(id);
  uint8_t p[] = {id, (uint8_t)(v & 0x7F), (uint8_t)((v >> 7) & 0x7F)};
  send_frame(rsp, seq, p, sizeof p);
}

void protocol_notify_changed(uint8_t id) {
  if (host) send_value(RSP_CHANGED, 0, id);
}

static void send_info(uint8_t seq) {
  static const char name[] = "CwKey";
  uint8_t p[16];
  uint8_t n = 0;
  p[n++] = PROTO_VERSION;
  p[n++] = CWKEY_VERSION_MAJOR;
  p[n++] = CWKEY_VERSION_MINOR;
  p[n++] = CWKEY_VERSION_PATCH;
  p[n++] = host->chip();
  p[n++] = 0;                     // reserved (was "sound variant" in an earlier draft)
  p[n++] = 0x07;                  // capabilities: tone settings, PTT outputs, bootloader entry
  p[n++] = sizeof name - 1;
  memcpy(&p[n], name, sizeof name - 1);
  n += sizeof name - 1;
  send_frame(RSP_INFO, seq, p, n);
}

static void handle_frame(const uint8_t *f, uint8_t len) {
  // f excludes F0/F7: 7D 43 4B ver cmd seq payload...
  if (len < 3 || f[0] != MFR_ID || f[1] != TAG_C || f[2] != TAG_K) return;  // not for us
  if (len < 6) return;                                                      // too short to answer
  uint8_t ver = f[3], cmd = f[4], seq = f[5];
  const uint8_t *p = f + 6;
  uint8_t plen = len - 6;
  if (cmd >= 0x40) return;                                                  // our own response format
  if (ver != PROTO_VERSION) {
    protocol_send_nak(cmd, seq, ERR_BAD_FRAME);
    return;
  }

  switch (cmd) {
    case CMD_GET_INFO:
      send_info(seq);
      break;

    case CMD_GET_ALL:
      for (uint8_t id = SET_FIRST; id <= SET_LAST; id++) send_value(RSP_VALUE, seq, id);
      protocol_send_ack(cmd, seq);
      break;

    case CMD_GET:
      if (plen < 1) { protocol_send_nak(cmd, seq, ERR_BAD_FRAME); break; }
      if (!settings_known(p[0])) { protocol_send_nak(cmd, seq, ERR_UNKNOWN_ID); break; }
      send_value(RSP_VALUE, seq, p[0]);
      break;

    case CMD_SET: {
      if (plen < 3) { protocol_send_nak(cmd, seq, ERR_BAD_FRAME); break; }
      uint16_t value = (uint16_t)(p[1] | (p[2] << 7));
      uint8_t err = settings_set(p[0], value);
      if (err != SETTINGS_OK) { protocol_send_nak(cmd, seq, err); break; }
      host->settings_changed(p[0]);
      protocol_send_ack(cmd, seq);
      break;
    }

    case CMD_SAVE:
      host->request_save(seq);   // ACK / NAK follows when the flash write is done
      break;

    case CMD_RESET_DEFAULTS:
      settings_init_defaults();
      host->settings_changed(0);
      protocol_send_ack(cmd, seq);
      break;

    case CMD_ENTER_BOOTLOADER:
      if (plen < 3 || p[0] != 0x55 || p[1] != 0x46 || p[2] != 0x32) {   // "UF2"
        protocol_send_nak(cmd, seq, ERR_BAD_CONFIRM);
        break;
      }
      if (host->busy()) { protocol_send_nak(cmd, seq, ERR_BUSY); break; }
      protocol_send_ack(cmd, seq);
      host->request_bootloader();
      break;

    default:
      protocol_send_nak(cmd, seq, ERR_UNKNOWN_CMD);
      break;
  }
}

void protocol_rx_byte(uint8_t b) {
  if (b >= 0xF8) return;                  // real-time messages may appear anywhere, ignore
  if (b == SYSEX_START) {
    inSysex = true;
    overflow = false;
    frameLen = 0;
    return;
  }
  if (!inSysex) return;                   // notes, CC, ... are ignored
  if (b == SYSEX_END) {
    inSysex = false;
    if (!overflow) handle_frame(frame, frameLen);
    return;
  }
  if (b & 0x80) {                         // any other status byte aborts the SysEx
    inSysex = false;
    return;
  }
  if (frameLen < MAX_FRAME) frame[frameLen++] = b;
  else overflow = true;
}
