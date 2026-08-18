#ifndef SHINE_TYPES_H
#define SHINE_TYPES_H

#include "shine.h"
#include <stdint.h>
#include <stdio.h>

#define PI              3.14159265358979323846
#define HAN_SIZE        512
#define SCALE_BLOCK     12
#define CBANDS          64
#define CBLIMIT         32

typedef int32_t int32;
typedef uint32_t uint32;
typedef int16_t int16;
typedef uint16_t uint16;

typedef int32_t fixed;

typedef struct {
  uint32_t data;
  int bit_count;
} bit_stream;

typedef struct {
  int tabperchannel;
  int tabpergranule;
} shine_side_info_t;

typedef struct {
  unsigned part2_3_length;
  unsigned big_values;
  unsigned count1;
  unsigned global_gain;
  unsigned scalefac_compress;
  unsigned table_select[3];
  unsigned region0_count;
  unsigned region1_count;
  unsigned preflag;
  unsigned scalefac_scale;
  unsigned count1table_select;
  unsigned part2_length;
  unsigned sfb_lmax;
  unsigned address1;
  unsigned address2;
  unsigned address3;
  int quantizerStepSize;
  unsigned slen[4];
} shine_gr_info_t;

typedef struct {
  unsigned main_data_begin;
  unsigned private_bits;
  struct {
    unsigned scfsi[4];
    shine_gr_info_t gr[2];
  } ch[2];
} shine_side_info;

typedef struct {
  int l[1 + 22];
  int s[1 + 13];
} shine_scalefac_t;

struct shine_global_flags {
  shine_config_t config;
  shine_side_info side_info;
  shine_scalefac_t scalefac_band;
  bit_stream bs;

  int subband_buf[2][HAN_SIZE];
  int subband_off[2];
  fixed mdct_buf[2][18][32];

  int l3_enc[2][2][576];
  int l3_sbpair[2][2][18][32];
  int scalefac[2][2][39];

  int ResvSize;
  int ResvMax;

  int side_info_size;
  int mean_bits;
  int frame_size;

  unsigned char *output_buffer;
  int output_buffer_size;
};

#endif
