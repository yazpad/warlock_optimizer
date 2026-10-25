// Calls the production C aggregation loop, not a model of its work.
#define main fastsim_native_main
#include "../tools/vulkan_probe.c"
#undef main
int main(int argc, char** argv)
{
  const int enabled = argc > 1 ? atoi(argv[1]) : 0;
  const unsigned fights = 1000000, candidates = 5, repeats = 9;
  uint32_t* reports = calloc((size_t)fights * 16, sizeof(uint32_t));
  if (!reports) fail("Cannot allocate benchmark reports");
  uint32_t random = 101;
  for (unsigned i = 0; i < fights; ++i)
  {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    float damage = (float)(400 + (random % 120000) / 100.0 + (i / (fights / candidates)) * 100) * 180;
    memcpy(&reports[(size_t)i * 16], &damage, 4);
    reports[(size_t)i * 16 + 1] = 1 | (16 << 16);
  }
  printf("{\"collection\":%s,\"fights\":%u,\"milliseconds\":[", enabled ? "true" : "false", fights);
  for (unsigned trial = 0; trial < repeats + 2; ++trial)
  {
    CandidateStats stats[5] = {0};
    for (unsigned i = 0; i < candidates; ++i) { stats[i].durationSec = 180; stats[i].histogram.upper = 1024; stats[i].histogram.scale = .25; }
    double start = seconds();
    void (*volatile aggregate)(const uint32_t*, uint32_t, uint32_t, uint32_t,
        uint32_t, uint32_t, CandidateStats*, const ReportField*, size_t, int) = aggregate_reports;
    for (unsigned first = 0; first < fights; first += 65536)
    {
      unsigned count = fights - first; if (count > 65536) count = 65536;
      aggregate(reports + (size_t)first * 16, first, count, 16, fights / candidates,
          candidates, stats, FAST_FIELDS, sizeof(FAST_FIELDS) / sizeof(FAST_FIELDS[0]), enabled);
    }
    double ms = (seconds() - start) * 1000;
    for (unsigned i = 0; i < candidates; ++i)
    {
      uint32_t sum = 0; for (unsigned j = 0; j < FASTSIM_HISTOGRAM_BINS; ++j) sum += stats[i].histogram.bins[j];
      if (stats[i].count != fights / candidates || (enabled && sum != stats[i].count) || (!enabled && sum)) fail("Benchmark count mismatch");
    }
    if (trial >= 2) printf("%s%.9f", trial == 2 ? "" : ",", ms);
  }
  puts("]}"); free(reports); return 0;
}
