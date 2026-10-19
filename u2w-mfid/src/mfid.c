/* mfid — serves this dongle's MFi coprocessor over TCP, the way LIVI's head unit asks for it.
 *
 * LIVI authenticates a CarPlay accessory with an MFi chip; on a Mac the only chip it can reach is
 * one served over the network (LIVI's "MFi Host" setting). Its own LIVI Link firmware does that on
 * port 5000, and this is the same wire for a stock CPC200-U2W:
 *
 *   GET_CERT    [0x01]                      -> [status][len hi][len lo][certificate]
 *   SIGN        [0x02][len hi][len lo][chal] -> [status][len hi][len lo][signature]
 *   PROTO_MAJOR [0x03]                      -> [status][0][1][major]
 *
 * status 0 is ok, 1 is an error; several requests run over one connection. The chip answers on
 * i2c-1 at 0x10 (see mfichip.h).
 *
 *   mfid --iface ncm1 [--bind 192.168.7.1] [--port 5000] [--bus 1] [--addr 0x10]
 *
 * The chip is a signing oracle, so exposure is limited to one interface and fails closed:
 *   - the listener is bound to --bind *and* to --iface (SO_BINDTODEVICE). An address bind alone
 *     would still accept a packet for that address arriving on wlan0;
 *   - --bind must be assigned to --iface at start, or mfid exits (3) instead of falling back to
 *     another address; it exits (3) as well when the interface is re-created (new ifindex) or the
 *     address goes away, and the init re-resolves the interface and starts it again;
 *   - SO_REUSEADDR without SO_REUSEPORT: a restart binds again while closed connections sit in
 *     TIME_WAIT, but a second listener on the same address and port still fails EADDRINUSE.
 * Resources are bounded: 8 client slots (the 9th connection is closed at once), requests capped
 * at 3 + 128 bytes, a connection without a complete request for 30 s or without write progress
 * for 10 s is closed, a queued response is never overwritten (the client is not read until it is
 * sent), and log lines are rate-limited per kind. One chip operation runs at a time.
 *
 * Exit status: 0 stopped by a signal, 1 chip/socket failure, 2 usage, 3 interface or address
 * unavailable. It writes nothing to the device; root is only needed for /dev/i2c-*.
 */

#define _GNU_SOURCE

#include "mfichip.h"

#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* SIGPIPE is ignored instead */
#endif

#define OP_GET_CERT 0x01
#define OP_SIGN 0x02
#define OP_PROTOCOL_MAJOR 0x03

#define STATUS_OK 0
#define STATUS_ERR 1

#define MAX_CLIENTS 8
#define REQ_MAX (3 + MFI_CHALLENGE_MAX)
#define RESP_MAX (3 + MFI_CERT_MAX)

#define IDLE_MS 30000
#define WRITE_MS 10000
#define IFACE_CHECK_MS 1000

#define EXIT_FAIL 1
#define EXIT_USAGE 2
#define EXIT_IFACE 3

/* Rate limit: at most LOG_BURST lines of one kind per LOG_WINDOW_MS, then one summary line. */
#define LOG_BURST 5
#define LOG_WINDOW_MS 60000

enum log_kind { LOG_STATE, LOG_REFUSED, LOG_PROTOCOL, LOG_TIMEOUT, LOG_CHIP, LOG_IO, LOG_KINDS };

static const char *const log_kind_name[LOG_KINDS] = {
    "state", "refused", "protocol", "timeout", "chip", "io",
};

struct limiter {
    int64_t window_start;
    unsigned used;
    unsigned suppressed;
};

struct client {
    int fd;
    unsigned char in[REQ_MAX];
    size_t in_len;
    unsigned char out[RESP_MAX];
    size_t out_len;
    size_t out_sent;
    int64_t idle_since;  /* accept, or the last request served */
    int64_t write_since; /* last write progress while a response is queued */
    char peer[INET_ADDRSTRLEN + 8];
};

