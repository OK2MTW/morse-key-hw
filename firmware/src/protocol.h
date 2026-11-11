// SPDX-License-Identifier: MIT
// USB MIDI SysEx settings protocol (docs/midi-settings-spec.md, section 5). Pure logic:
// bytes come in through protocol_rx_byte(), responses leave through the ProtocolHost callbacks.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PROTO_VERSION 0x01

enum {
  CMD_GET_INFO = 0x01, CMD_GET_ALL = 0x02, CMD_GET = 0x03, CMD_SET = 0x04,
  CMD_SAVE = 0x05, CMD_RESET_DEFAULTS = 0x06, CMD_ENTER_BOOTLOADER = 0x07,
  RSP_INFO = 0x41, RSP_VALUE = 0x42, RSP_ACK = 0x43, RSP_NAK = 0x44, RSP_CHANGED = 0x45,
};

enum {
  ERR_UNKNOWN_CMD = 0x01, ERR_UNKNOWN_ID = 0x02, ERR_RANGE = 0x03, ERR_BUSY = 0x04,
  ERR_UNSUPPORTED = 0x05, ERR_FLASH = 0x06, ERR_BAD_CONFIRM = 0x07, ERR_BAD_FRAME = 0x08,
};

typedef struct {
  void (*send)(const uint8_t *data, uint32_t len);   // raw MIDI bytes (complete SysEx frames)
  uint8_t (*chip)(void);                             // 0 F411, 1 F401
  bool (*busy)(void);                                // keyer active or PTT on
  void (*request_save)(uint8_t seq);                 // answer later with protocol_send_ack/nak
  void (*request_bootloader)(void);                  // called after the ACK was queued
  void (*settings_changed)(uint8_t id);              // a SET or RESET changed values (apply + autosave)
} ProtocolHost;

void protocol_init(const ProtocolHost *host);
void protocol_rx_byte(uint8_t b);
void protocol_send_ack(uint8_t cmd, uint8_t seq);
void protocol_send_nak(uint8_t cmd, uint8_t seq, uint8_t err);
void protocol_notify_changed(uint8_t id);           // device-side change (KEY button)
