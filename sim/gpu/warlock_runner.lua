local contract=require('contracts.packed_evaluation')
local layout=require('sim.classes.warlock.generated.layout')
local reports=require('sim.classes.warlock.reports')
local specialize=require('sim.classes.warlock.specialize')
local Runner={};Runner.__index=Runner
local function release(object) if object then object:release() end end
function Runner.new()
 return setmetatable({status='idle',pipelines={},completed=0,draws=0},Runner)
end
function Runner:releaseBuffers()
 for _,key in ipairs({'readback','configBuffer','parameterBuffer','resultBuffer'}) do release(self[key]);self[key]=nil end
end
function Runner:dispose()
 self:releaseBuffers()
 for _,pipeline in pairs(self.pipelines) do pipeline:release() end
 self.pipelines={}
 if self.status=='running' then self.status='cancelled';self.result=nil;self.accumulators=nil;self.rawParts=nil end
end
function Runner:fail(err)
 self.status='failed';self.error=tostring(err);self.result=nil;self.accumulators=nil;self.rawParts=nil;self:releaseBuffers()
end
function Runner:cancel()
 if self.status=='running' then self.cancelRequested=true end
end
function Runner:start(request)
 assert(self.status~='running','An evaluation is already running')
 local snapshot=contract.validate(request) -- No caller mutation after submission.
 assert(require('app.runtime').inspect(false).ready,'Warlock runner requires LÖVE 12 with Vulkan')
 self:releaseBuffers()
 self.request=snapshot;self.status='running';self.error=nil;self.result=nil;self.cancelRequested=false
 self.completed=0;self.draws=0;self.accumulators={};self.rawParts=snapshot.retainRawReports and {} or nil;self.started=love.timer.getTime();self.compileMs=0;self.decodeMs=0
 local ok,err=pcall(function()
  local packed={}
  for index,candidate in ipairs(snapshot.candidates) do
   for _,word in ipairs(candidate.packed) do packed[#packed+1]=word end
   self.accumulators[index]=reports.accumulator(candidate.id,candidate.duration,snapshot.retainSamples)
  end
  self.total=#snapshot.candidates*snapshot.iterations
  self.capacity=math.min(snapshot.batchSize,self.total)
  self.words=snapshot.mode=='fast' and layout.fastWords or layout.detailedWords
  local before=love.timer.getTime()
  local shaderSource=love.filesystem.read('sim/classes/warlock/generated/'..snapshot.mode..'.glsl')
  local pipelineKey=snapshot.mode
  self.optimization={variant='generic',features={}}
  if snapshot.specializeFeatures then
   local specializedSource,features=specialize.specialize(shaderSource,snapshot.candidates)
   local keys={}
   for k,v in pairs(features) do keys[#keys+1]=k..'='..tostring(v) end
   table.sort(keys)
   pipelineKey=snapshot.mode..':spec:'..table.concat(keys,',')
   shaderSource=specializedSource
   self.optimization={variant='shared-features',features=features}
  end
  if snapshot.subgroupSize then
   assert(love.graphics.fastsimComputeTiming,'Explicit subgroup width requires the patched LÖVE runtime; rebuild with tools/build_love12.sh')
   pipelineKey=pipelineKey..':wave'..snapshot.subgroupSize
   self.optimization.requiredSubgroupSize=snapshot.subgroupSize
   self.optimization.variant=self.optimization.variant..'-wave'..snapshot.subgroupSize
  end
  if not self.pipelines[pipelineKey] then
   self.pipelines[pipelineKey]=love.graphics.newComputeShader(shaderSource,{requiredsubgroupsize=snapshot.subgroupSize})
  end
  self.compileMs=(love.timer.getTime()-before)*1000
  self.pipeline=self.pipelines[pipelineKey]
  self.configBuffer=love.graphics.newBuffer('uint32',packed,{shaderstorage=true})
  self.parameterBuffer=love.graphics.newBuffer('uint32',6,{shaderstorage=true})
  self.resultBuffer=love.graphics.newBuffer('uint32',self.capacity*self.words,{shaderstorage=true})
  assert(self.configBuffer:getElementStride()==4 and self.parameterBuffer:getElementStride()==4 and self.resultBuffer:getElementStride()==4,'Unexpected uint32 buffer stride')
  self.pipeline:send('Configs',self.configBuffer);self.pipeline:send('Parameters',self.parameterBuffer);self.pipeline:send('Results',self.resultBuffer)
 end)
 if not ok then self:fail(err) end
 return ok,self.error
end
function Runner:submit()
 local r=self.request
 self.batchCount=math.min(self.capacity,self.total-self.completed)
 self.parameterBuffer:setArrayData({#r.candidates,r.iterations,r.seed,self.completed,self.batchCount,r.eventBudget})
 love.graphics.dispatchThreadgroups(self.pipeline,math.ceil(self.batchCount/layout.workgroupSize),1,1)
 self.readback=love.graphics.readbackBufferAsync(self.resultBuffer,0,self.batchCount*self.words*4)
 self.submitted=love.timer.getTime();self.draws=self.draws+1
 if r.blockingReadback then self.readback:wait() end
end
function Runner:warmup(batches)
 assert(self.status=='running' and self.completed==0 and not self.readback,'Warmup must precede measured dispatches')
 for _=1,batches do
  self:submit();self.readback:wait()
  assert(not self.readback:hasError() and self.readback:isComplete(),'Warmup readback failed')
  self.readback:release();self.readback=nil
 end
 self.draws=0
end
function Runner:finish()
 local results={}
 for i,a in ipairs(self.accumulators) do results[i]=reports.finish(a,self.request.iterations) end
 local elapsedMs=(love.timer.getTime()-self.started)*1000
 self.result={schemaVersion=1,classId='warlock',mode=self.request.mode,status='complete',results=results,optimization=self.optimization,
  rawReports=self.rawParts and table.concat(self.rawParts) or nil,
  timing={elapsedMs=elapsedMs,compileMs=self.compileMs,decodeMs=self.decodeMs,draws=self.draws,totalFights=self.total,
   simsPerSecond=elapsedMs>0 and self.total*1000/elapsedMs or nil},
  validation='diagnostic-only; accepted Forever parity remains unverified'}
 self.status='complete';self.accumulators=nil;self.rawParts=nil;self:releaseBuffers()
end
function Runner:update()
 if self.status~='running' then return end
 local ok,err=pcall(function()
  -- Cancellation never interrupts an in-flight GPU dispatch; drain it first.
  if self.readback then
   self.readback:update()
   if self.readback:hasError() then error('Warlock result readback failed') end
   if not self.readback:isComplete() then
    if love.timer.getTime()-self.submitted>30 then error('Warlock batch readback timed out after 30 seconds') end
    return
   end
   if not self.cancelRequested then
    local before=love.timer.getTime()
    local data=self.readback:getBufferData()
    assert(data and data:getSize()==self.batchCount*self.words*4,'Unexpected Warlock report byte count')
    if self.rawParts then self.rawParts[#self.rawParts+1]=data:getString() end
    reports.accumulateBatch(self.accumulators,data,self.completed,self.batchCount,self.request.iterations,self.request.eventBudget,self.request.mode)
    self.completed=self.completed+self.batchCount
    self.decodeMs=self.decodeMs+(love.timer.getTime()-before)*1000
   end
   release(self.readback);self.readback=nil
   -- Yield a frame before the next submission so callers can cancel.
   if not self.cancelRequested and self.completed<self.total then return end
  end
  if self.cancelRequested then
   self.status='cancelled';self.result=nil;self.accumulators=nil;self.rawParts=nil;self:releaseBuffers();return
  end
  if self.completed==self.total then self:finish() else self:submit() end
 end)
 if not ok then self:fail(err) end
end
return Runner
