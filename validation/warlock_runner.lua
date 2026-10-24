-- Synthetic GPU transport tests. No combat mechanics are simulated here.
local ffi=require('ffi')
ffi.cdef('typedef union { uint32_t word; float number; char bytes[4]; } fastsim_test_word;')
local function pack(format,value)
 local v=ffi.new('fastsim_test_word')
 if format=='=f' then v.number=value else v.word=value end
 return ffi.string(v.bytes,4)
end
local function unpackWord(format,text,position)
 local v=ffi.new('fastsim_test_word');ffi.copy(v.bytes,text:sub(position,position+3),4)
 return tonumber(format=='=f' and v.number or v.word)
end
local layout=require('sim.classes.warlock.generated.layout')
local fixture=require('sim.classes.warlock.generated.example')
local Runner=require('sim.gpu.warlock_runner')
local contract=require('contracts.packed_evaluation')
local reports=require('sim.classes.warlock.reports')
local now,mode,dispatches,compiles,releases=0,'ok',0,0,0
local function copy(t) local r={};for k,v in pairs(t) do r[k]=v end;return r end
local function bytes(text) return {text=text,getSize=function() return #text end,getString=function() return text end} end
local function resource(t) t.release=function() releases=releases+1 end;return t end
love={graphics={},timer={},filesystem={},data={}}
love.timer.getTime=function() return now end
love.getVersion=function() return 12,0,0 end
love.graphics.getRendererInfo=function() return 'Vulkan','1.3','test','synthetic GPU' end
love.filesystem.read=function(path)
 if path:sub(-5)=='.glsl' then local f=assert(io.open(path,'rb'));local text=f:read('*a');f:close();return text end
 return path
end
love.data.unpack=function(format,data,position) return unpackWord('='..format,data.text,position) end
love.graphics.newBuffer=function(_,input)
 local b={values=type(input)=='table' and copy(input) or {},size=type(input)=='number' and input or #input}
 b.getElementStride=function() return 4 end
 b.setArrayData=function(self,values) self.values=copy(values) end
 return resource(b)
end
love.graphics.newComputeShader=function(path,options)
 compiles=compiles+1
 assert(not options.requiredsubgroupsize or options.requiredsubgroupsize==8 or options.requiredsubgroupsize==32,'Unexpected subgroup request')
 return resource({path=path,bound={},send=function(self,name,buffer) self.bound[name]=buffer end})
end
local active
love.graphics.dispatchThreadgroups=function(shader,groups)
 dispatches=dispatches+1
 local parameters=shader.bound.Parameters.values
 local candidates,iterations,seed,offset,count,budget=unpack(parameters)
 assert(groups==math.ceil(count/64) and #parameters==6 and seed==101)
 local reportMode=(shader.path:find('lane*163u',1,true) or shader.path:find('detailed.glsl',1,true)) and 'detailed' or 'fast'
 local words=reportMode=='fast' and layout.fastWords or layout.detailedWords
 local result={}
 for lane=0,count-1 do
  local candidate=math.floor((offset+lane)/iterations)
  local fight=(offset+lane)%iterations
  assert(candidate<candidates)
  local raw={};for word=0,words-1 do raw[word]=pack('=I4',0) end
  local fields=layout.fields[reportMode]
  local durationWord=shader.bound.Configs.values[candidate*layout.configWords+layout.endIndex]
  local damage=1000+(durationWord==60000000 and 100 or 0)+fight*10
  raw[0]=pack('=f',mode=='nan' and 0/0 or damage)
  raw[1]=pack('=I4',4*65536+((mode=='incomplete' or budget==1) and 6 or 1))
  raw[2]=pack('=I4',mode=='events' and budget+1 or 5)
  raw[3]=pack('=I4',2+3*65536)
  raw[fields.mana.word]=pack('=f',200)
  raw[fields.spent.word]=pack('=f',300)
  for word=0,words-1 do result[#result+1]=raw[word] end
 end
 active=table.concat(result)
end
love.graphics.readbackBufferAsync=function(_,offset,size)
 local text=active
 assert(offset==0 and size==#text,'Readback must include only active lanes')
 if mode=='size' then text=text:sub(1,-2) end
 return resource({update=function() end,wait=function() end,hasError=function() return mode=='readback' end,
  isComplete=function() return mode~='waiting' end,getBufferData=function() return bytes(text) end})
end
local function request(reportMode,batch)
 local second=copy(fixture.packed);second[layout.endIndex]=60000000
 return {schemaVersion=1,classId='warlock',mode=reportMode or 'fast',iterations=5,seed=101,batchSize=batch or 3,retainSamples=true,
 candidates={{id='alpha',packed=copy(fixture.packed),duration=180},{id='beta',packed=second,duration=60}}}
end
local function drain(r)
 for _=1,100 do r:update();if r.status~='running' then break end end
 assert(r.status~='running','Job did not terminate')
end
for _,reportMode in ipairs({'fast','detailed'}) do
 local r=Runner.new()
 for _,batch in ipairs({1,3,5,7,64}) do
  local req=request(reportMode,batch)
  assert(r:start(req))
  -- Caller edits cannot alter an active job.
  req.candidates[1].packed[layout.endIndex]=1000000;req.candidates[1].id='changed';req.iterations=99
  drain(r);assert(r.status=='complete',r.error)
  local result=r.result.results
  assert(result[1].candidateId=='alpha' and result[2].candidateId=='beta')
  assert(math.abs(result[1].meanDps-1020/180)<1e-12)
  assert(math.abs(result[2].meanDps-1120/60)<1e-12)
  assert(result[1].sampleCount==5 and result[2].sampleCount==5)
  assert(result[1].totals.total==5100 and result[1].totals.taps==10 and result[1].totals.procs==15)
  assert(math.abs(result[1].sd-math.sqrt(250)/180)<1e-12,'Welford sample variance incorrect')
  assert(#result[1].samples==5 and result[1].samples[5].total==1040)
  assert(r.result.timing.draws==math.ceil(10/batch))
  assert(r.resultBuffer==nil,'Completed results must release transport buffers')
 end
 r:dispose()
end
assert(compiles==2,'Same-mode pipelines should be cached per runner')
-- Packed requests reject malformed and nonfinite inputs before GPU allocation.
for _,change in ipairs({
 function(r) r.seed=-1 end,
 function(r) r.candidates[2].id='alpha' end,
 function(r) r.candidates[1].packed[1]=-1 end,
 function(r) r.candidates[1].duration=1 end,
 function(r) r.mode='unknown' end,
 function(r) r.batchSize=65537 end,
 function(r) r.specializeFeatures='yes' end,
 function(r) r.subgroupSize=12 end,
 function(r) r.blockingReadback='yes' end,
 function(r) r.retainRawReports='yes' end,
 function(r) for i,t in ipairs(layout.configTypes) do if t=='f32' then r.candidates[1].packed[i]=2139095040;break end end end,
}) do local req=request();change(req);assert(not pcall(contract.validate,req)) end
for _,failure in ipairs({'incomplete','nan','events','size','readback','waiting'}) do
 mode=failure;now=0
 local r=Runner.new();assert(r:start(request()));r:update();now=31;r:update()
 assert(r.status=='failed',failure..' must fail');assert(r.result==nil and r.readback==nil and r.accumulators==nil)
 mode='ok';assert(r:start(request()));drain(r);assert(r.status=='complete',r.error);r:dispose()
end
-- Cancel before submission and while readback is pending; never accept partial results.
local r=Runner.new();local before=dispatches;assert(r:start(request()));r:cancel();r:update()
assert(r.status=='cancelled' and dispatches==before and r.result==nil)
assert(r:start(request()));mode='waiting';r:update();r:cancel();r:update();assert(r.status=='running','Pending readback must drain')
mode='ok';r:update();assert(r.status=='cancelled' and r.result==nil and r.completed==0)
assert(r:start(request()));r:update();r:update();assert(r.completed==3);r:cancel();before=dispatches;r:update()
assert(r.status=='cancelled' and r.result==nil and dispatches==before)
r:dispose()
-- Specialization is an immutable request option with a separate cached pipeline.
local specialized=Runner.new()
local req=request();req.specializeFeatures=true
assert(specialized:start(req));req.specializeFeatures=false
assert(specialized.request.specializeFeatures==true)
drain(specialized);assert(specialized.status=='complete',specialized.error)
assert(specialized.result.optimization.variant=='shared-features')
assert(specialized.result.optimization.features.race~=nil)
local compiled=compiles
req=request();req.specializeFeatures=true;assert(specialized:start(req));drain(specialized)
assert(compiles==compiled,'Specialized pipelines should be cached')
assert(specialized:start(request()));drain(specialized)
assert(specialized.result.optimization.variant=='generic' and compiles==compiled+1)
specialized:dispose()
-- The real-GPU check's orchestration also runs against this synthetic transport.
local integration=require('validation.warlock_gpu').new();integration:start()
for _=1,1000 do integration:update();if integration.status~='running' then break end end
assert(integration.status=='passed',integration.message)
-- Validate tail fields and packed uptime decoding, not just the common report prefix.
local raw={};for _=1,layout.detailedWords do raw[#raw+1]=pack('=I4',0) end
raw[1]=pack('=f',42);raw[2]=pack('=I4',1)
raw[layout.fields.detailed.uptime_corr.word+1]=pack('=I4',65535)
raw[layout.fields.detailed.gained.word+1]=pack('=f',123.5)
local decoded=reports.decode(bytes(table.concat(raw)),0,'detailed')
assert(decoded.uptime_corr==1 and decoded.uptime_agony==0 and decoded.gained==123.5)
assert(releases>0)
print('PASS: packed contracts, batch boundaries, candidate mapping, decoding, statistics, failures, cancellation, cached pipelines and GPU-check orchestration (synthetic transport)')
-- Exercise frozen-suite hash validation and JSON export through the host runner.
love.filesystem.read=function(path) return path end -- Hash mocks use paths as tokens.
local suite=require('data.validation.forever_baseline')
local hashes=suite.kernelHashes
local originalHash
love.data.hash=function(container,algorithm,path)
 assert(container=='string' and algorithm=='sha256')
 local hex=path=='data/validation/forever_baseline.json' and suite.suiteHash or hashes[path]
 assert(hex,'Unexpected fixture hash request')
 return hex:gsub('..',function(pair) return string.char(tonumber(pair,16)) end)
end
local saved
love.filesystem.write=function(name,text) assert(name=='warlock-parity.json');saved=text;return true end
love.filesystem.getSaveDirectory=function() return '/synthetic-save' end
local ReferenceJob=require('validation.reference_job')
local job=ReferenceJob.new();job:start()
for _=1,1000 do job:update();if job.status~='running' then break end end
assert(job.status=='complete',job.message)
assert(saved:find('"suiteHash":"'..suite.suiteHash..'"',1,true),'Export must identify exact frozen suite')
assert(saved:find('"sampleCount":1000',1,true),'Export must retain measured sample counts')
job:dispose()
-- The reference CLI can explicitly request the specialized shader too.
local tokenRead=love.filesystem.read
love.filesystem.read=function(path)
 if path:sub(-5)=='.glsl' then local f=assert(io.open(path,'rb'));local text=f:read('*a');f:close();return text end
 return path
end
local tokenHash=love.data.hash
love.data.hash=function(container,algorithm,value)
 if value:find('#pragma language glsl4',1,true) then
  for path in pairs(hashes) do
   if path:sub(-5)=='.glsl' and love.filesystem.read(path)==value then return tokenHash(container,algorithm,path) end
  end
 end
 return tokenHash(container,algorithm,value)
end
local specializedJob=ReferenceJob.new();specializedJob:start(true)
for _=1,1000 do specializedJob:update();if specializedJob.status~='running' then break end end
assert(specializedJob.status=='complete',specializedJob.message)
assert(saved:find('"variant":"shared-features"',1,true),'Reference export must identify specialization')
specializedJob:dispose();love.filesystem.read=tokenRead;love.data.hash=tokenHash
originalHash=love.data.hash
love.data.hash=function() return string.rep('\0',32) end
local stale=ReferenceJob.new();stale:start();assert(stale.status=='failed' and stale.message:find('stale',1,true))
stale:dispose();love.data.hash=originalHash
print('PASS: frozen-reference host job, snapshot mismatch rejection and JSON export (synthetic transport)')
-- Explicit widths must fail on an unpatched runtime, never silently fall back.
local explicit=Runner.new();local widthRequest=request();widthRequest.subgroupSize=32
local ok=explicit:start(widthRequest)
assert(not ok and explicit.error:find('patched',1,true));explicit:dispose()
-- Benchmark orchestration uses this synthetic transport, not real GPU timings.
local benchmarkPrint=print
print=function(message) benchmarkPrint('[synthetic transport] '..tostring(message)) end
love.filesystem.read=function(path)
 if path:sub(-5)=='.glsl' then local f=assert(io.open(path,'rb'));local text=f:read('*a');f:close();return text end
 return path
end
love.data.hash=function(container,algorithm,value)
 if value:find('#pragma language glsl4',1,true) then
  for path in pairs(hashes) do
   if path:sub(-5)=='.glsl' and love.filesystem.read(path)==value then return tokenHash(container,algorithm,path) end
  end
 end
 if value=='data/validation/forever_baseline.json' or hashes[value] then return tokenHash(container,algorithm,value) end
 return string.rep('\171',32) -- Synthetic identities for other host source files.
end
local benchmarkSaved
love.filesystem.write=function(name,text) assert(name=='love-benchmark.json');benchmarkSaved=text;return true end
love.timer.getTime=function() now=now+0.00001;return now end
local startDispatches,badTiming
love.graphics.fastsimComputeTiming=function(begin)
 if begin then startDispatches=dispatches;return 0,0 end
 return badTiming and 0 or 10,dispatches-startDispatches
end
package.loaded['validation.subgroup_gpu']={check=function(required) assert(required==32);return 32 end}
local bench=require('benchmarks.love')
local report=bench.run({totalFights=10,runs=2,batchSize=4,warmupBatches=2})
assert(report.status=='complete' and report.canaryObservedSubgroupSize==32 and report.requiredSubgroupSize==32)
assert(#report.runs==2 and report.runs[1].timing.dispatches==3)
assert(report.runs[1].optimization.variant=='shared-features-wave32')
assert(report.runs[1].results[1].sampleCount==2)
assert(report.medians.gpuSimsPerSecond==1000)
badTiming=true
assert(not pcall(bench.run,{totalFights=10,runs=1,batchSize=4,warmupBatches=0}))
assert(benchmarkSaved:find('"status":"failed"',1,true) and not benchmarkSaved:find('"medians"',1,true),'Failed timing must not publish accepted speed summary')
print=benchmarkPrint
print('PASS: explicit subgroup rejection, synchronous benchmark warmups/counts/repeat validation, GPU timestamp accounting and failed-result export (synthetic transport)')
