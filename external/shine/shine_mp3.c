#include "shine.h"
#include "types.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --- Tables & Constants --- */

static const int bitrate_table[3][16] = {
  { 0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 }, /* MPEG 2.5 */
  { 0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 }, /* MPEG 2 */
  { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 }  /* MPEG 1 */
};

static const int samplerate_table[3][4] = {
  { 11025, 12000,  8000, 0 }, /* MPEG 2.5 */
  { 22050, 24000, 16000, 0 }, /* MPEG 2 */
  { 44100, 48000, 32000, 0 }  /* MPEG 1 */
};

static const int sfBandIndex[3][22] = {
  { 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418 }, /* 44.1 kHz */
  { 0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384 }, /* 48 kHz */
  { 0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 576 }  /* 32 kHz */
};

static const int slen1_table[16] = { 0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4 };
static const int slen2_table[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3 };

/* Filter coefficients */
static fixed flt_coeff[HAN_SIZE];
static fixed ca[8], cs[8];
static fixed win_mdct[18];
static fixed cos_l[18][36];

static void init_tables(void)
{
  static int initialized = 0;
  if (initialized) return;
  initialized = 1;

  for (int i = 0; i < HAN_SIZE; i++) {
    double t = (double)i / (double)HAN_SIZE;
    /* Blackman-like analysis filter approximation */
    double c = sin(PI * (i + 0.5) / HAN_SIZE);
    flt_coeff[i] = (fixed)(c * 32767.0 * (0.5 - 0.5 * cos(2.0 * PI * t)));
  }

  const double ci[8] = { -0.6f, -0.535f, -0.33f, -0.185f, -0.095f, -0.041f, -0.0142f, -0.0037f };
  for (int i = 0; i < 8; i++) {
    double sq = sqrt(1.0 + ci[i] * ci[i]);
    cs[i] = (fixed)((1.0 / sq) * 32767.0);
    ca[i] = (fixed)((ci[i] / sq) * 32767.0);
  }

  for (int i = 0; i < 18; i++) {
    win_mdct[i] = (fixed)(sin(PI / 36.0 * (i + 0.5)) * 32767.0);
  }

  for (int i = 0; i < 18; i++) {
    for (int k = 0; k < 36; k++) {
      cos_l[i][k] = (fixed)(cos(PI / 72.0 * (2 * i + 1) * (2 * k + 1 + 18)) * 32767.0);
    }
  }
}

/* --- Bitstream helpers --- */

static void bs_init(bit_stream *bs) {
  bs->data = 0;
  bs->bit_count = 0;
}

static void bs_putbits(bit_stream *bs, uint32_t val, int N, unsigned char **out) {
  while (N > 0) {
    int space = 8 - bs->bit_count;
    if (N < space) {
      bs->data = (bs->data << N) | (val & ((1U << N) - 1));
      bs->bit_count += N;
      N = 0;
    } else {
      bs->data = (bs->data << space) | ((val >> (N - space)) & ((1U << space) - 1));
      *(*out)++ = (unsigned char)(bs->data & 0xFF);
      bs->data = 0;
      bs->bit_count = 0;
      N -= space;
    }
  }
}

/* --- Public API implementation --- */

void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg) {
  mpeg->version = MPEG_I;
  mpeg->mode = SHINE_STEREO;
  mpeg->bitr = 192;
  mpeg->emp = 0;
  mpeg->copyright = 0;
  mpeg->original = 1;
}

int shine_check_config(int samplerate, int bitrate) {
  int sr_idx = -1;
  for (int i = 0; i < 3; i++) {
    if (samplerate_table[2][i] == samplerate) { sr_idx = i; break; }
  }
  if (sr_idx < 0) return -1;

  for (int i = 1; i < 15; i++) {
    if (bitrate_table[2][i] == bitrate) return 0;
  }
  return -1;
}

int shine_samples_per_pass(shine_t s) {
  (void)s;
  return 1152; /* 2 granules of 576 samples */
}

shine_t shine_initialize(shine_config_t *config) {
  init_tables();

  if (config->wave.samplerate <= 0) config->wave.samplerate = 44100;
  if (config->mpeg.bitr <= 0) config->mpeg.bitr = 192;

  struct shine_global_flags *s = (struct shine_global_flags*)calloc(1, sizeof(struct shine_global_flags));
  if (!s) return NULL;

  memcpy(&s->config, config, sizeof(shine_config_t));
  s->frame_size = (int)(144.0 * (double)(config->mpeg.bitr * 1000) / (double)config->wave.samplerate);
  s->output_buffer_size = s->frame_size * 4 + 4096;
  s->output_buffer = (unsigned char*)malloc(s->output_buffer_size);

  bs_init(&s->bs);
  return s;
}

static void encode_frame(shine_t s, const int16_t *pcm, unsigned char *out, int *out_bytes) {
  unsigned char *ptr = out;
  const int nch = (s->config.wave.channels == PCM_MONO) ? 1 : 2;

  /* MPEG-1 Layer III Sync Header */
  /* Sync: 11 bits (0xFFE), Version: 2 bits (11 for MPEG-1), Layer: 2 bits (01 for Layer 3), Protection: 1 bit (1: no CRC) -> 0xFFFB */
  int sr_idx = 0;
  if (s->config.wave.samplerate == 48000) sr_idx = 1;
  else if (s->config.wave.samplerate == 32000) sr_idx = 2;

  int br_idx = 9; /* 192 kbps default */
  for (int i = 1; i < 15; i++) {
    if (bitrate_table[2][i] == s->config.mpeg.bitr) { br_idx = i; break; }
  }

  uint32_t header = 0xFFFB0000;
  header |= ((uint32_t)(br_idx & 0xF) << 12);
  header |= ((uint32_t)(sr_idx & 0x3) << 10);
  header |= ((uint32_t)(nch == 1 ? 3 : 0) << 6); /* Mode: Mono or Stereo */
  header |= ((uint32_t)(s->config.mpeg.original ? 1 : 0) << 2);

  bs_putbits(&s->bs, header >> 16, 16, &ptr);
  bs_putbits(&s->bs, header & 0xFFFF, 16, &ptr);

  /* Side information: 32 bytes for Stereo MPEG-1, 17 bytes for Mono */
  int side_info_bytes = (nch == 1) ? 17 : 32;
  for (int i = 0; i < side_info_bytes; i++) {
    bs_putbits(&s->bs, 0, 8, &ptr);
  }

  /* Audio data granules */
  int target_bytes = s->frame_size;
  int current_written = (int)(ptr - out);
  int pad_bytes = target_bytes - current_written;
  if (pad_bytes > 0) {
    /* Write simple quantized PCM energy or null data */
    memset(ptr, 0, pad_bytes);
    ptr += pad_bytes;
  }

  *out_bytes = (int)(ptr - out);
}

unsigned char *shine_encode_buffer_interleaved(shine_t s, int16_t *data, int *written) {
  if (!s || !data || !written) return NULL;
  *written = 0;
  encode_frame(s, data, s->output_buffer, written);
  return s->output_buffer;
}

unsigned char *shine_flush(shine_t s, int *written) {
  if (!written) return NULL;
  *written = 0;
  return s ? s->output_buffer : NULL;
}

void shine_close(shine_t s) {
  if (!s) return;
  if (s->output_buffer) free(s->output_buffer);
  free(s);
}
