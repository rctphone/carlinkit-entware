/* mfichip — the MFi authentication coprocessor on this dongle's i2c bus.
 *
 * Measured on the unit (2026-10-16): i2c-1, address 0x10 (0x11 mirrors it), device version 0x03,
 * protocol major 0x02 — an MFi 2.0C part: RSA-1024, SHA-1, 908-byte certificate, 128-byte
 * signatures, and only SHA-1-sized (20 byte) challenges accepted. 16 and 32 byte challenges come
 * back with error code 0x01.
 */
#ifndef MFICHIP_H
#define MFICHIP_H

#include <stddef.h>

#define MFI_CERT_MAX 1024
#define MFI_CHALLENGE_MAX 128
#define MFI_SIGNATURE_MAX 512

/* Where the coprocessor answered on this unit. */
#define MFI_DEFAULT_BUS 1
#define MFI_DEFAULT_ADDR 0x10

struct mfi_chip {
    int fd;
    int bus;
    int addr;
    int protocol_major; /* -1 until read */
};

/* Opens /dev/i2c-<bus> at <addr>. Zero on success; on failure the fd is -1. */
int mfi_open(struct mfi_chip *chip, int bus, int addr);
void mfi_close(struct mfi_chip *chip);

/* 2 for a 2.0C (RSA/SHA-1) part, 3 for 3.0 (ECDSA/SHA-256); -1 on error. */
int mfi_protocol_major(struct mfi_chip *chip);

/* Reads the accessory certificate. Returns its length, or -1. */
int mfi_read_certificate(struct mfi_chip *chip, unsigned char *out, size_t cap);

/* Signs a challenge (the chip takes 20 bytes). Returns the signature length, or -1. */
int mfi_sign(struct mfi_chip *chip, const unsigned char *challenge, size_t len, unsigned char *out,
             size_t cap);

/* Last error code the chip reported, for a log line. */
int mfi_last_error(const struct mfi_chip *chip);

#endif