static volatile sig_atomic_t running = 1;
static struct limiter limits[LOG_KINDS];

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void emit(const char *fmt, va_list ap) {
    char line[256];
    time_t now = time(NULL);
    struct tm tm;
    size_t n = 0;
    if (localtime_r(&now, &tm)) {
        n = strftime(line, sizeof line, "[mfid %H:%M:%S] ", &tm);
    }
    int m = vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    if (m < 0) {
        return;
    }
    n += (size_t)m < sizeof line - n - 1 ? (size_t)m : sizeof line - n - 2;
    line[n++] = '\n';
    /* One write per line: the log is shared with the init (O_APPEND). */
    ssize_t ignored = write(STDERR_FILENO, line, n);
    (void)ignored;
}

static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(fmt, ap);
    va_end(ap);
}

static void flush_suppressed(int64_t now, int force) {
    for (int k = 0; k < LOG_KINDS; k++) {
        struct limiter *l = &limits[k];
        if (l->suppressed && (force || now - l->window_start >= LOG_WINDOW_MS)) {
            say("%u more %s message(s) suppressed", l->suppressed, log_kind_name[k]);
            l->suppressed = 0;
        }
    }
}

/* Errors and state changes only; a flood of one kind costs at most LOG_BURST lines a minute. */
static void logk(enum log_kind kind, const char *fmt, ...) {
    if (kind != LOG_STATE) {
        int64_t now = now_ms();
        struct limiter *l = &limits[kind];
        if (now - l->window_start >= LOG_WINDOW_MS) {
            flush_suppressed(now, 0);
            l->window_start = now;
            l->used = 0;
        }
        if (l->used >= LOG_BURST) {
            l->suppressed++;
            return;
        }
        l->used++;
    }
    va_list ap;
    va_start(ap, fmt);
    emit(fmt, ap);
    va_end(ap);
}

/* 1 when addr is assigned to iface, 0 when it is not, -1 when the list cannot be read. */
static int addr_on_iface(const char *iface, struct in_addr addr) {
    struct ifaddrs *list;
    if (getifaddrs(&list) != 0) {
        return -1;
    }
    int found = 0;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && a->ifa_name &&
            strcmp(a->ifa_name, iface) == 0 &&
            ((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr == addr.s_addr) {
            found = 1;
            break;
        }
    }
    freeifaddrs(list);
    return found;
}

static int bind_to_device(int fd, const char *iface, unsigned ifindex) {
#if defined(SO_BINDTODEVICE)
    (void)ifindex;
    return setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface, (socklen_t)strlen(iface) + 1);
#elif defined(IP_BOUND_IF)
    (void)iface; /* macOS host builds (tests) */
    return setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &ifindex, sizeof ifindex);
#else
    (void)fd;
    (void)iface;
    (void)ifindex;
    errno = ENOSYS;
    return -1;
#endif
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void drop(struct client *c) {
    close(c->fd);
    c->fd = -1;
    c->in_len = 0;
    c->out_len = 0;
    c->out_sent = 0;
}

static void respond(struct client *c, unsigned char status, const unsigned char *data, size_t len) {
    if (len > RESP_MAX - 3) {
        len = 0;
        status = STATUS_ERR;
    }
    c->out[0] = status;
    c->out[1] = (unsigned char)(len >> 8);
    c->out[2] = (unsigned char)len;
    if (len) {
        memcpy(c->out + 3, data, len);
    }
    c->out_len = 3 + len;
    c->out_sent = 0;
}

/* Frames the request at the head of the buffer: 1 complete (*need bytes), 0 incomplete, -1 a
 * protocol violation (unknown op, or a challenge length the chip cannot take). */
static int frame(const unsigned char *in, size_t have, size_t *need) {
    if (have == 0) {
        return 0;
    }
    switch (in[0]) {
        case OP_GET_CERT:
        case OP_PROTOCOL_MAJOR:
            *need = 1;
            return 1;
        case OP_SIGN: {
            if (have < 3) {
                return 0;
            }
            size_t len = ((size_t)in[1] << 8) | in[2];
            if (len < 1 || len > MFI_CHALLENGE_MAX) {
                return -1;
            }
            *need = 3 + len;
            return have >= *need;
        }
        default:
            return -1;
    }
}

