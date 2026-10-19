/*
 * Guarded-buffer tests for the fdk-aac 0.1.6 decoder patches 010 and 011.
 *
 * Streams are encoded with the encoder of the same (patched) library, then decoded the way
 * the CarPlay SDK converter stub does it (aacDecoder_Open(TT_MP4_RAW, 1) + ConfigRaw with the
 * AudioSpecificConfig, then per packet Fill + DecodeFrame).  Every output buffer ends exactly
 * at a PROT_NONE page, so any write past the physical allocation kills the case with SIGSEGV.
 * Everything between the claimed capacity and the guard page, and the buffer itself, is
 * filled with a canary before each call, so the bytes actually written are measured too.
 *
 * Each case runs in a forked child; the parent reports PASS/FAIL and exits non-zero on failure.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fdk-aac/aacdecoder_lib.h>
#include <fdk-aac/aacenc_lib.h>

#define CANARY 0xA5
#define SDK_ELD_FRAMES 480 /* kAudioSamplesPerPacket_AAC_ELD */
#define MAXPKT 64
#define LOGNEEDLE "is the input byte count"

/* ---------------------------------------------------------------- reporting */

static int g_fail;

static void fail(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  printf("    FAIL: ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  g_fail = 1;
}

static void note(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  printf("    ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
}

#define CHECK(cond, ...) do { if (!(cond)) fail(__VA_ARGS__); } while (0)

/* ---------------------------------------------------------------- guarded buffers */

typedef struct {
  unsigned char *map;
  size_t maplen;
  unsigned char *buf;  /* buf + phys == first byte of the PROT_NONE page */
  size_t phys;         /* bytes physically usable before the guard page */
} gbuf_t;

static gbuf_t galloc(size_t phys)
{
  gbuf_t g;
  size_t page = (size_t)sysconf(_SC_PAGESIZE);
  size_t data = (phys + page - 1) / page * page;
  g.maplen = data + page;
  g.map = mmap(NULL, g.maplen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (g.map == MAP_FAILED) { perror("mmap"); exit(2); }
  if (mprotect(g.map + data, page, PROT_NONE) != 0) { perror("mprotect"); exit(2); }
  g.buf = g.map + data - phys;
  g.phys = phys;
  return g;
}

static void gfree(gbuf_t *g) { munmap(g->map, g->maplen); }

static void gcanary(gbuf_t *g) { memset(g->buf, CANARY, g->phys); }

/* Number of bytes from the buffer start up to and including the last non-canary byte. */
static size_t gfootprint(const gbuf_t *g)
{
  size_t n = g->phys;
  while (n > 0 && g->buf[n - 1] == CANARY) n--;
  return n;
}

/* ---------------------------------------------------------------- stderr capture */

static FILE *g_errlog;

static void capture_stderr(void)
{
  g_errlog = tmpfile();
  if (!g_errlog) { perror("tmpfile"); exit(2); }
  fflush(stderr);
  dup2(fileno(g_errlog), 2);
}

static int count_log_lines(void)
{
  char line[512];
  int n = 0;
  fflush(stderr);
  rewind(g_errlog);
  while (fgets(line, sizeof(line), g_errlog))
    if (strstr(line, LOGNEEDLE)) n++;
  return n;
}

/* ---------------------------------------------------------------- encoder */

typedef struct {
  int n;
  int len[MAXPKT];
  unsigned char data[MAXPKT][2048];
  UCHAR asc[64];
  UINT ascLen;
  int frameLength;
  int channels;
} stream_t;

static int encode(stream_t *st, int aot, int rate, int channels, int granule, int sbr, int sbrRatio,
                  int bitrate)
{
  HANDLE_AACENCODER enc;
  AACENC_InfoStruct info;
  int calls;

  memset(st, 0, sizeof(*st));
  st->channels = channels;
  if (aacEncOpen(&enc, 0, channels) != AACENC_OK) return -1;
  if (aacEncoder_SetParam(enc, AACENC_AOT, aot) != AACENC_OK
      || aacEncoder_SetParam(enc, AACENC_SAMPLERATE, rate) != AACENC_OK
      || aacEncoder_SetParam(enc, AACENC_CHANNELMODE, channels == 1 ? MODE_1 : MODE_2) != AACENC_OK
      || aacEncoder_SetParam(enc, AACENC_CHANNELORDER, 1) != AACENC_OK
      || aacEncoder_SetParam(enc, AACENC_BITRATE, bitrate) != AACENC_OK
      || aacEncoder_SetParam(enc, AACENC_TRANSMUX, TT_MP4_RAW) != AACENC_OK
      || aacEncoder_SetParam(enc, AACENC_AFTERBURNER, 1) != AACENC_OK)
    goto bad;
  if (granule && aacEncoder_SetParam(enc, AACENC_GRANULE_LENGTH, granule) != AACENC_OK) goto bad;
  if (aot == AOT_ER_AAC_ELD && aacEncoder_SetParam(enc, AACENC_SBR_MODE, sbr) != AACENC_OK) goto bad;
  if (sbr && sbrRatio && aacEncoder_SetParam(enc, AACENC_SBR_RATIO, sbrRatio) != AACENC_OK) goto bad;
  if (aacEncEncode(enc, NULL, NULL, NULL, NULL) != AACENC_OK) goto bad;
  if (aacEncInfo(enc, &info) != AACENC_OK) goto bad;
  memcpy(st->asc, info.confBuf, info.confSize);
  st->ascLen = info.confSize;
  st->frameLength = (int)info.frameLength;

  for (calls = 0; st->n < MAXPKT && calls < MAXPKT + 32; calls++) {
    static INT_PCM in[2048 * 2];
    int i, c;
    void *inPtr = in, *outPtr;
    INT inId = IN_AUDIO_DATA, inSize, inEl = sizeof(INT_PCM);
    INT outId = OUT_BITSTREAM_DATA, outSize = sizeof(st->data[0]), outEl = 1;
    AACENC_BufDesc inDesc = { 1, &inPtr, &inId, &inSize, &inEl };
    AACENC_BufDesc outDesc = { 1, &outPtr, &outId, &outSize, &outEl };
    AACENC_InArgs inArgs = { 0, 0 };
    AACENC_OutArgs outArgs;

    for (i = 0; i < st->frameLength; i++)
      for (c = 0; c < channels; c++) {
        double t = (double)(calls * st->frameLength + i) / rate;
        in[i * channels + c] = (INT_PCM)(8000.0 * sin(2 * M_PI * (440.0 + 220.0 * c) * t));
      }
    inSize = st->frameLength * channels * (INT)sizeof(INT_PCM);
    inArgs.numInSamples = st->frameLength * channels;
    outPtr = st->data[st->n];
    memset(&outArgs, 0, sizeof(outArgs));
    if (aacEncEncode(enc, &inDesc, &outDesc, &inArgs, &outArgs) != AACENC_OK) goto bad;
    if (outArgs.numOutBytes > 0) st->len[st->n++] = outArgs.numOutBytes;
  }
  aacEncClose(&enc);
  return st->n > 8 ? 0 : -1;
bad:
  aacEncClose(&enc);
  return -1;
}

/* ---------------------------------------------------------------- decoder */

static HANDLE_AACDECODER dec_open(stream_t *st)
{
  HANDLE_AACDECODER d = aacDecoder_Open(TT_MP4_RAW, 1);
  UCHAR *conf[1] = { st->asc };
  UINT confLen[1] = { st->ascLen };
  if (!d) { fail("aacDecoder_Open"); return NULL; }
  if (aacDecoder_ConfigRaw(d, conf, confLen) != AAC_DEC_OK) { fail("aacDecoder_ConfigRaw"); return NULL; }
  return d;
}

/* AACDecBuf() of the SDK stub: Fill the whole packet, then DecodeFrame once. */
static AAC_DECODER_ERROR dec_packet(HANDLE_AACDECODER d, const unsigned char *pkt, int len,
                                    gbuf_t *out, INT timeDataSize)
{
  UCHAR *in[1] = { (UCHAR *)pkt };
  UINT inLen[1] = { (UINT)len };
  UINT valid[1] = { (UINT)len };
  aacDecoder_Fill(d, in, inLen, valid);
  return aacDecoder_DecodeFrame(d, (INT_PCM *)out->buf, timeDataSize, 0);
}

static double energy(const gbuf_t *g, size_t bytes)
{
  const INT_PCM *s = (const INT_PCM *)g->buf;
  double e = 0;
  size_t i;
  for (i = 0; i < bytes / sizeof(INT_PCM); i++) e += (double)s[i] * s[i];
  return e;
}

/* ---------------------------------------------------------------- encodings */

enum { ENC_ELD480_MONO, ENC_ELD480_STEREO, ENC_ELD512_MONO, ENC_ELD480_SBR_DUAL, ENC_LC_STEREO };

static int make_stream(stream_t *st, int which)
{
  switch (which) {
  case ENC_ELD480_MONO:     return encode(st, AOT_ER_AAC_ELD, 16000, 1, 480, 0, 0, 24000);
  case ENC_ELD480_STEREO:   return encode(st, AOT_ER_AAC_ELD, 16000, 2, 480, 0, 0, 48000);
  case ENC_ELD512_MONO:     return encode(st, AOT_ER_AAC_ELD, 16000, 1, 512, 0, 0, 24000);
  case ENC_ELD480_SBR_DUAL: return encode(st, AOT_ER_AAC_ELD, 32000, 1, 480, 1, 2, 32000);
  case ENC_LC_STEREO:       return encode(st, AOT_AAC_LC, 44100, 2, 1024, 0, 0, 128000);
  }
  return -1;
}

/* ---------------------------------------------------------------- cases */

/* (a) ELD 480 mono, timeDataSize = fill length, buffer exactly 480 frames: decodes, bounded. */
static void case_a(void)
{
  stream_t st;
  HANDLE_AACDECODER d;
  gbuf_t g = galloc(SDK_ELD_FRAMES * 1 * sizeof(INT_PCM));
  int i, ok = 0, belowFrame = 0;
  double e = 0;

  if (make_stream(&st, ENC_ELD480_MONO)) { fail("ELD 480 mono encoder setup"); return; }
  capture_stderr();
  if (!(d = dec_open(&st))) return;
  for (i = 0; i < st.n; i++) {
    AAC_DECODER_ERROR err;
    gcanary(&g);
    err = dec_packet(d, st.data[i], st.len[i], &g, st.len[i]);
    if (st.len[i] < SDK_ELD_FRAMES) belowFrame++;
    CHECK(err == AAC_DEC_OK, "packet %d (%d bytes): err 0x%x", i, st.len[i], err);
    if (err == AAC_DEC_OK) { ok++; e += energy(&g, g.phys); }
    CHECK(gfootprint(&g) <= g.phys, "footprint %zu > %zu", gfootprint(&g), g.phys);
  }
  {
    const CStreamInfo *si = aacDecoder_GetStreamInfo(d);
    note("aot %d, rate %d, channels %d, channelConfig %d, frameSize %d, packets %d (%d..%d bytes)",
         si->aot, si->sampleRate, si->numChannels, si->channelConfig, si->frameSize, st.n,
         st.len[0], st.len[st.n - 1]);
    CHECK(si->aot == AOT_ER_AAC_ELD && si->frameSize == 480 && si->numChannels == 1,
          "unexpected stream info");
  }
  CHECK(belowFrame == st.n, "%d of %d packets are < 480 bytes (0.1.6 check would reject them)",
        belowFrame, st.n);
  CHECK(ok == st.n, "%d of %d frames decoded", ok, st.n);
  CHECK(e > 0, "decoded output is silent");
  CHECK(count_log_lines() == 1, "compat log printed %d times, want once per handle", count_log_lines());
  note("%d/%d frames OK into a 480-frame buffer with timeDataSize = fill length; log once", ok, st.n);
  aacDecoder_Close(d);
  gfree(&g);
}

/* (a2) ELD 480 stereo, coincident length, buffer exactly 480 frames x 2: decodes, bounded. */
static void case_a_stereo(void)
{
  stream_t st;
  HANDLE_AACDECODER d;
  gbuf_t g = galloc(SDK_ELD_FRAMES * 2 * sizeof(INT_PCM));
  int i, ok = 0;

  if (make_stream(&st, ENC_ELD480_STEREO)) { fail("ELD 480 stereo encoder setup"); return; }
  capture_stderr();
  if (!(d = dec_open(&st))) return;
  for (i = 0; i < st.n; i++) {
    AAC_DECODER_ERROR err;
    gcanary(&g);
    err = dec_packet(d, st.data[i], st.len[i], &g, st.len[i]);
    CHECK(err == AAC_DEC_OK, "packet %d (%d bytes): err 0x%x", i, st.len[i], err);
    if (err == AAC_DEC_OK) ok++;
  }
  note("channels %d, frameSize %d: %d/%d frames OK into a 480x2 buffer",
       aacDecoder_GetStreamInfo(d)->numChannels, aacDecoder_GetStreamInfo(d)->frameSize, ok, st.n);
  CHECK(count_log_lines() == 1, "compat log printed %d times", count_log_lines());
  aacDecoder_Close(d);
  gfree(&g);
}

/* (b) ELD 480, timeDataSize = fill length = claimed capacity < 480 frames.  The decoder only
   sees timeDataSize, so the bypass relies on the SDK guarantee (>= 480 frames physically).
   Physical allocation = exactly the SDK minimum, guard right after: the write must stay at
   numChannels*frameSize and never reach the guard. */
static void case_b(void)
{
  stream_t st;
  HANDLE_AACDECODER d;
  gbuf_t g = galloc(SDK_ELD_FRAMES * sizeof(INT_PCM));
  int i, ok = 0;
  size_t maxPast = 0;

  if (make_stream(&st, ENC_ELD480_MONO)) { fail("ELD encoder setup"); return; }
  capture_stderr();
  if (!(d = dec_open(&st))) return;
  for (i = 0; i < st.n; i++) {
    AAC_DECODER_ERROR err;
    size_t claimed = (size_t)st.len[i] * sizeof(INT_PCM), fp;
    gcanary(&g);
    err = dec_packet(d, st.data[i], st.len[i], &g, st.len[i]);
    fp = gfootprint(&g);
    CHECK(err == AAC_DEC_OK, "packet %d (%d bytes): err 0x%x", i, st.len[i], err);
    if (err == AAC_DEC_OK) ok++;
    CHECK(fp <= 480 * sizeof(INT_PCM), "wrote %zu bytes, more than one 480-sample frame", fp);
    if (fp > claimed && fp - claimed > maxPast) maxPast = fp - claimed;
  }
  note("%d/%d frames OK; writes reach at most %zu bytes past the claimed capacity, always inside",
       ok, st.n, maxPast);
  note("the 480-frame SDK buffer (documented contract; the decoder cannot see the allocation)");
  aacDecoder_Close(d);
  gfree(&g);
}

/* (b2) ELD 480, genuinely undersized buffer whose timeDataSize is NOT the fill length: the
   0.1.6 check still rejects it; guard placed right after the claimed capacity. */
static void case_b_noncoincident(void)
{
  stream_t st;
  HANDLE_AACDECODER d;
  int i, small = 0;

  if (make_stream(&st, ENC_ELD480_MONO)) { fail("ELD encoder setup"); return; }
  if (!(d = dec_open(&st))) return;
  for (i = 0; i < st.n; i++) {
    int cap = st.len[i] + 1; /* < 480 samples, never equal to the fill length */
    gbuf_t g = galloc((size_t)cap * sizeof(INT_PCM));
    AAC_DECODER_ERROR err;
    gcanary(&g);
    err = dec_packet(d, st.data[i], st.len[i], &g, cap);
    CHECK(err == AAC_DEC_OUTPUT_BUFFER_TOO_SMALL, "packet %d: cap %d err 0x%x", i, cap, err);
    if (err == AAC_DEC_OUTPUT_BUFFER_TOO_SMALL) small++;
    gfree(&g);
  }
  note("%d/%d frames rejected with OUTPUT_BUFFER_TOO_SMALL, guard intact", small, st.n);
  aacDecoder_Close(d);
}

/* Shared body of the "must NOT bypass" cases: buffer of physSamples (guard right after),
   timeDataSize = fill length; every frame must be rejected with OUTPUT_BUFFER_TOO_SMALL, the
   write must stay within min(timeDataSize bytes, buffer) and the compat log must not appear. */
static void expect_rejected(int which, const char *label, int physSamplesFixed)
{
  stream_t st;
  HANDLE_AACDECODER d;
  int i, small = 0;
  const CStreamInfo *si;

  if (make_stream(&st, which)) { note("SKIP: encoder cannot produce %s", label); return; }
  capture_stderr();
  if (!(d = dec_open(&st))) return;
  for (i = 0; i < st.n; i++) {
    int phys = physSamplesFixed ? physSamplesFixed : st.len[i];
    gbuf_t g = galloc((size_t)phys * sizeof(INT_PCM));
    AAC_DECODER_ERROR err;
    size_t fp;
    gcanary(&g);
    err = dec_packet(d, st.data[i], st.len[i], &g, st.len[i]);
    fp = gfootprint(&g);
    CHECK(err == AAC_DEC_OUTPUT_BUFFER_TOO_SMALL, "packet %d (%d bytes): err 0x%x", i, st.len[i], err);
    if (err == AAC_DEC_OUTPUT_BUFFER_TOO_SMALL) small++;
    CHECK(fp <= (size_t)st.len[i], "packet %d: wrote %zu bytes, more than timeDataSize %d bytes",
          i, fp, st.len[i]);
    gfree(&g);
  }
  si = aacDecoder_GetStreamInfo(d);
  note("%s: aot %d, channels %d, frameSize %d, extAot %d; %d/%d frames rejected "
       "(OUTPUT_BUFFER_TOO_SMALL), guard intact", label, si->aot, si->numChannels, si->frameSize,
       si->extAot, small, st.n);
  CHECK(count_log_lines() == 0, "compat log printed for %s", label);
  aacDecoder_Close(d);
}

/* (c) AAC-LC stereo, timeDataSize = fill length = undersized capacity (guard right after). */
static void case_c(void) { expect_rejected(ENC_LC_STEREO, "AAC-LC 44.1k stereo, buffer = fill length", 0); }

/* (e) ELD 512: coincident length, buffer = fill length and buffer = SDK minimum 480 frames. */
static void case_e(void)
{
  expect_rejected(ENC_ELD512_MONO, "ELD 512 mono, buffer = fill length", 0);
  expect_rejected(ENC_ELD512_MONO, "ELD 512 mono, buffer = 480 frames (SDK minimum)", SDK_ELD_FRAMES);
}

/* (e2) ELD 480 with dual-rate SBR: output frame 960 > 480, must not bypass. */
static void case_e_sbr(void)
{
  expect_rejected(ENC_ELD480_SBR_DUAL, "ELD 480 + dual-rate SBR, buffer = 480 frames", SDK_ELD_FRAMES);
}

/* (d) AAC-LC stereo, correct buffer (1024 x 2, guard right after): decodes OK. */
static void case_d(void)
{
  stream_t st;
  HANDLE_AACDECODER d;
  gbuf_t g = galloc(1024 * 2 * sizeof(INT_PCM));
  int i, ok = 0;
  double e = 0;

  if (make_stream(&st, ENC_LC_STEREO)) { fail("LC encoder setup"); return; }
  capture_stderr();
  if (!(d = dec_open(&st))) return;
  for (i = 0; i < st.n; i++) {
    AAC_DECODER_ERROR err;
    gcanary(&g);
    err = dec_packet(d, st.data[i], st.len[i], &g, 1024 * 2);
    CHECK(err == AAC_DEC_OK, "packet %d: err 0x%x", i, err);
    if (err == AAC_DEC_OK) { ok++; e += energy(&g, g.phys); }
  }
  note("%d/%d frames OK (frameSize %d, channels %d)", ok, st.n,
       aacDecoder_GetStreamInfo(d)->frameSize, aacDecoder_GetStreamInfo(d)->numChannels);
  CHECK(e > 0, "decoded output is silent");
  CHECK(count_log_lines() == 0, "compat log printed for AAC-LC");
  aacDecoder_Close(d);
  gfree(&g);
}

/* Error paths: patch 010's clear must stay inside the buffer.
   (1) AAC-LC stereo, AppleCarPlay's byte-counting caller: buffer 4096 bytes, timeDataSize 4096
       (bytes), guard right after.  Upstream 0.1.6 clears 4096*2 bytes here.
   (2) ELD 480 mono, coincident lengths, 480-frame buffer.
   Inputs: a good frame, then a truncated packet, then an empty fill (NOT_ENOUGH_BITS). */
static void error_path(int which, const char *label, int physSamples, int tdsBytesCaller)
{
  stream_t st;
  HANDLE_AACDECODER d;
  gbuf_t g = galloc((size_t)physSamples * sizeof(INT_PCM));
  AAC_DECODER_ERROR err;
  int tds;
  size_t fp;

  if (make_stream(&st, which)) { fail("%s encoder setup", label); return; }
  capture_stderr();
  if (!(d = dec_open(&st))) return;
  gcanary(&g);
  tds = tdsBytesCaller ? (int)g.phys : st.len[0];
  err = dec_packet(d, st.data[0], st.len[0], &g, tds);
  CHECK(err == AAC_DEC_OK, "%s: first frame err 0x%x", label, err);

  gcanary(&g);
  tds = tdsBytesCaller ? (int)g.phys : st.len[1] / 3;
  err = dec_packet(d, st.data[1], st.len[1] / 3, &g, tds);
  fp = gfootprint(&g);
  note("%s: truncated packet (%d of %d bytes) -> 0x%x, wrote %zu of %zu bytes", label,
       st.len[1] / 3, st.len[1], err, fp, g.phys);
  /* AAC-LC detects the truncation; ER AAC-ELD may conceal it and return a frame, which then
     takes the bounded (one 480-sample frame) success path instead. */
  if (tdsBytesCaller) CHECK(err != AAC_DEC_OK, "%s: truncated packet decoded OK", label);
  CHECK(fp <= g.phys, "%s: overrun", label);

  gcanary(&g);
  tds = tdsBytesCaller ? (int)g.phys : 0;
  err = dec_packet(d, st.data[2], 0, &g, tds);
  fp = gfootprint(&g);
  note("%s: empty fill -> 0x%x, wrote %zu of %zu bytes", label, err, fp, g.phys);
  CHECK(!IS_OUTPUT_VALID(err), "%s: empty fill returned output", label);
  CHECK(fp <= g.phys && fp <= (size_t)(tds > 0 ? tds : 0), "%s: clear exceeded min(buffer, timeDataSize bytes)", label);

  aacDecoder_Close(d);
  gfree(&g);
}

static void case_err(void)
{
  error_path(ENC_LC_STEREO, "AAC-LC, timeDataSize in bytes", 1024 * 2, 1);
  error_path(ENC_ELD480_MONO, "ELD 480, timeDataSize = fill length", SDK_ELD_FRAMES, 0);
}

/* ---------------------------------------------------------------- driver */

static const struct { const char *name; void (*fn)(void); } cases[] = {
  { "a   ELD 480 mono, coincident length, 480-frame buffer", case_a },
  { "a2  ELD 480 stereo, coincident length, 480x2 buffer", case_a_stereo },
  { "b   ELD 480, coincident length = undersized claim, SDK-minimum allocation", case_b },
  { "b2  ELD 480, undersized buffer, non-coincident length", case_b_noncoincident },
  { "c   AAC-LC stereo, coincident length, undersized buffer", case_c },
  { "d   AAC-LC stereo, correct 1024x2 buffer", case_d },
  { "e   ELD 512, coincident length", case_e },
  { "e2  ELD 480 + dual-rate SBR (frame 960), coincident length", case_e_sbr },
  { "err error paths stay bounded (patch 010)", case_err },
};

int main(void)
{
  unsigned i;
  int failed = 0;

  setvbuf(stdout, NULL, _IONBF, 0);
  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    pid_t pid;
    int status;
    printf("[%s]\n", cases[i].name);
    pid = fork();
    if (pid == 0) {
      cases[i].fn();
      _exit(g_fail ? 1 : 0);
    }
    if (pid < 0 || waitpid(pid, &status, 0) < 0) { perror("fork/wait"); return 2; }
    if (WIFSIGNALED(status)) {
      printf("  FAIL (signal %d%s)\n", WTERMSIG(status),
             WTERMSIG(status) == SIGSEGV ? ": wrote into the guard page" : "");
      failed++;
    } else if (WEXITSTATUS(status) != 0) {
      printf("  FAIL\n");
      failed++;
    } else {
      printf("  PASS\n");
    }
  }
  printf("%s: %d of %u cases failed\n", failed ? "FAILED" : "OK", failed,
         (unsigned)(sizeof(cases) / sizeof(cases[0])));
  return failed ? 1 : 0;
}
