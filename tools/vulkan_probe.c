// Headless diagnostic transport, not the LÖVE product runtime or a speed claim.
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>
#define CHECK(call)                                      \
  do                                                     \
  {                                                      \
    VkResult status = (call);                            \
    if (status != VK_SUCCESS)                            \
    {                                                    \
      fprintf(stderr, "%s failed: %d\n", #call, status); \
      exit(2);                                           \
    }                                                    \
  } while (0)
static void fail(const char* message)
{
  fprintf(stderr, "%s\n", message);
  exit(2);
}
static void* read_file(const char* path, size_t* size)
{
  FILE* file = fopen(path, "rb");
  if (!file)
    fail("Cannot open input");
  if (fseek(file, 0, SEEK_END))
    fail("Cannot seek input");
  long length = ftell(file);
  if (length <= 0 || length > 128 * 1024 * 1024)
    fail("Invalid input size");
  rewind(file);
  void* data = malloc((size_t)length);
  if (!data)
    fail("Allocation failed");
  if (fread(data, 1, (size_t)length, file) != (size_t)length)
    fail("Cannot read input");
  fclose(file);
  *size = (size_t)length;
  return data;
}
static double seconds(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
typedef struct
{
  VkBuffer buffer;
  VkDeviceMemory memory;
  void* mapped;
  VkDeviceSize size;
} Buffer;
static Buffer buffer_create(VkPhysicalDevice gpu, VkDevice device, size_t size, const void* data)
{
  Buffer out = {.size = size};
  VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             .size = size,
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
  CHECK(vkCreateBuffer(device, &info, NULL, &out.buffer));
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device, out.buffer, &requirements);
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(gpu, &props);
  uint32_t type = UINT32_MAX;
  VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VkMemoryPropertyFlags preferred =
      data ? (required | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) : (required | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  for (uint32_t i = 0; i < props.memoryTypeCount; i++)
    if ((requirements.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & preferred) == preferred)
    {
      type = i;
      break;
    }
  if (type == UINT32_MAX)
    for (uint32_t i = 0; i < props.memoryTypeCount; i++)
      if ((requirements.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & required) == required)
      {
        type = i;
        break;
      }
  if (type == UINT32_MAX)
    fail("No coherent host-visible memory for diagnostic probe");
  VkMemoryAllocateInfo allocation = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size, .memoryTypeIndex = type};
  CHECK(vkAllocateMemory(device, &allocation, NULL, &out.memory));
  CHECK(vkBindBufferMemory(device, out.buffer, out.memory, 0));
  CHECK(vkMapMemory(device, out.memory, 0, size, 0, &out.mapped));
  if (data)
    memcpy(out.mapped, data, size);
  else
    memset(out.mapped, 0xCD, size);
  return out;
}
typedef struct
{
  const char* name;
  uint32_t word;
  char type; // 'f' = f32, 'u' = u32, 'r' = fraction (/65535.0)
  char part; // 'a' = all, 'l' = low 16, 'h' = high 16
} ReportField;

static const ReportField FAST_FIELDS[] = {
    {"total", 0, 'f', 'a'},
    {"events", 2, 'u', 'a'},
    {"taps", 3, 'u', 'l'},
    {"procs", 3, 'u', 'h'},
    {"isbProcs", 4, 'u', 'l'},
    {"isbConsumed", 4, 'u', 'h'},
    {"petBrandDamage", 5, 'f', 'a'},
    {"petDamage", 6, 'f', 'a'},
    {"petCasts", 7, 'u', 'l'},
    {"rngCalls", 7, 'u', 'h'},
    {"petMeleeDamage", 8, 'f', 'a'},
    {"petMeleeCasts", 9, 'u', 'l'},
    {"petMeleeHits", 9, 'u', 'h'},
    {"petMeleeCrits", 10, 'u', 'l'},
    {"petMeleeMisses", 10, 'u', 'h'},
    {"petSpellDamage", 11, 'f', 'a'},
    {"petSpellCasts", 12, 'u', 'l'},
    {"petSpellHits", 12, 'u', 'h'},
    {"petSpellCrits", 13, 'u', 'l'},
    {"petSpellMisses", 13, 'u', 'h'},
    {"mana", 14, 'f', 'a'},
    {"spent", 15, 'f', 'a'},
};

static const ReportField DETAILED_FIELDS[] = {
    {"total", 0, 'f', 'a'},          {"events", 2, 'u', 'a'},          {"taps", 3, 'u', 'l'},
    {"procs", 3, 'u', 'h'},          {"isbProcs", 4, 'u', 'l'},        {"isbConsumed", 4, 'u', 'h'},
    {"petBrandDamage", 5, 'f', 'a'}, {"petDamage", 6, 'f', 'a'},       {"petCasts", 7, 'u', 'l'},
    {"rngCalls", 7, 'u', 'h'},       {"petMeleeDamage", 26, 'f', 'a'}, {"petMeleeCasts", 27, 'u', 'l'},
    {"petMeleeHits", 27, 'u', 'h'},  {"petMeleeCrits", 28, 'u', 'l'},  {"petMeleeMisses", 28, 'u', 'h'},
    {"petSpellDamage", 29, 'f', 'a'},{"petSpellCasts", 30, 'u', 'l'},  {"petSpellHits", 30, 'u', 'h'},
    {"petSpellCrits", 31, 'u', 'l'}, {"petSpellMisses", 31, 'u', 'h'}, {"damage0", 8, 'f', 'a'},
    {"casts0", 9, 'u', 'l'},         {"hits0", 9, 'u', 'h'},           {"crits0", 10, 'u', 'l'},
    {"misses0", 10, 'u', 'h'},       {"damage1", 11, 'f', 'a'},        {"casts1", 12, 'u', 'l'},
    {"hits1", 12, 'u', 'h'},         {"crits1", 13, 'u', 'l'},         {"misses1", 13, 'u', 'h'},
    {"damage2", 14, 'f', 'a'},       {"casts2", 15, 'u', 'l'},         {"hits2", 15, 'u', 'h'},
    {"crits2", 16, 'u', 'l'},        {"misses2", 16, 'u', 'h'},        {"damage3", 17, 'f', 'a'},
    {"casts3", 18, 'u', 'l'},         {"hits3", 18, 'u', 'h'},          {"crits3", 19, 'u', 'l'},
    {"misses3", 19, 'u', 'h'},       {"damage4", 20, 'f', 'a'},        {"casts4", 21, 'u', 'l'},
    {"hits4", 21, 'u', 'h'},         {"crits4", 22, 'u', 'l'},         {"misses4", 22, 'u', 'h'},
    {"damage5", 23, 'f', 'a'},       {"casts5", 24, 'u', 'l'},         {"hits5", 24, 'u', 'h'},
    {"crits5", 25, 'u', 'l'},        {"misses5", 25, 'u', 'h'},        {"damage6", 32, 'f', 'a'},
    {"casts6", 33, 'u', 'l'},        {"hits6", 33, 'u', 'h'},          {"crits6", 34, 'u', 'l'},
    {"misses6", 34, 'u', 'h'},       {"damage7", 35, 'f', 'a'},        {"casts7", 36, 'u', 'l'},
    {"hits7", 36, 'u', 'h'},         {"crits7", 37, 'u', 'l'},         {"misses7", 37, 'u', 'h'},
    {"damage8", 38, 'f', 'a'},       {"casts8", 39, 'u', 'l'},         {"hits8", 39, 'u', 'h'},
    {"crits8", 40, 'u', 'l'},        {"misses8", 40, 'u', 'h'},        {"damage9", 41, 'f', 'a'},
    {"casts9", 42, 'u', 'l'},        {"hits9", 42, 'u', 'h'},          {"crits9", 43, 'u', 'l'},
    {"misses9", 43, 'u', 'h'},       {"damage10", 44, 'f', 'a'},       {"casts10", 45, 'u', 'l'},
    {"hits10", 45, 'u', 'h'},        {"crits10", 46, 'u', 'l'},        {"misses10", 46, 'u', 'h'},
    {"damage11", 47, 'f', 'a'},      {"casts11", 48, 'u', 'l'},        {"hits11", 48, 'u', 'h'},
    {"crits11", 49, 'u', 'l'},       {"misses11", 49, 'u', 'h'},       {"damage12", 50, 'f', 'a'},
    {"casts12", 51, 'u', 'l'},       {"hits12", 51, 'u', 'h'},         {"crits12", 52, 'u', 'l'},
    {"misses12", 52, 'u', 'h'},      {"damage13", 53, 'f', 'a'},       {"casts13", 54, 'u', 'l'},
    {"hits13", 54, 'u', 'h'},        {"crits13", 55, 'u', 'l'},        {"misses13", 55, 'u', 'h'},
    {"damage14", 56, 'f', 'a'},      {"casts14", 57, 'u', 'l'},        {"hits14", 57, 'u', 'h'},
    {"crits14", 58, 'u', 'l'},       {"misses14", 58, 'u', 'h'},       {"damage15", 59, 'f', 'a'},
    {"casts15", 60, 'u', 'l'},       {"hits15", 60, 'u', 'h'},         {"crits15", 61, 'u', 'l'},
    {"misses15", 61, 'u', 'h'},      {"uptime_corr", 62, 'r', 'l'},    {"uptime_agony", 62, 'r', 'h'},
    {"uptime_imm", 63, 'r', 'l'},    {"uptime_siphon", 63, 'r', 'h'},  {"uptime_doom", 64, 'r', 'l'},
    {"uptime_isb", 64, 'r', 'h'},    {"uptime_trance", 65, 'r', 'l'},  {"uptime_trinket", 65, 'r', 'h'},
    {"uptime_racial", 66, 'r', 'l'}, {"uptime_decimation", 66, 'r', 'h'},{"uptime_snfShadow", 67, 'r', 'l'},
    {"uptime_snfFire", 67, 'r', 'h'},{"spent", 68, 'f', 'a'},          {"manaWasted", 69, 'f', 'a'},
    {"impDamage", 70, 'f', 'a'},     {"impCasts", 71, 'u', 'l'},       {"impHits", 71, 'u', 'h'},
    {"impCrits", 72, 'u', 'l'},      {"impMisses", 72, 'u', 'h'},      {"petSwapUsed", 73, 'u', 'a'},
    {"ticks0", 74, 'u', 'a'},        {"tickCrits0", 75, 'u', 'a'},     {"tickDamage0", 76, 'f', 'a'},
    {"critDamage0", 77, 'f', 'a'},   {"tickCritDamage0", 78, 'f', 'a'},{"ticks1", 79, 'u', 'a'},
    {"tickCrits1", 80, 'u', 'a'},    {"tickDamage1", 81, 'f', 'a'},    {"critDamage1", 82, 'f', 'a'},
    {"tickCritDamage1", 83, 'f', 'a'},{"ticks2", 84, 'u', 'a'},        {"tickCrits2", 85, 'u', 'a'},
    {"tickDamage2", 86, 'f', 'a'},   {"critDamage2", 87, 'f', 'a'},    {"tickCritDamage2", 88, 'f', 'a'},
    {"ticks3", 89, 'u', 'a'},        {"tickCrits3", 90, 'u', 'a'},     {"tickDamage3", 91, 'f', 'a'},
    {"critDamage3", 92, 'f', 'a'},   {"tickCritDamage3", 93, 'f', 'a'},{"ticks4", 94, 'u', 'a'},
    {"tickCrits4", 95, 'u', 'a'},    {"tickDamage4", 96, 'f', 'a'},    {"critDamage4", 97, 'f', 'a'},
    {"tickCritDamage4", 98, 'f', 'a'},{"ticks5", 99, 'u', 'a'},        {"tickCrits5", 100, 'u', 'a'},
    {"tickDamage5", 101, 'f', 'a'},  {"critDamage5", 102, 'f', 'a'},   {"tickCritDamage5", 103, 'f', 'a'},
    {"ticks6", 104, 'u', 'a'},       {"tickCrits6", 105, 'u', 'a'},    {"tickDamage6", 106, 'f', 'a'},
    {"critDamage6", 107, 'f', 'a'},  {"tickCritDamage6", 108, 'f', 'a'},{"ticks7", 109, 'u', 'a'},
    {"tickCrits7", 110, 'u', 'a'},   {"tickDamage7", 111, 'f', 'a'},   {"critDamage7", 112, 'f', 'a'},
    {"tickCritDamage7", 113, 'f', 'a'},{"ticks8", 114, 'u', 'a'},      {"tickCrits8", 115, 'u', 'a'},
    {"tickDamage8", 116, 'f', 'a'},  {"critDamage8", 117, 'f', 'a'},   {"tickCritDamage8", 118, 'f', 'a'},
    {"ticks9", 119, 'u', 'a'},       {"tickCrits9", 120, 'u', 'a'},    {"tickDamage9", 121, 'f', 'a'},
    {"critDamage9", 122, 'f', 'a'},  {"tickCritDamage9", 123, 'f', 'a'},{"ticks10", 124, 'u', 'a'},
    {"tickCrits10", 125, 'u', 'a'},  {"tickDamage10", 126, 'f', 'a'},  {"critDamage10", 127, 'f', 'a'},
    {"tickCritDamage10", 128, 'f', 'a'},{"ticks11", 129, 'u', 'a'},    {"tickCrits11", 130, 'u', 'a'},
    {"tickDamage11", 131, 'f', 'a'}, {"critDamage11", 132, 'f', 'a'},  {"tickCritDamage11", 133, 'f', 'a'},
    {"ticks12", 134, 'u', 'a'},      {"tickCrits12", 135, 'u', 'a'},   {"tickDamage12", 136, 'f', 'a'},
    {"critDamage12", 137, 'f', 'a'}, {"tickCritDamage12", 138, 'f', 'a'},{"ticks13", 139, 'u', 'a'},
    {"tickCrits13", 140, 'u', 'a'},  {"tickDamage13", 141, 'f', 'a'},  {"critDamage13", 142, 'f', 'a'},
    {"tickCritDamage13", 143, 'f', 'a'},{"ticks14", 144, 'u', 'a'},    {"tickCrits14", 145, 'u', 'a'},
    {"tickDamage14", 146, 'f', 'a'}, {"critDamage14", 147, 'f', 'a'},  {"tickCritDamage14", 148, 'f', 'a'},
    {"ticks15", 149, 'u', 'a'},      {"tickCrits15", 150, 'u', 'a'},   {"tickDamage15", 151, 'f', 'a'},
    {"critDamage15", 152, 'f', 'a'}, {"tickCritDamage15", 153, 'f', 'a'},{"petBrandHits", 154, 'u', 'a'},
    {"petBrandCasts", 155, 'u', 'a'},{"petBrandCrits", 156, 'u', 'a'}, {"petBrandMisses", 157, 'u', 'a'},
    {"petMeleeGlances", 158, 'u', 'a'},{"amplifyCasts", 159, 'u', 'a'},{"racialCasts", 160, 'u', 'a'},
    {"mana", 161, 'f', 'a'},         {"gained", 162, 'f', 'a'},
};

typedef struct
{
  uint32_t count;
  double mean;
  double m2;
  uint32_t maxHeap;
  double durationSec;
  double totals[216];
  uint64_t totalsInt[216];
} CandidateStats;

static double safe_sqrt(double x)
{
  if (x <= 0.0)
    return 0.0;
  double g = x > 1.0 ? x : 1.0;
  for (int i = 0; i < 32; i++)
    g = 0.5 * (g + x / g);
  return g;
}

static void json_string(FILE* file, const char* text)
{
  fputc('"', file);
  for (const unsigned char* p = (const unsigned char*)text; *p; p++)
  {
    if (*p == '"' || *p == '\\')
    {
      fputc('\\', file);
      fputc(*p, file);
    }
    else if (*p < 32)
      fprintf(file, "\\u%04x", *p);
    else
      fputc(*p, file);
  }
  fputc('"', file);
}
static void dump_pipeline_stats(VkDevice device, VkPipeline pipeline, const char* path, VkBool32 enabled)
{
  if (!path)
    return;
  FILE* file = fopen(path, "w");
  if (!file)
    fail("Cannot write pipeline statistics");
  PFN_vkGetPipelineExecutablePropertiesKHR getProperties =
      (PFN_vkGetPipelineExecutablePropertiesKHR)vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR");
  PFN_vkGetPipelineExecutableStatisticsKHR getStatistics =
      (PFN_vkGetPipelineExecutableStatisticsKHR)vkGetDeviceProcAddr(device, "vkGetPipelineExecutableStatisticsKHR");
  VkPipelineInfoKHR info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR, .pipeline = pipeline};
  uint32_t count = 0;
  if (!enabled || !getProperties || !getStatistics || getProperties(device, &info, &count, NULL) != VK_SUCCESS ||
      !count || count > 64)
  {
    fputs("{\"available\":false,\"reason\":\"Driver does not expose usable pipeline executable statistics\"}\n", file);
    fclose(file);
    return;
  }
  VkPipelineExecutablePropertiesKHR* properties = calloc(count, sizeof(*properties));
  if (!properties)
    fail("Statistics allocation failed");
  for (uint32_t i = 0; i < count; i++)
    properties[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
  CHECK(getProperties(device, &info, &count, properties));
  fputs("{\"available\":true,\"executables\":[", file);
  for (uint32_t i = 0; i < count; i++)
  {
    if (i)
      fputc(',', file);
    fputs("{\"name\":", file);
    json_string(file, properties[i].name);
    fprintf(file, ",\"subgroupSize\":%u,\"statistics\":[", properties[i].subgroupSize);
    VkPipelineExecutableInfoKHR executable = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR, .pipeline = pipeline, .executableIndex = i};
    uint32_t number = 0;
    CHECK(getStatistics(device, &executable, &number, NULL));
    if (number > 1024)
      fail("Too many pipeline statistics");
    VkPipelineExecutableStatisticKHR* stats = number ? calloc(number, sizeof(*stats)) : NULL;
    if (number && !stats)
      fail("Statistics allocation failed");
    for (uint32_t j = 0; j < number; j++)
      stats[j].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
    if (number)
      CHECK(getStatistics(device, &executable, &number, stats));
    for (uint32_t j = 0; j < number; j++)
    {
      if (j)
        fputc(',', file);
      fputs("{\"name\":", file);
      json_string(file, stats[j].name);
      fputs(",\"description\":", file);
      json_string(file, stats[j].description);
      fputs(",\"value\":", file);
      switch (stats[j].format)
      {
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
          fputs(stats[j].value.b32 ? "true" : "false", file);
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
          fprintf(file, "%" PRId64, stats[j].value.i64);
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
          fprintf(file, "%" PRIu64, stats[j].value.u64);
          break;
        case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
          if (isfinite(stats[j].value.f64))
            fprintf(file, "%.17g", stats[j].value.f64);
          else
            fputs("null", file);
          break;
        default:
          fputs("null", file);
      }
      fputc('}', file);
    }
    free(stats);
    fputs("]}", file);
  }
  free(properties);
  fputs("]}\n", file);
  if (fclose(file))
    fail("Cannot close pipeline statistics");
}
int main(int argc, char** argv)
{
  if (argc != 7)
    fail("Usage: vulkan-probe shader.spv configs.bin params.bin output.bin resultWords batchSize");
  char* end = NULL;
  unsigned long parsed = strtoul(argv[5], &end, 10);
  if (!end || *end || parsed < 1 || parsed > 4096)
    fail("Invalid report words");
  uint32_t words = (uint32_t)parsed;
  parsed = strtoul(argv[6], &end, 10);
  if (!end || *end || parsed < 1 || parsed > 65536)
    fail("Invalid batch size");
  uint32_t batchSize = (uint32_t)parsed;
  uint32_t warmupBatches = 0;
  const char* warmupText = getenv("FASTSIM_WARMUP_BATCHES");
  if (warmupText)
  {
    parsed = strtoul(warmupText, &end, 10);
    if (!end || *end || parsed > 32)
      fail("Invalid warmup batch count");
    warmupBatches = (uint32_t)parsed;
  }
  uint32_t requiredSubgroup = 0;
  const char* subgroupText = getenv("FASTSIM_SUBGROUP_SIZE");
  if (subgroupText)
  {
    parsed = strtoul(subgroupText, &end, 10);
    if (!end || *end || !parsed || parsed > 64 || (parsed & (parsed - 1)))
      fail("Invalid subgroup size");
    requiredSubgroup = (uint32_t)parsed;
  }
  size_t shaderSize, configSize, paramSize;
  uint32_t* code = read_file(argv[1], &shaderSize);
  void* configs = read_file(argv[2], &configSize);
  uint32_t* params = read_file(argv[3], &paramSize);
  if (shaderSize % 4 || paramSize != 24 || params[0] == 0 || params[1] == 0 || params[4] == 0 || params[5] == 0 ||
      params[3] != 0)
    fail("Invalid dispatch parameters");
  uint32_t total = params[4];
  if ((uint64_t)params[0] * params[1] != total)
    fail("Fight/config count mismatch");
  uint32_t numConfigs = params[0];
  uint32_t fightsPerConfig = params[1];
  uint32_t* configWords = (uint32_t*)configs;
  VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .pApplicationName = "Fastsim headless diagnostic",
                           .apiVersion = VK_API_VERSION_1_1};
  VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
  VkInstance instance;
  CHECK(vkCreateInstance(&create, NULL, &instance));
  uint32_t numGpus = 0;
  CHECK(vkEnumeratePhysicalDevices(instance, &numGpus, NULL));
  if (!numGpus)
    fail("No Vulkan devices");
  VkPhysicalDevice* gpus = calloc(numGpus, sizeof(*gpus));
  CHECK(vkEnumeratePhysicalDevices(instance, &numGpus, gpus));
  VkPhysicalDevice gpu = gpus[0];
  const char* requireGpu = getenv("FASTSIM_REQUIRE_GPU");
  if (requireGpu && strcmp(requireGpu, "1") == 0)
  {
    int best = 0;
    for (uint32_t i = 0; i < numGpus; i++)
    {
      VkPhysicalDeviceProperties candidate;
      vkGetPhysicalDeviceProperties(gpus[i], &candidate);
      int score = candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                  : candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                  : candidate.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 1
                                                                                   : 0;
      if (score > best)
      {
        best = score;
        gpu = gpus[i];
      }
    }
    if (!best)
      fail("GPU benchmark requires a hardware Vulkan device; refusing CPU/software Vulkan");
  }
  free(gpus);
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(gpu, &properties);
  uint32_t numQueues;
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &numQueues, NULL);
  VkQueueFamilyProperties* queues = calloc(numQueues, sizeof(*queues));
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &numQueues, queues);
  uint32_t family = UINT32_MAX;
  for (uint32_t i = 0; i < numQueues; i++)
    if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      family = i;
      break;
    }
  if (family == UINT32_MAX)
    fail("No compute queue");
  uint32_t timestampBits = queues[family].timestampValidBits;
  free(queues);
  float priority = 1;
  VkDeviceQueueCreateInfo queueInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                       .queueFamilyIndex = family,
                                       .queueCount = 1,
                                       .pQueuePriorities = &priority};
  const char* statsPath = getenv("FASTSIM_PIPELINE_STATS_PATH");
  VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR statsFeatures = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR};
  VkPhysicalDeviceSubgroupSizeControlFeaturesEXT subgroupFeatures = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT};
  VkBool32 statsEnabled = VK_FALSE;
  const char* statsExtension = VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME;
  const char* subgroupExtension = VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME;
  VkBool32 haveStats = VK_FALSE, haveSubgroup = VK_FALSE;
  if (statsPath || requiredSubgroup)
  {
    uint32_t count = 0;
    CHECK(vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL));
    VkExtensionProperties* extensions = count ? calloc(count, sizeof(*extensions)) : NULL;
    if (count && !extensions)
      fail("Extension allocation failed");
    if (count)
      CHECK(vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, extensions));
    for (uint32_t i = 0; i < count; i++)
    {
      if (strcmp(extensions[i].extensionName, statsExtension) == 0)
        haveStats = VK_TRUE;
      if (strcmp(extensions[i].extensionName, subgroupExtension) == 0)
        haveSubgroup = VK_TRUE;
    }
    free(extensions);
  }
  if (statsPath && haveStats)
  {
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                          .pNext = &statsFeatures};
    vkGetPhysicalDeviceFeatures2(gpu, &features);
    statsEnabled = statsFeatures.pipelineExecutableInfo;
  }
  if (requiredSubgroup)
  {
    if (!haveSubgroup)
      fail("Required subgroup size control extension unavailable; refusing fallback");
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                          .pNext = &subgroupFeatures};
    vkGetPhysicalDeviceFeatures2(gpu, &features);
    VkPhysicalDeviceSubgroupSizeControlPropertiesEXT subgroupProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 properties2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                               .pNext = &subgroupProperties};
    vkGetPhysicalDeviceProperties2(gpu, &properties2);
    if (!subgroupFeatures.subgroupSizeControl ||
        !(subgroupProperties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) ||
        requiredSubgroup < subgroupProperties.minSubgroupSize ||
        requiredSubgroup > subgroupProperties.maxSubgroupSize ||
        64 / requiredSubgroup > subgroupProperties.maxComputeWorkgroupSubgroups)
      fail("Requested subgroup size unsupported for compute; refusing fallback");
    subgroupFeatures.computeFullSubgroups = VK_FALSE;
    fprintf(stderr,
            "Required compute subgroup size: %u (driver range %u-%u)\n",
            requiredSubgroup,
            subgroupProperties.minSubgroupSize,
            subgroupProperties.maxSubgroupSize);
  }
  const char* enabledExtensions[2];
  uint32_t enabledCount = 0;
  void* featureChain = NULL;
  if (statsEnabled)
  {
    enabledExtensions[enabledCount++] = statsExtension;
    statsFeatures.pNext = featureChain;
    featureChain = &statsFeatures;
  }
  if (requiredSubgroup)
  {
    enabledExtensions[enabledCount++] = subgroupExtension;
    subgroupFeatures.pNext = featureChain;
    featureChain = &subgroupFeatures;
  }
  VkDeviceCreateInfo deviceInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                   .queueCreateInfoCount = 1,
                                   .pQueueCreateInfos = &queueInfo,
                                   .pNext = featureChain,
                                   .enabledExtensionCount = enabledCount,
                                   .ppEnabledExtensionNames = enabledExtensions};
  VkDevice device;
  CHECK(vkCreateDevice(gpu, &deviceInfo, NULL, &device));
  VkQueue queue;
  vkGetDeviceQueue(device, family, 0, &queue);
  Buffer buffers[3] = {buffer_create(gpu, device, configSize, configs),
                       buffer_create(gpu, device, 24, params),
                       buffer_create(gpu, device, (size_t)batchSize * words * 4, NULL)};
  VkDescriptorSetLayoutBinding bindings[3];
  memset(bindings, 0, sizeof(bindings));
  for (int i = 0; i < 3; i++)
  {
    bindings[i].binding = (uint32_t)i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo setInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = bindings};
  VkDescriptorSetLayout setLayout;
  CHECK(vkCreateDescriptorSetLayout(device, &setInfo, NULL, &setLayout));
  VkPipelineLayoutCreateInfo layoutInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &setLayout};
  VkPipelineLayout pipelineLayout;
  CHECK(vkCreatePipelineLayout(device, &layoutInfo, NULL, &pipelineLayout));
  VkShaderModuleCreateInfo shaderInfo = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = shaderSize, .pCode = code};
  VkShaderModule module;
  CHECK(vkCreateShaderModule(device, &shaderInfo, NULL, &module));
  VkComputePipelineCreateInfo pipelineInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                              .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                        .module = module,
                                                        .pName = "main"},
                                              .layout = pipelineLayout};
  VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroupStage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
      .requiredSubgroupSize = requiredSubgroup};
  if (requiredSubgroup)
    pipelineInfo.stage.pNext = &subgroupStage;
  if (statsEnabled)
    pipelineInfo.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
  double compileStart = seconds();
  VkPipeline pipeline;
  CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline));
  double compileMs = (seconds() - compileStart) * 1000;
  dump_pipeline_stats(device, pipeline, statsPath, statsEnabled);
  fprintf(
      stderr, "Pipeline ready on %s (%.1f ms); beginning diagnostic dispatches\n", properties.deviceName, compileMs);
  VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
  VkDescriptorPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                         .maxSets = 1,
                                         .poolSizeCount = 1,
                                         .pPoolSizes = &poolSize};
  VkDescriptorPool pool;
  CHECK(vkCreateDescriptorPool(device, &poolInfo, NULL, &pool));
  VkDescriptorSetAllocateInfo setAllocation = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                               .descriptorPool = pool,
                                               .descriptorSetCount = 1,
                                               .pSetLayouts = &setLayout};
  VkDescriptorSet set;
  CHECK(vkAllocateDescriptorSets(device, &setAllocation, &set));
  VkDescriptorBufferInfo bufferInfo[3];
  VkWriteDescriptorSet writes[3];
  memset(writes, 0, sizeof(writes));
  for (int i = 0; i < 3; i++)
  {
    bufferInfo[i] = (VkDescriptorBufferInfo){buffers[i].buffer, 0, buffers[i].size};
    writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .dstSet = set,
                                       .dstBinding = (uint32_t)i,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                       .pBufferInfo = &bufferInfo[i]};
  }
  vkUpdateDescriptorSets(device, 3, writes, 0, NULL);
  VkCommandPoolCreateInfo commandPoolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                             .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                             .queueFamilyIndex = family};
  VkCommandPool commandPool;
  CHECK(vkCreateCommandPool(device, &commandPoolInfo, NULL, &commandPool));
  VkCommandBufferAllocateInfo commandInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                             .commandPool = commandPool,
                                             .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                             .commandBufferCount = 1};
  VkCommandBuffer command;
  CHECK(vkAllocateCommandBuffers(device, &commandInfo, &command));
  VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VkFence fence;
  CHECK(vkCreateFence(device, &fenceInfo, NULL, &fence));
  VkQueryPool queryPool = VK_NULL_HANDLE;
  if (timestampBits)
  {
    VkQueryPoolCreateInfo queryInfo = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2};
    CHECK(vkCreateQueryPool(device, &queryInfo, NULL, &queryPool));
  }
  FILE* output = NULL;
  if (argv[4][0] && strcmp(argv[4], "-") != 0)
  {
    output = fopen(argv[4], "wb");
    if (!output)
      fail("Cannot open output");
  }
  const ReportField* reportFields = NULL;
  size_t numReportFields = 0;
  if (words == 16 && configSize == (size_t)numConfigs * 229 * 4)
  {
    reportFields = FAST_FIELDS;
    numReportFields = sizeof(FAST_FIELDS) / sizeof(FAST_FIELDS[0]);
  }
  else if (words == 163 && configSize == (size_t)numConfigs * 229 * 4)
  {
    reportFields = DETAILED_FIELDS;
    numReportFields = sizeof(DETAILED_FIELDS) / sizeof(DETAILED_FIELDS[0]);
  }
  CandidateStats* candStats = NULL;
  if (reportFields)
  {
    candStats = calloc(numConfigs, sizeof(*candStats));
    if (!candStats)
      fail("Stats allocation failed");
    for (uint32_t cIdx = 0; cIdx < numConfigs; cIdx++)
    {
      uint32_t endMicros = configWords[cIdx * 229 + 16];
      candStats[cIdx].durationSec = (double)endMicros / 1000000.0;
      if (candStats[cIdx].durationSec <= 0.0)
        candStats[cIdx].durationSec = 180.0;
    }
  }
  double warmupStart = seconds(), warmupMs = 0, gpuMs = 0, decodeMs = 0;
  double executeStart = warmupStart;
  uint32_t draws = 0, remainingWarmups = warmupBatches;
  for (uint32_t first = 0; remainingWarmups || first < total;)
  {
    uint32_t n = total - first;
    if (n > batchSize)
      n = batchSize;
    uint32_t* p = buffers[1].mapped;
    memcpy(p, params, 24);
    p[3] = first;
    p[4] = n;
    CHECK(vkResetCommandBuffer(command, 0));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    if (queryPool)
    {
      vkCmdResetQueryPool(command, queryPool, 0, 2);
      vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);
    }
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(command, (n + 63) / 64, 1, 1);
    if (queryPool)
      vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);
    VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(
        command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    CHECK(vkResetFences(device, 1, &fence));
    CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 180000000000ull));
    if (remainingWarmups)
    {
      remainingWarmups--;
      if (!remainingWarmups)
      {
        warmupMs = (seconds() - warmupStart) * 1000;
        executeStart = seconds();
        fprintf(
            stderr, "Warmup complete: %u batches (%.1f ms), not counted in measured fights\n", warmupBatches, warmupMs);
      }
      continue;
    }
    if (queryPool)
    {
      uint64_t ticks[2];
      CHECK(vkGetQueryPoolResults(device,
                                  queryPool,
                                  0,
                                  2,
                                  sizeof(ticks),
                                  ticks,
                                  sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      uint64_t delta = ticks[1] - ticks[0];
      if (timestampBits < 64)
        delta &= (1ull << timestampBits) - 1;
      gpuMs += (double)delta * properties.limits.timestampPeriod / 1e6;
    }
    if (output)
    {
      size_t bytes = (size_t)n * words * 4;
      if (fwrite(buffers[2].mapped, 1, bytes, output) != bytes)
        fail("Output write failed");
    }
    if (candStats)
    {
      double decStart = seconds();
      const uint32_t* rawWords = (const uint32_t*)buffers[2].mapped;
      for (uint32_t lane = 0; lane < n; lane++)
      {
        uint32_t globalLane = first + lane;
        uint32_t cIdx = globalLane / fightsPerConfig;
        if (cIdx >= numConfigs)
          cIdx = numConfigs - 1;
        CandidateStats* cs = &candStats[cIdx];
        const uint32_t* w = rawWords + (size_t)lane * words;
        uint32_t statusWord = w[1];
        uint32_t done = statusWord & 0xFFFFu;
        uint32_t highWater = statusWord >> 16u;
        float totalDmg;
        memcpy(&totalDmg, &w[0], sizeof(float));
        if (done != 1u || highWater > 40u || !isfinite(totalDmg) || totalDmg < 0.0f)
          fail("Invalid/incomplete fight in report buffer");
        cs->count++;
        double dps = (double)totalDmg / cs->durationSec;
        double delta = dps - cs->mean;
        cs->mean += delta / (double)cs->count;
        cs->m2 += delta * (dps - cs->mean);
        if (highWater > cs->maxHeap)
          cs->maxHeap = highWater;
        if (words == 16)
        {
          // Fast-path unrolled accumulation for 16-word fast report
          float f5, f6, f8, f11, f14, f15;
          memcpy(&f5, &w[5], 4);
          memcpy(&f6, &w[6], 4);
          memcpy(&f8, &w[8], 4);
          memcpy(&f11, &w[11], 4);
          memcpy(&f14, &w[14], 4);
          memcpy(&f15, &w[15], 4);
          if (!isfinite(f5) || !isfinite(f6) || !isfinite(f8) || !isfinite(f11) || !isfinite(f14) || !isfinite(f15))
            fail("Nonfinite float in fast report");
          cs->totals[0] += (double)totalDmg;
          cs->totalsInt[1] += w[2];
          cs->totalsInt[2] += w[3] & 0xFFFFu;
          cs->totalsInt[3] += w[3] >> 16u;
          cs->totalsInt[4] += w[4] & 0xFFFFu;
          cs->totalsInt[5] += w[4] >> 16u;
          cs->totals[6] += (double)f5;
          cs->totals[7] += (double)f6;
          cs->totalsInt[8] += w[7] & 0xFFFFu;
          cs->totalsInt[9] += w[7] >> 16u;
          cs->totals[10] += (double)f8;
          cs->totalsInt[11] += w[9] & 0xFFFFu;
          cs->totalsInt[12] += w[9] >> 16u;
          cs->totalsInt[13] += w[10] & 0xFFFFu;
          cs->totalsInt[14] += w[10] >> 16u;
          cs->totals[15] += (double)f11;
          cs->totalsInt[16] += w[12] & 0xFFFFu;
          cs->totalsInt[17] += w[12] >> 16u;
          cs->totalsInt[18] += w[13] & 0xFFFFu;
          cs->totalsInt[19] += w[13] >> 16u;
          cs->totals[20] += (double)f14;
          cs->totals[21] += (double)f15;
        }
        else
        {
          for (size_t fi = 0; fi < numReportFields; fi++)
          {
            uint32_t raw = w[reportFields[fi].word];
            if (reportFields[fi].type == 'f')
            {
              float fv;
              memcpy(&fv, &raw, sizeof(float));
              if (!isfinite(fv))
                fail("Nonfinite float in detailed report");
              cs->totals[fi] += (double)fv;
            }
            else
            {
              uint32_t uv = reportFields[fi].part == 'l'   ? (raw & 0xFFFFu)
                            : reportFields[fi].part == 'h' ? (raw >> 16u)
                                                           : raw;
              if (reportFields[fi].type == 'r')
                cs->totals[fi] += (double)uv / 65535.0;
              else
                cs->totalsInt[fi] += uv;
            }
          }
        }
      }
      decodeMs += (seconds() - decStart) * 1000;
    }
    first += n;
    draws++;
    if (total >= 100000 && (first == total || first / 50000 != (first - n) / 50000))
      fprintf(stderr, "Read back %u/%u fights\n", first, total);
  }
  if (output && fclose(output))
    fail("Output close failed");
  double elapsedMs = (seconds() - executeStart) * 1000 - decodeMs;
  if (elapsedMs < 0)
    elapsedMs = 0;
  char gpuTime[64];
  if (queryPool)
    snprintf(gpuTime, sizeof(gpuTime), "%.6f", gpuMs);
  else
    strcpy(gpuTime, "null");
  printf("{\"device\":\"%s\",\"deviceType\":%u,\"compileMs\":%.3f,\"executeReadbackMs\":%.3f,\"cDecodeMs\":%.3f,\"draws\":%u,"
         "\"totalFights\":%u,\"warmupBatches\":%u,\"warmupMs\":%.3f,\"gpuComputeMs\":%s,\"requiredSubgroupSize\":%u",
         properties.deviceName,
         properties.deviceType,
         compileMs,
         elapsedMs,
         decodeMs,
         draws,
         total,
         warmupBatches,
         warmupMs,
         gpuTime,
         requiredSubgroup);
  if (candStats)
  {
    printf(",\"summaries\":[");
    for (uint32_t cIdx = 0; cIdx < numConfigs; cIdx++)
    {
      if (cIdx)
        putchar(',');
      CandidateStats* cs = &candStats[cIdx];
      double var = cs->count > 1 ? cs->m2 / (double)(cs->count - 1) : 0.0;
      double sd = safe_sqrt(var > 0.0 ? var : 0.0);
      double se = cs->count > 0 ? sd / safe_sqrt((double)cs->count) : 0.0;
      printf("{\"sampleCount\":%u,\"meanDps\":%.17g,\"sd\":%.17g,\"standardError\":%.17g,\"maxHeap\":%u,\"totals\":{",
             cs->count,
             cs->mean,
             sd,
             se,
             cs->maxHeap);
      for (size_t fi = 0; fi < numReportFields; fi++)
      {
        if (fi)
          putchar(',');
        if (reportFields[fi].type == 'u')
          printf("\"%s\":%" PRIu64, reportFields[fi].name, cs->totalsInt[fi]);
        else
          printf("\"%s\":%.17g", reportFields[fi].name, cs->totals[fi]);
      }
      printf("}}");
    }
    printf("]");
    free(candStats);
  }
  printf("}\n");
  if (queryPool)
    vkDestroyQueryPool(device, queryPool, NULL);
  vkDestroyFence(device, fence, NULL);
  vkDestroyCommandPool(device, commandPool, NULL);
  vkDestroyDescriptorPool(device, pool, NULL);
  vkDestroyPipeline(device, pipeline, NULL);
  vkDestroyShaderModule(device, module, NULL);
  vkDestroyPipelineLayout(device, pipelineLayout, NULL);
  vkDestroyDescriptorSetLayout(device, setLayout, NULL);
  for (int i = 0; i < 3; i++)
  {
    vkUnmapMemory(device, buffers[i].memory);
    vkDestroyBuffer(device, buffers[i].buffer, NULL);
    vkFreeMemory(device, buffers[i].memory, NULL);
  }
  vkDestroyDevice(device, NULL);
  vkDestroyInstance(instance, NULL);
  free(code);
  free(configs);
  free(params);
  return 0;
}