/* Runs one complete request against the chip and queues the answer. */
static void serve(struct mfi_chip *chip, struct client *c, size_t need, int64_t now) {
    switch (c->in[0]) {
        case OP_GET_CERT: {
            unsigned char cert[MFI_CERT_MAX];
            int len = mfi_read_certificate(chip, cert, sizeof cert);
            if (len < 0) {
                logk(LOG_CHIP, "certificate: chip error %d", mfi_last_error(chip));
                respond(c, STATUS_ERR, NULL, 0);
            } else {
                respond(c, STATUS_OK, cert, (size_t)len);
            }
            break;
        }
        case OP_PROTOCOL_MAJOR: {
            int major = mfi_protocol_major(chip);
            unsigned char value = (unsigned char)major;
            if (major < 0) {
                logk(LOG_CHIP, "protocol major: chip error");
                respond(c, STATUS_ERR, NULL, 0);
            } else {
                respond(c, STATUS_OK, &value, 1);
            }
            break;
        }
        case OP_SIGN: {
            unsigned char sig[MFI_SIGNATURE_MAX];
            int len = mfi_sign(chip, c->in + 3, need - 3, sig, sizeof sig);
            if (len < 0) {
                logk(LOG_CHIP, "sign: %zu-byte challenge, chip error %d", need - 3,
                     mfi_last_error(chip));
                respond(c, STATUS_ERR, NULL, 0);
            } else {
                respond(c, STATUS_OK, sig, (size_t)len);
            }
            break;
        }
    }
    memmove(c->in, c->in + need, c->in_len - need);
    c->in_len -= need;
    c->idle_since = now;
    c->write_since = now;
}

/* Sends what the socket takes. -1 when the connection is gone. */
static int flush_out(struct client *c, int64_t now) {
    while (c->out_sent < c->out_len) {
        ssize_t n = send(c->fd, c->out + c->out_sent, c->out_len - c->out_sent, MSG_NOSIGNAL);
        if (n > 0) {
            c->out_sent += (size_t)n;
            c->write_since = now;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        } else {
            return -1;
        }
    }
    c->out_len = 0;
    c->out_sent = 0;
    c->idle_since = now;
    return 0;
}

/* Serves at most one buffered request (round robin between clients), only when nothing is
 * queued for this client. -1 closes the connection. */
static int step(struct mfi_chip *chip, struct client *c, int64_t now) {
    if (c->out_len) {
        return 0;
    }
    size_t need = 0;
    int r = frame(c->in, c->in_len, &need);
    if (r < 0) {
        logk(LOG_PROTOCOL, "%s: protocol violation (op 0x%02x, %zu bytes), closing", c->peer,
             c->in[0], c->in_len);
        return -1;
    }
    if (r == 0) {
        return 0;
    }
    serve(chip, c, need, now);
    return flush_out(c, now);
}

static int has_work(const struct client *c) {
    size_t need;
    return c->fd >= 0 && !c->out_len && frame(c->in, c->in_len, &need) != 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --iface IFACE [--bind ADDR] [--port N] [--bus N] [--addr 0xNN]\n"
            "          [--idle-ms N] [--write-ms N]\n",
            argv0);
}

static int parse_int(const char *s, long lo, long hi, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 0);
    if (errno || end == s || *end || v < lo || v > hi) {
        return -1;
    }
    *out = v;
    return 0;
}

