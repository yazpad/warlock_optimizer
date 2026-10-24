-- Actual LÖVE buffers/dispatch/readback. No C headless transport is used here.
local M={}
local function sha(text) return love.data.hash('string','sha256',text):gsub('.',function(c) return ('%02x'):format(c:byte()) end) end
local function pump(runner)
 if not love.event or not love.event.pump then return end
 love.event.pump()
 for event,key in love.event.poll() do
  if event=='quit' or (event=='keypressed' and key=='escape') then runner:cancel() end
 end
end
function M.run(options)
 options=options or {}
 local total=options.totalFights or 1000000
 local runs=options.runs or 3
 local batch=options.batchSize or 65536
 local warmups=options.warmupBatches or 2
 local width=options.subgroupSize
 if width==nil and not options.driverWidth then width=32 end
 assert(type(runs)=='number' and runs%1==0 and runs>=1 and runs<=10,'runs must be 1..10')
 assert(type(warmups)=='number' and warmups%1==0 and warmups>=0 and warmups<=32,'warmup batches must be 0..32')
 assert(love.graphics.fastsimComputeTiming,'Rebuild LÖVE with tools/build_love12.sh: subgroup control and GPU timestamps require the Fastsim patch')
 local suite=require('data.validation.forever_baseline')
 local runtime=require('app.runtime').inspect(false)
 local device=runtime.device:lower()
 assert(options.allowSoftware or not (device:find('llvmpipe',1,true) or device:find('lavapipe',1,true) or device:find('swiftshader',1,true) or device:find('software',1,true)),'Hardware benchmark rejects software Vulkan; use --allow-software for diagnostics')
 assert(total%#suite.scenarios==0,'Fight count must be divisible by scenario count')
 for path,hash in pairs(suite.kernelHashes) do assert(sha(assert(love.filesystem.read(path)))==hash,'Stale suite shader/layout: '..path) end
 assert(sha(assert(love.filesystem.read('data/validation/forever_baseline.json')))==suite.suiteHash,'Stale JSON/Lua suite')
 local candidates={};for i,s in ipairs(suite.scenarios) do candidates[i]={id=s.id,packed=s.packed,duration=s.duration} end
 require('contracts.packed_evaluation').validate({schemaVersion=1,classId='warlock',mode='fast',iterations=total/#candidates,seed=suite.seed,batchSize=batch,candidates=candidates,specializeFeatures=not options.generic,subgroupSize=width,blockingReadback=true})
 local hostHashes={}
 for _,file in ipairs({'main.lua','benchmarks/love.lua','contracts/packed_evaluation.lua','sim/gpu/warlock_runner.lua','sim/classes/warlock/specialize.lua','sim/classes/warlock/reports.lua','tools/patches/love12-fastsim.patch'}) do hostHashes[file]=sha(assert(love.filesystem.read(file))) end
 -- Preflight exact raw-word replay and completion checks outside performance timing.
 local observedWidth=require('validation.subgroup_gpu').check(width)
 print(('PASS: subgroup canary observed %d; benchmark kernels use %s'):format(observedWidth,width and ('enforced width '..width) or 'driver-selected widths (not inferred from canary)'))
 local check=require('validation.warlock_gpu').new({subgroupSize=width,blockingReadback=true})
 check:start()
 while check.status=='running' do check:update() end
 assert(check.status=='passed',check.message)
 print('PASS: preflight exact generic/specialized/subgroup report replay (fast and detailed)')
 local runner=require('sim.gpu.warlock_runner').new()
 local report={schemaVersion=1,transport='LOVE 12 Vulkan',status='running',suiteId=suite.suiteId,suiteHash=suite.suiteHash,kernelHashes=suite.kernelHashes,hostHashes=hostHashes,
  runtime=runtime,requiredSubgroupSize=width,canaryObservedSubgroupSize=observedWidth,subgroupEnforcement=width and 'required Vulkan compute pipeline subgroup size; verified by separate canary' or 'driver-selected; canary width does not establish combat-kernel width',totalFights=total,iterations=total/#candidates,seed=suite.seed,batchSize=batch,warmupBatches=warmups,
  preflight='passed: exact fast and detailed raw report replay against generic',runs={},
  timingScope='GPU timestamps cover measured dispatches only. Evaluation wall time includes configuration, shader creation, two optional warmup dispatches, readback and LuaJIT summary; excludes process/window startup and preflight. Steady time excludes configuration/compilation/warmups.',
  validation='diagnostic-only; accepted Forever parity remains open'}
 local path=options.output or 'love-benchmark.json'
 local function save() assert(love.filesystem.write(path,require('app.json').encode(report)..'\n'),'Cannot save benchmark') end
 local activeTiming=false
 local ok,err=xpcall(function()
  for index=1,runs do
   local started=love.timer.getTime()
   assert(runner:start({schemaVersion=1,classId='warlock',mode='fast',iterations=report.iterations,seed=suite.seed,batchSize=batch,candidates=candidates,specializeFeatures=not options.generic,subgroupSize=width,blockingReadback=true}))
   local beforeWarmup=love.timer.getTime();runner:warmup(warmups)
   local warmupMs=(love.timer.getTime()-beforeWarmup)*1000
   love.graphics.fastsimComputeTiming(true);activeTiming=true
   local steadyStart=love.timer.getTime()
   while runner.status=='running' do pump(runner);runner:update() end
   assert(runner.status=='complete',runner.error or runner.status)
   local gpuMs,dispatches=love.graphics.fastsimComputeTiming(false);activeTiming=false
   local wallMs=(love.timer.getTime()-started)*1000
   local steadyMs=(love.timer.getTime()-steadyStart)*1000
   assert(dispatches==math.ceil(total/batch) and gpuMs>0,'Missing or invalid GPU dispatch timestamps')
   local result=runner.result
   local valid=0;for _,r in ipairs(result.results) do assert(r.sampleCount==report.iterations and r.status=='complete','Incomplete candidate');valid=valid+r.sampleCount end
   assert(valid==total,'Fight count mismatch')
   -- Repeat stability checks all aggregates, including IDs/counters and Welford stats.
   if report.runs[1] then assert(require('app.json').encode(report.runs[1].results)==require('app.json').encode(result.results),'Repeated evaluation results changed') end
   local timing={gpuComputeMs=gpuMs,wallMs=wallMs,steadyMs=steadyMs,warmupMs=warmupMs,compileMs=result.timing.compileMs,decodeMs=result.timing.decodeMs,dispatches=dispatches,
    gpuSimsPerSecond=total*1000/gpuMs,evaluationSimsPerSecond=total*1000/wallMs,steadySimsPerSecond=total*1000/steadyMs}
   report.runs[index]={timing=timing,optimization=result.optimization,results=result.results}
   print(('LOVE %s run %d: %d valid fights; GPU %.0f sims/sec (%.3f s); evaluation %.0f sims/sec (%.3f s); steady %.0f sims/sec; decode %.1f ms'):format(result.optimization.variant,index,total,timing.gpuSimsPerSecond,gpuMs/1000,timing.evaluationSimsPerSecond,wallMs/1000,timing.steadySimsPerSecond,timing.decodeMs))
   save()
  end
  report.medians={}
  for _,metric in ipairs({'gpuComputeMs','wallMs','steadyMs','gpuSimsPerSecond','evaluationSimsPerSecond','steadySimsPerSecond'}) do
   local values={};for _,r in ipairs(report.runs) do values[#values+1]=r.timing[metric] end;table.sort(values)
   local n=#values;report.medians[metric]=(values[math.floor((n+1)/2)]+values[math.floor((n+2)/2)])/2
  end
  report.status='complete'
  print(('LOVE median: GPU %.0f sims/sec; evaluation %.0f sims/sec; steady %.0f sims/sec'):format(report.medians.gpuSimsPerSecond,report.medians.evaluationSimsPerSecond,report.medians.steadySimsPerSecond))
 end,debug.traceback)
 if activeTiming then pcall(love.graphics.fastsimComputeTiming,false) end
 runner:dispose()
 if not ok then report.status='failed';report.error=err end
 save();print('Saved '..love.filesystem.getSaveDirectory()..'/'..path)
 assert(ok,err)
 return report
end
return M
