/* mfichip — see mfichip.h. The register protocol is the MFi 2.0C one: select a register, then
 * read or write its bytes, with a busy retry because the chip NAKs while it works. */

#define _POSIX_C_SOURCE 200809L

#include "mfichip.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>

#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif

#define REG_DEVICE_VERSION 0x00
#define REG_PROTOCOL_MAJOR 0x02
#define REG_ERROR_CODE 0x05
#define REG_AUTH_CONTROL_STATUS 0x10
#define REG_SIGNATURE_LENGTH 0x11
#define REG_SIGNATURE_DATA 0x12
#define REG_CHALLENGE_LENGTH 0x20
#define REG_CHALLENGE_DATA 0x21
#define REG_CERT_LENGTH 0x30
#define REG_CERT_DATA 0x31

#define AUTH_START 0x01
#define AUTH_DONE 0x10

#define BUSY_RETRY_MS 1
#define IO_TIMEOUT_MS 2000
#define AUTH_POLL_MS 10
/* A signature lands in well under a second, but the first one on a cold chip can be slow. */
#define AUTH_TIMEOUT_MS 8000
#define CERT_TRIES 6
#define CERT_RETRY_MS 20

static void sleep_ms(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* 64-bit: a 32-bit long of milliseconds since boot wraps after 24.8 days of uptime. */
static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int reg_read(struct mfi_chip *chip, unsigned char reg, unsigned char *out, size_t n) {
    int64_t deadline = now_ms() + IO_TIMEOUT_MS;
    for (;;) {
        if (write(chip->fd, &reg, 1) == 1 && (ssize_t)read(chip->fd, out, n) == (ssize_t)n) {
            return 0;
        }
        if (now_ms() >= deadline) {
            return -1;
        }
        sleep_ms(BUSY_RETRY_MS);
    }
}

static int reg_write(struct mfi_chip *chip, unsigned char reg, const unsigned char *data, size_t n) {
    unsigned char frame[1 + MFI_CHALLENGE_MAX];
    if (n > MFI_CHALLENGE_MAX) {
        return -1;
    }
    frame[0] = reg;
    memcpy(frame + 1, data, n);
    int64_t deadline = now_ms() + IO_TIMEOUT_MS;
    for (;;) {
        if (write(chip->fd, frame, n + 1) == (ssize_t)(n + 1)) {
            return 0;
        }
        if (now_ms() >= deadline) {
            return -1;
        }
        sleep_ms(BUSY_RETRY_MS);
    }
}

static int reg_read_len(struct mfi_chip *chip, unsigned char reg, size_t *len) {
    unsigned char buf[2];
    if (reg_read(chip, reg, buf, 2) != 0) {
        return -1;
    }
    *len = ((size_t)buf[0] << 8) | buf[1];
    return 0;
}

int mfi_open(struct mfi_chip *chip, int bus, int addr) {
    char path[32];
    chip->fd = -1;
    chip->bus = bus;
    chip->addr = addr;
    chip->protocol_major = -1;

    snprintf(path, sizeof path, "/dev/i2c-%d", bus);
    chip->fd = open(path, O_RDWR);
    if (chip->fd < 0) {
        return -1;
    }
    if (ioctl(chip->fd, I2C_SLAVE, (unsigned long)addr) != 0) {
        close(chip->fd);
        chip->fd = -1;
        return -1;
    }
    unsigned char version = 0;
    if (reg_read(chip, REG_DEVICE_VERSION, &version, 1) != 0) {
        close(chip->fd);
        chip->fd = -1;
        return -1;
    }
    return 0;
}

void mfi_close(struct mfi_chip *chip) {
    if (chip->fd >= 0) {
        close(chip->fd);
    }
    chip->fd = -1;
}

int mfi_protocol_major(struct mfi_chip *chip) {
    if (chip->protocol_major >= 0) {
        return chip->protocol_major;
    }
    unsigned char major = 0;
    if (chip->fd < 0 || reg_read(chip, REG_PROTOCOL_MAJOR, &major, 1) != 0) {
        return -1;
    }
    chip->protocol_major = major;
    return major;
}

int mfi_read_certificate(struct mfi_chip *chip, unsigned char *out, size_t cap) {
    if (chip->fd < 0) {
        return -1;
    }
    size_t len = 0;
    int len_ok = 0;
    for (int tries = 0; tries < CERT_TRIES && !len_ok; tries++) {
        if (reg_read_len(chip, REG_CERT_LENGTH, &len) == 0 && len > 0 && len <= cap) {
            len_ok = 1;
            break;
        }
        sleep_ms(CERT_RETRY_MS);
    }
    if (!len_ok) {
        return -1;
    }
    /* The data read is the flaky half: the chip NAKs until it is ready to hand the buffer over. */
    for (int tries = 0; tries < CERT_TRIES; tries++) {
        if (reg_read(chip, REG_CERT_DATA, out, len) == 0) {
            return (int)len;
        }
        sleep_ms(CERT_RETRY_MS);
    }
    return -1;
}

int mfi_last_error(const struct mfi_chip *chip) {
    unsigned char err = 0;
    if (chip->fd < 0) {
        return -1;
    }
    struct mfi_chip *mutable = (struct mfi_chip *)chip;
    if (reg_read(mutable, REG_ERROR_CODE, &err, 1) != 0) {
        return -1;
    }
    return err;
}

int mfi_sign(struct mfi_chip *chip, const unsigned char *challenge, size_t len, unsigned char *out,
             size_t cap) {
    if (chip->fd < 0 || len == 0 || len > MFI_CHALLENGE_MAX) {
        return -1;
    }
    unsigned char len_be[2] = { (unsigned char)(len >> 8), (unsigned char)len };
    if (reg_write(chip, REG_CHALLENGE_LENGTH, len_be, 2) != 0 ||
        reg_write(chip, REG_CHALLENGE_DATA, challenge, len) != 0 ||
        reg_write(chip, REG_AUTH_CONTROL_STATUS, (const unsigned char[]){ AUTH_START }, 1) != 0) {
        return -1;
    }

    int64_t deadline = now_ms() + AUTH_TIMEOUT_MS;
    unsigned char status = 0;
    for (;;) {
        if (reg_read(chip, REG_AUTH_CONTROL_STATUS, &status, 1) == 0 && status == AUTH_DONE) {
            break;
        }
        if (now_ms() >= deadline) {
            return -1;
        }
        sleep_ms(AUTH_POLL_MS);
    }

    size_t sig_len = 0;
    if (reg_read_len(chip, REG_SIGNATURE_LENGTH, &sig_len) != 0 || sig_len == 0 || sig_len > cap) {
        return -1;
    }
    if (reg_read(chip, REG_SIGNATURE_DATA, out, sig_len) != 0) {
        return -1;
    }
    return (int)sig_len;
}
