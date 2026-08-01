#pragma once

// Minimal public declarations from ricmoo/QRCode 0.0.1. The hybrid
// Arduino+ESP-IDF build also provides an unrelated qrcode.h, so including the
// dependency by its generic filename can select the ESP-IDF header instead.
#include <stdint.h>

#define ECC_LOW 0

typedef struct QRCode {
  uint8_t version;
  uint8_t size;
  uint8_t ecc;
  uint8_t mode;
  uint8_t mask;
  uint8_t* modules;
} QRCode;

extern "C" {
uint16_t qrcode_getBufferSize(uint8_t version);
int8_t qrcode_initText(QRCode* qrcode, uint8_t* modules, uint8_t version, uint8_t ecc, const char* data);
bool qrcode_getModule(QRCode* qrcode, uint8_t x, uint8_t y);
}
