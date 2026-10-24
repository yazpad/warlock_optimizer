#!/usr/bin/env luajit
-- Run from the repository root; shell entry points handle cwd automatically.
local ffi=require('ffi')
ffi.cdef[[typedef struct { long tv_sec; long tv_nsec; } fastsim_timespec;
int clock_gettime(int, fastsim_timespec*); int setenv(const char*,const char*,int); int unsetenv(const char*);]]
local function now() local t=ffi.new('fastsim_timespec');assert(ffi.C.clock_gettime(1,t)==0);return tonumber(t.tv_sec)+tonumber(t.tv_nsec)*1e-9 end
local started=now()
local iohelp=require('tools.native_support')
local json=require('sim.common.json')
local layout=require('sim.classes.warlock.generated.layout')
local suite=require('data.validation.forever_baseline')
local specialize=require('sim.classes.warlock.specialize')
local opts={mode='fast',totalFights=1000000,batchSize=65536,warmupBatches=2,subgroupSize=32,specialized=true,requireGpu=true,output='artifacts/native-million.json'}
local valueFlags={['--mode']='mode',['--total-fights']='totalFights',['--iterations']='iterations',['--batch-size']='batchSize',['--warmup-batches']='warmupBatches',['--subgroup-size']='subgroupSize',['--output']='output',['--raw-output']='rawOutput',['--work-dir']='workDir'}
local numeric={totalFights=true,iterations=true,batchSize=true,warmupBatches=true,subgroupSize=true}
local i=1
while i<=#arg do
 local a=arg[i];local key=valueFlags[a]
 if key then i=i+1;opts[key]=assert(arg[i],'Missing '..a);if numeric[key] then opts[key]=assert(tonumber(opts[key]),'Invalid '..a) end
 elseif a=='--generic' then opts.specialized=false;opts.subgroupSize=nil
 elseif a=='--specialize-features' then opts.specialized=true
 elseif a=='--no-subgroup-size' then opts.subgroupSize=nil
 elseif a=='--allow-software' then opts.requireGpu=false
 elseif a=='--require-gpu' then opts.requireGpu=true
 elseif a=='--pipeline-stats' then opts.pipelineStats=true
 else error('Unknown argument: '..a) end
 i=i+1
