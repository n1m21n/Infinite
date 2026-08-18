#ifndef SHINE_H
#define SHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
  PCM_MONO = 1,
  PCM_STEREO = 2
} shine_channels_t;

typedef enum {
  SHINE_STEREO = 0,
  SHINE_JOINT_STEREO = 1,
  SHINE_DUAL_CHANNEL = 2,
  SHINE_MONO = 3
} shine_mode_t;

typedef enum {
  MPEG_I = 3,
  MPEG_II = 2,
  MPEG_25 = 0
} shine_version_t;

typedef struct {
  shine_version_t version;
  shine_mode_t mode;
  int bitr;
  int emp;
  int copyright;
  int original;
} shine_mpeg_t;

typedef struct {
  shine_channels_t channels;
  int samplerate;
} shine_wave_t;

typedef struct {
  shine_wave_t wave;
  shine_mpeg_t mpeg;
} shine_config_t;

typedef struct shine_global_flags *shine_t;

void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg);
int shine_check_config(int samplerate, int bitrate);
int shine_samples_per_pass(shine_t s);
shine_t shine_initialize(shine_config_t *config);
unsigned char *shine_encode_buffer_interleaved(shine_t s, int16_t *data, int *written);
unsigned char *shine_flush(shine_t s, int *written);
void shine_close(shine_t s);

#ifdef __cplusplus
}
#endif

#endif