int main(int argc, char **argv) {
    long port = 5000, bus = MFI_DEFAULT_BUS, addr = MFI_DEFAULT_ADDR;
    long idle_ms = IDLE_MS, write_ms = WRITE_MS;
    const char *bind_addr = "192.168.7.1";
    const char *iface = NULL;

    for (int i = 1; i < argc; i += 2) {
        const char *opt = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;
        int bad = 0;
        if (!val) {
            bad = 1;
        } else if (strcmp(opt, "--iface") == 0) {
            iface = val;
        } else if (strcmp(opt, "--bind") == 0) {
            bind_addr = val;
        } else if (strcmp(opt, "--port") == 0) {
            bad = parse_int(val, 1, 65535, &port);
        } else if (strcmp(opt, "--bus") == 0) {
            bad = parse_int(val, 0, 255, &bus);
        } else if (strcmp(opt, "--addr") == 0) {
            bad = parse_int(val, 0x03, 0x77, &addr);
        } else if (strcmp(opt, "--idle-ms") == 0) {
            bad = parse_int(val, 100, 3600000, &idle_ms);
        } else if (strcmp(opt, "--write-ms") == 0) {
            bad = parse_int(val, 100, 3600000, &write_ms);
        } else {
            bad = 1;
        }
        if (bad) {
            usage(argv[0]);
            return EXIT_USAGE;
        }
    }
    struct in_addr listen_addr;
    if (!iface || !*iface || strlen(iface) >= IF_NAMESIZE || inet_pton(AF_INET, bind_addr, &listen_addr) != 1 ||
        listen_addr.s_addr == htonl(INADDR_ANY)) {
        /* No wildcard listener, ever: the address and the interface are both required. */
        usage(argv[0]);
        return EXIT_USAGE;
    }

    struct sigaction sa_stop;
    memset(&sa_stop, 0, sizeof sa_stop);
    sa_stop.sa_handler = on_signal; /* no SA_RESTART: poll() returns EINTR */
    sigaction(SIGINT, &sa_stop, NULL);
    sigaction(SIGTERM, &sa_stop, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* Fail closed: the interface must exist and carry the address before anything listens. */
    unsigned ifindex = if_nametoindex(iface);
    if (ifindex == 0) {
        say("interface %s does not exist; not listening", iface);
        return EXIT_IFACE;
    }
    if (addr_on_iface(iface, listen_addr) != 1) {
        say("%s is not an address of %s; not listening", bind_addr, iface);
        return EXIT_IFACE;
    }

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        say("socket: %s", strerror(errno));
        return EXIT_FAIL;
    }
    /* SO_REUSEADDR (never SO_REUSEPORT): mfid closes idle and misbehaving clients itself, which
     * leaves TIME_WAIT entries that would block an immediate restart.  It does not allow a
     * second listener: that bind still fails with EADDRINUSE. */
    int one = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0) {
        say("SO_REUSEADDR: %s", strerror(errno));
        close(listener);
        return EXIT_FAIL;
    }
    if (bind_to_device(listener, iface, ifindex) != 0) {
        say("bind to device %s: %s; not listening", iface, strerror(errno));
        close(listener);
        return EXIT_IFACE;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr = listen_addr;
    if (bind(listener, (struct sockaddr *)&sa, sizeof sa) != 0) {
        int err = errno;
        say("bind %s:%ld: %s", bind_addr, port, strerror(err));
        close(listener);
        return err == EADDRNOTAVAIL ? EXIT_IFACE : EXIT_FAIL;
    }
    if (listen(listener, MAX_CLIENTS) != 0 || set_nonblocking(listener) != 0) {
        say("listen %s:%ld: %s", bind_addr, port, strerror(errno));
        close(listener);
        return EXIT_FAIL;
    }

    struct mfi_chip chip;
    if (mfi_open(&chip, (int)bus, (int)addr) != 0) {
        say("no coprocessor on i2c-%ld at 0x%02lx: %s", bus, addr, strerror(errno));
        close(listener);
        return EXIT_FAIL;
    }
    int major = mfi_protocol_major(&chip);
    say("coprocessor on i2c-%ld at 0x%02lx, protocol major %d (%s)", bus, addr, major,
        major == 2 ? "2.0C, RSA/SHA-1" : major == 3 ? "3.0, ECDSA/SHA-256" : "unknown");
    say("listening on %s:%ld, %s only (ifindex %u)", bind_addr, port, iface, ifindex);

    struct client clients[MAX_CLIENTS];
    memset(clients, 0, sizeof clients);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
    }
    int status = 0;
    int64_t next_check = now_ms() + IFACE_CHECK_MS;

    while (running) {
        int64_t now = now_ms();
        struct pollfd fds[1 + MAX_CLIENTS];
        int64_t wake = next_check;
        int work = 0;
        fds[0].fd = listener;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            struct client *c = &clients[i];
            fds[1 + i].fd = c->fd;
            fds[1 + i].events = 0;
            fds[1 + i].revents = 0;
            if (c->fd < 0) {
                continue;
            }
            if (c->out_len) {
                /* Backpressure: not read again until the queued answer is out. */
                fds[1 + i].events = POLLOUT;
                if (c->write_since + write_ms < wake) {
                    wake = c->write_since + write_ms;
                }
            } else {
                if (c->in_len < sizeof c->in) {
                    fds[1 + i].events = POLLIN;
                }
                if (c->idle_since + idle_ms < wake) {
                    wake = c->idle_since + idle_ms;
                }
            }
            work |= has_work(c);
        }
        int timeout = work ? 0 : wake > now ? (int)(wake - now) : 0;
        if (poll(fds, 1 + MAX_CLIENTS, timeout) < 0) {
            if (errno == EINTR) {
                continue;
            }
            say("poll: %s", strerror(errno));
            status = EXIT_FAIL;
            break;
        }
        now = now_ms();

        if (fds[0].revents & POLLIN) {
            struct sockaddr_in peer;
            socklen_t peer_len = sizeof peer;
            int fd = accept(listener, (struct sockaddr *)&peer, &peer_len);
            if (fd >= 0) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd < 0) {
                        slot = i;
                        break;
                    }
                }
                char name[INET_ADDRSTRLEN] = "?";
                inet_ntop(AF_INET, &peer.sin_addr, name, sizeof name);
                if (slot < 0) {
                    logk(LOG_REFUSED, "%s:%u: all %d slots busy, refused", name,
                         ntohs(peer.sin_port), MAX_CLIENTS);
                    close(fd);
                } else if (set_nonblocking(fd) != 0) {
                    close(fd);
                } else {
                    int one = 1;
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
                    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                    struct client *c = &clients[slot];
                    c->fd = fd;
                    c->in_len = c->out_len = c->out_sent = 0;
                    c->idle_since = c->write_since = now;
                    snprintf(c->peer, sizeof c->peer, "%s:%u", name, ntohs(peer.sin_port));
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            struct client *c = &clients[i];
            short revents = fds[1 + i].revents;
            if (c->fd < 0 || fds[1 + i].fd != c->fd) {
                continue; /* empty, or accepted in this round */
            }
            if (revents & (POLLERR | POLLNVAL)) {
                drop(c);
                continue;
            }
            if ((revents & POLLOUT) && flush_out(c, now) != 0) {
                drop(c);
                continue;
            }
            if (revents & (POLLIN | POLLHUP)) {
                if (c->out_len || c->in_len >= sizeof c->in) {
                    if (revents & POLLHUP) {
                        drop(c);
                        continue;
                    }
                } else {
                    ssize_t got = recv(c->fd, c->in + c->in_len, sizeof c->in - c->in_len, 0);
                    if (got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                                     errno != EINTR)) {
                        if (c->in_len) {
                            logk(LOG_PROTOCOL, "%s: closed with a truncated request", c->peer);
                        }
                        drop(c);
                        continue;
                    }
                    if (got > 0) {
                        c->in_len += (size_t)got;
                    }
                }
            }
            if (step(&chip, c, now) != 0) {
                drop(c);
                continue;
            }
            int64_t after = now_ms(); /* a chip operation can take seconds */
            if (c->out_len && after - c->write_since >= write_ms) {
                logk(LOG_TIMEOUT, "%s: no write progress for %ld ms, closing", c->peer, write_ms);
                drop(c);
            } else if (!c->out_len && !has_work(c) && after - c->idle_since >= idle_ms) {
                logk(LOG_TIMEOUT, "%s: no complete request for %ld ms, closing", c->peer, idle_ms);
                drop(c);
            }
        }

        if (now >= next_check) {
            next_check = now + IFACE_CHECK_MS;
            flush_suppressed(now, 0);
            /* A re-created interface has a new ifindex; the listener is still bound to the old
             * one, so exit and let the init bind afresh. */
            if (if_nametoindex(iface) != ifindex || addr_on_iface(iface, listen_addr) == 0) {
                say("%s (ifindex %u) or its address %s went away; exiting", iface, ifindex,
                    bind_addr);
                status = EXIT_IFACE;
                break;
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            close(clients[i].fd);
        }
    }
    close(listener);
    mfi_close(&chip);
    flush_suppressed(now_ms(), 1);
    say("stopped");
    return status;
}
