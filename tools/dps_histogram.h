#ifndef FASTSIM_DPS_HISTOGRAM_H
#define FASTSIM_DPS_HISTOGRAM_H
#include <stdint.h>
#include <string.h>
#define FASTSIM_HISTOGRAM_BINS 256
#ifndef FASTSIM_HISTOGRAM_ENABLED
#define FASTSIM_HISTOGRAM_ENABLED 1
#endif
typedef struct {
  double upper;
  double scale;
  uint32_t bins[FASTSIM_HISTOGRAM_BINS];
} DpsHistogramCounters;
static inline void histogram_add(DpsHistogramCounters* histogram, double dps)
{
  if (dps >= histogram->upper)
  {
    do
    {
      // Doubling the range preserves exact counts by merging adjacent bins.
      for (unsigned i = 0; i < FASTSIM_HISTOGRAM_BINS / 2; ++i)
        histogram->bins[i] = histogram->bins[2 * i] + histogram->bins[2 * i + 1];
      memset(histogram->bins + FASTSIM_HISTOGRAM_BINS / 2, 0,
             sizeof(uint32_t) * FASTSIM_HISTOGRAM_BINS / 2);
      histogram->upper *= 2;
      histogram->scale *= 0.5;
    } while (dps >= histogram->upper);
  }
  histogram->bins[(unsigned)(dps * histogram->scale)]++;
}
#endif
