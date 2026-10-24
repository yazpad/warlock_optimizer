-- Minimal compute workbench. Product/search UI is intentionally deferred.
local runtime,compute,warlock,reference
local options={}
local task=nil
local notice='B: specialized wave32 benchmark   G: generic wave32 benchmark   Esc: quit'
local lastReport
local benchmarkPending=false
local function number(args,i,name) return assert(tonumber(args[i+1]),'Missing/invalid '..name) end
function love.load(args)
 local preview=false
 local i=1
 while i<=#args do
  local a=args[i]
  if a=='--preview' then preview=true
  elseif a=='--compute-smoke' or a=='--warlock-smoke' or a=='--parity-reference' or a=='--benchmark' then assert(not task,'Choose one task');task=a
  elseif a=='--specialize-features' then options.specializeFeatures=true
  elseif a=='--generic' then options.generic=true
  elseif a=='--allow-software' then options.allowSoftware=true
  elseif a=='--no-subgroup-size' then options.driverWidth=true;options.subgroupSize=nil
  elseif a=='--subgroup-size' then options.subgroupSize=number(args,i,a);options.driverWidth=false;i=i+1
  elseif a=='--total-fights' then options.totalFights=number(args,i,a);i=i+1
  elseif a=='--batch-size' then options.batchSize=number(args,i,a);i=i+1
  elseif a=='--warmup-batches' then options.warmupBatches=number(args,i,a);i=i+1
  elseif a=='--runs' then options.runs=number(args,i,a);i=i+1
  elseif a=='--output' then options.output=assert(args[i+1],'Missing output filename');i=i+1
  else error('Unknown option: '..a) end
  i=i+1
 end
 runtime=require('app.runtime').inspect(preview)
 assert(preview or runtime.ready,runtime.message)
 assert(not task or runtime.ready,'Compute tasks require LÖVE 12/Vulkan')
 print('Fastsim | LOVE '..runtime.version..' | '..runtime.backend..' | '..runtime.device)
 compute=require('sim.gpu.smoke').new()
 warlock=require('validation.warlock_gpu').new({subgroupSize=options.subgroupSize})
 reference=require('validation.reference_job').new()
 if runtime.ready then compute:start() else notice='UI preview only. LÖVE 12/Vulkan required for benchmarks.' end
end
local function runBenchmark()
 benchmarkPending=false
 notice='Benchmark running; Esc cancels between dispatches. Results are printed to the terminal.'
 lastReport=require('benchmarks.love').run(options)
 notice=('GPU %.0f sims/sec | evaluation %.0f sims/sec | steady %.0f sims/sec'):format(lastReport.medians.gpuSimsPerSecond,lastReport.medians.evaluationSimsPerSecond,lastReport.medians.steadySimsPerSecond)
 if task=='--benchmark' then love.event.quit(0) end
end
function love.update()
 if not compute then return end
 compute:update()
 if compute.status=='failed' then print(compute.message);if task then love.event.quit(1) end;return end
 if compute.status~='passed' then return end
 if not compute.reported then print(compute.message);compute.reported=true end
 if task=='--compute-smoke' then love.event.quit(0);return end
 if task=='--benchmark' or benchmarkPending then runBenchmark();return end
 if task=='--warlock-smoke' or task=='--parity-reference' then
  if warlock.status=='idle' then warlock:start() end
  warlock:update()
  if warlock.status=='failed' then print(warlock.message);love.event.quit(1);return end
  if warlock.status~='passed' then return end
  if task=='--warlock-smoke' then print(warlock.message);love.event.quit(0);return end
  if reference.status=='idle' then reference:start(options.specializeFeatures) end
  reference:update()
  if reference.status=='complete' or reference.status=='failed' then print(reference.message);love.event.quit(reference.status=='complete' and 0 or 1) end
 end
end
function love.draw()
 if not runtime then return end
 love.graphics.clear(0.055,0.06,0.08)
 love.graphics.setColor(0.9,0.91,0.96)
 love.graphics.print('Fastsim / Vulkan compute benchmark',24,24)
 love.graphics.print('LOVE '..runtime.version..' | '..runtime.backend..' | '..runtime.device,24,56)
 love.graphics.print('Frozen workload: 5 Warlock builds x 200,000 fights; 180 seconds; heap40; batches of 65,536.',24,100)
 love.graphics.printf(notice,24,140,love.graphics.getWidth()-48)
 love.graphics.print(compute and (compute.message or compute.status) or '',24,200)
 love.graphics.print('1.56M GPU sims/sec is the native target; LÖVE rates are measured separately.',24,240)
end
function love.keypressed(key)
 if key=='escape' then love.event.quit()
 elseif runtime and runtime.ready and compute.status=='passed' and not task then
  if key=='b' or key=='g' then options.generic=key=='g';benchmarkPending=true end
 end
end
function love.quit()
 if compute then compute:dispose() end
 if warlock then warlock:dispose() end
 if reference then reference:dispose() end
end
function love.errorhandler(message)
 io.stderr:write(debug.traceback(tostring(message),2)..'\n')
 return function() return 1 end
end
