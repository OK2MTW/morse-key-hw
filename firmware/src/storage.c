// SPDX-License-Identifier: MIT
// Append-only 64-byte records in one 128 KB sector. The newest record with a valid CRC wins.
// Only SETTINGS_FLASH_SECTOR is ever erased or written; option bytes are never touched.
#include "storage.h"
#include <stddef.h>
#include <string.h>
#include "config.h"
#include "settings.h"
#include "stm32f4xx_hal.h"

#define RECORD_MAGIC  0x59454B43u   // "CKEY"
#define RECORD_LAYOUT 1u
#define MAX_VALUES    24u

typedef struct {
  uint32_t magic;
  uint16_t layout;
  uint16_t count;          // number of values stored (ids SET_FIRST .. SET_FIRST+count-1)
  uint32_t seq;
  uint16_t values[MAX_VALUES];
  uint32_t crc;
} Record;                  // 64 bytes

_Static_assert(sizeof(Record) == 64, "record must be 64 bytes");
_Static_assert(SET_COUNT <= MAX_VALUES, "too many settings for the record");

#define SLOT_COUNT (SETTINGS_FLASH_SIZE / sizeof(Record))

static uint32_t crc32(const void *data, uint32_t len) {
  const uint8_t *p = data;
  uint32_t c = 0xFFFFFFFFu;
  while (len--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static const Record *slot(uint32_t i) {
  return (const Record *)(SETTINGS_FLASH_ADDR + i * sizeof(Record));
}

static bool slot_blank(const Record *r) {
  const uint32_t *w = (const uint32_t *)r;
  for (unsigned i = 0; i < sizeof(Record) / 4; i++) {
    if (w[i] != 0xFFFFFFFFu) return false;
  }
  return true;
}

static bool record_valid(const Record *r) {
  return r->magic == RECORD_MAGIC && r->layout == RECORD_LAYOUT && r->count <= MAX_VALUES &&
         r->crc == crc32(r, offsetof(Record, crc));
}

// Index of the newest valid record, -1 if none; *firstBlank = first blank slot or SLOT_COUNT.
static int32_t scan(uint32_t *firstBlank) {
  int32_t newest = -1;
  uint32_t newestSeq = 0;
  *firstBlank = SLOT_COUNT;
  for (uint32_t i = 0; i < SLOT_COUNT; i++) {
    const Record *r = slot(i);
    if (slot_blank(r)) {
      *firstBlank = i;
      break;                // records are appended in order, nothing follows a blank slot
    }
    if (record_valid(r) && (newest < 0 || r->seq > newestSeq)) {
      newest = (int32_t)i;
      newestSeq = r->seq;
    }
  }
  return newest;
}

bool storage_load(void) {
  uint32_t blank;
  int32_t i = scan(&blank);
  if (i < 0) return false;
  const Record *r = slot((uint32_t)i);
  for (uint16_t k = 0; k < r->count && k < SET_COUNT; k++) {
    // An out-of-range value (older firmware, other limits) keeps the default.
    settings_set((uint8_t)(SET_FIRST + k), r->values[k]);
  }
  return true;
}

static bool erase_sector(void) {
  FLASH_EraseInitTypeDef e = {0};
  e.TypeErase = FLASH_TYPEERASE_SECTORS;
  e.Sector = SETTINGS_FLASH_SECTOR;
  e.NbSectors = 1;
  e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  uint32_t badSector = 0;
  return HAL_FLASHEx_Erase(&e, &badSector) == HAL_OK;
}

bool storage_save(void) {
  uint32_t blank;
  int32_t newest = scan(&blank);
  uint32_t nextSeq = newest >= 0 ? slot((uint32_t)newest)->seq + 1 : 1;

  Record rec;
  memset(&rec, 0xFF, sizeof rec);
  rec.magic = RECORD_MAGIC;
  rec.layout = RECORD_LAYOUT;
  rec.count = SET_COUNT;
  rec.seq = nextSeq;
  for (uint16_t k = 0; k < SET_COUNT; k++) rec.values[k] = settings_get((uint8_t)(SET_FIRST + k));
  rec.crc = crc32(&rec, offsetof(Record, crc));

  HAL_FLASH_Unlock();
  bool ok = true;
  if (blank >= SLOT_COUNT) {          // sector full (or damaged): start again at slot 0
    ok = erase_sector();
    blank = 0;
  }
  uint32_t addr = SETTINGS_FLASH_ADDR + blank * sizeof(Record);
  const uint32_t *w = (const uint32_t *)&rec;
  for (unsigned i = 0; ok && i < sizeof rec / 4; i++) {
    if (addr + i * 4 < SETTINGS_FLASH_ADDR || addr + i * 4 >= SETTINGS_FLASH_ADDR + SETTINGS_FLASH_SIZE) {
      ok = false;                     // never leave sector 5
      break;
    }
    ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i * 4, w[i]) == HAL_OK;
  }
  HAL_FLASH_Lock();
  // The flash data cache may still hold the old (blank) contents of the slot; flush it so the
  // read-back below and the next scan() see what is really in the flash.
  __HAL_FLASH_DATA_CACHE_DISABLE();
  __HAL_FLASH_DATA_CACHE_RESET();
  __HAL_FLASH_DATA_CACHE_ENABLE();
  return ok && memcmp((const void *)addr, &rec, sizeof rec) == 0;
}