end
local function integer(v,lo,hi,label) assert(type(v)=='number' and v==math.floor(v) and v>=lo and v<=hi,'Invalid '..label);return v end
assert(opts.mode=='fast' or opts.mode=='detailed','Invalid mode')
integer(opts.batchSize,1,65536,'batch size');integer(opts.warmupBatches,0,32,'warmup batches')
if opts.subgroupSize then assert(opts.specialized,'Subgroup override requires specialization');assert(({[4]=true,[8]=true,[16]=true,[32]=true,[64]=true})[opts.subgroupSize],'Invalid subgroup size') end
local count=#suite.scenarios
local iterations=opts.iterations or opts.totalFights/count
integer(iterations,2,1048576,'iterations');local total=count*iterations
integer(total,1,16777216,'total fights')
local kernelHashes={}
for p,expected in pairs(suite.kernelHashes) do kernelHashes[p]=iohelp.hashFile(p);assert(kernelHashes[p]==expected,'Stale suite kernel/layout: '..p) end
local suiteHash=iohelp.hashFile('data/validation/forever_baseline.json')
assert(suiteHash==suite.suiteHash,'Stale generated suite; regenerate JSON and Lua together')
local words=opts.mode=='fast' and layout.fastWords or layout.detailedWords
local maxDuration=0
for _,s in ipairs(suite.scenarios) do maxDuration=math.max(maxDuration,s.duration) end
local budget=maxDuration*32+1024
iohelp.run('mkdir -p artifacts/.cache')
local temp=opts.workDir
if not temp then temp=os.tmpname();os.remove(temp) end
-- mkdir must succeed exclusively before this job owns/cleans the directory.
iohelp.run('mkdir '..iohelp.quote(temp))
local function execute()
 local phase=now()
 local buildKey=iohelp.hash(iohelp.read('tools/vulkan_probe.c')..'cc -O2 -Wall -Wextra -Werror -lvulkan')
 local probe='artifacts/.cache/probe-'..buildKey
 if not iohelp.exists(probe) then
  iohelp.run('cc -O2 -Wall -Wextra -Werror tools/vulkan_probe.c -lvulkan -o '..iohelp.quote(temp..'/probe'))
  iohelp.run('mv '..iohelp.quote(temp..'/probe')..' '..iohelp.quote(probe))
 end
 local buildMs=(now()-phase)*1000
 local generic=iohelp.read('sim/classes/warlock/generated/'..opts.mode..'.glsl')
 local actual,features=generic,{}
 if opts.specialized then actual,features=specialize.specialize(generic,suite.scenarios) end
 local genericHash,actualHash=iohelp.hash(generic),iohelp.hash(actual)
 phase=now()
 local wrapped=iohelp.wrap(actual)
 local spv='artifacts/.cache/'..iohelp.hash(wrapped)..'.spv'
 if not iohelp.exists(spv) then
  iohelp.write(temp..'/shader.comp',wrapped)
  iohelp.run('glslangValidator -V '..iohelp.quote(temp..'/shader.comp')..' -o '..iohelp.quote(temp..'/shader.spv')..' >/dev/null')
  iohelp.run('spirv-val '..iohelp.quote(temp..'/shader.spv'))
  iohelp.run('mv '..iohelp.quote(temp..'/shader.spv')..' '..iohelp.quote(spv))
 end
 local shaderCompileMs=(now()-phase)*1000
 local configs=ffi.new('uint32_t[?]',count*layout.configWords)
 local idx=0
 for _,s in ipairs(suite.scenarios) do for _,w in ipairs(s.packed) do configs[idx]=w;idx=idx+1 end end
 local params=ffi.new('uint32_t[6]',{count,iterations,suite.seed,0,total,budget})
 iohelp.write(temp..'/configs.bin',ffi.string(configs,ffi.sizeof(configs)))
 iohelp.write(temp..'/params.bin',ffi.string(params,ffi.sizeof(params)))
 local function env(k,v) if v then ffi.C.setenv(k,tostring(v),1) else ffi.C.unsetenv(k) end end
 env('FASTSIM_WARMUP_BATCHES',opts.warmupBatches);env('FASTSIM_SUBGROUP_SIZE',opts.subgroupSize)
 env('FASTSIM_REQUIRE_GPU',opts.requireGpu and '1');env('FASTSIM_PIPELINE_STATS_PATH',opts.pipelineStats and temp..'/stats.json')
 phase=now()
 local command={probe,spv,temp..'/configs.bin',temp..'/params.bin',opts.rawOutput or '-',words,opts.batchSize}
 for n,v in ipairs(command) do command[n]=iohelp.quote(v) end
 iohelp.run(table.concat(command,' ')..' > '..iohelp.quote(temp..'/probe.json'))
 local probeMs=(now()-phase)*1000
 local probeJson=iohelp.read(temp..'/probe.json')
 local function metric(k) return tonumber(probeJson:match('"'..k..'":([%d%.]+)')) end
 local gpuMs,execMs=metric('gpuComputeMs'),metric('executeReadbackMs')
 local summaries=assert(probeJson:match('"summaries":(%[.*%])%s*}'),'Missing C summaries')
 local candidate=0
 summaries=summaries:gsub('{"sampleCount":',function()
  candidate=candidate+1
  return '{"candidateId":'..json.encode(assert(suite.scenarios[candidate]).id)..',"status":"complete","sampleCount":'
 end)
 assert(candidate==count,'C summary candidate count mismatch')
 assert(metric('totalFights')==total,'C fight count mismatch')
 local wallMs=(now()-started)*1000
 local variant=opts.specialized and 'shared-features' or 'generic'
 if opts.subgroupSize then variant=variant..'-wave'..opts.subgroupSize end
 local metadata={schemaVersion=1,classId='warlock',suiteId=suite.suiteId,suiteHash=suiteHash,kernelHashes=kernelHashes,
  inputSnapshotHash=iohelp.hash(json.encode({scenarios=suite.scenarios,iterations=iterations,seed=suite.seed})),
  mode=opts.mode,status='complete',iterations=iterations,seed=suite.seed,
  optimization={variant=variant,features=features,genericShaderHash=genericHash,actualShaderHash=actualHash},
  runtime={backend='Vulkan',transport='native LuaJIT + C Vulkan engine',engineBuildKey=buildKey,nativeRunnerHash=iohelp.hashFile('tools/run_native.lua')},
  timing={wallMs=wallMs,buildMs=buildMs,shaderCompileMs=shaderCompileMs,probeWallMs=probeMs,
   gpuComputeMs=gpuMs,executeReadbackMs=execMs,decodeSummaryMs=metric('cDecodeMs'),compileMs=metric('compileMs'),
   warmupMs=metric('warmupMs'),draws=metric('draws'),totalFights=total,warmupBatches=opts.warmupBatches,batchSize=opts.batchSize,
   gpuSimsPerSecond=gpuMs and gpuMs>0 and total*1000/gpuMs or nil,
   dispatchReadbackSimsPerSecond=execMs and execMs>0 and total*1000/execMs or nil,endToEndSimsPerSecond=total*1000/wallMs},
  validation='diagnostic-only; accepted Forever parity pending'}
 local encoded=json.encode(metadata)
 -- Embed C JSON directly: no lossy parse/re-encode of counters or device strings.
 local stats=opts.pipelineStats and iohelp.read(temp..'/stats.json') or 'null'
 iohelp.write(opts.output,encoded:sub(1,-2)..',"probe":'..probeJson..',"pipelineStatistics":'..stats..',"results":'..summaries..'}\n')
 print(('Native %s: %d valid fights; GPU %.0f sims/sec; end-to-end %.0f sims/sec (%.3f s)'):format(variant,total,metadata.timing.gpuSimsPerSecond or 0,metadata.timing.endToEndSimsPerSecond,wallMs/1000))
 print('Saved '..opts.output)
end
local ok,err=xpcall(execute,debug.traceback)
iohelp.run('rm -rf -- '..iohelp.quote(temp))
if not ok then error(err) end
