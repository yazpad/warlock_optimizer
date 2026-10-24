-- Actual GPU integration check, run through --warlock-smoke. No parity claim.
local Runner=require('sim.gpu.warlock_runner')
local example=require('sim.classes.warlock.generated.example')
local Check={};Check.__index=Check
local cases={
 {mode='fast',batch=64},{mode='fast',batch=17},{mode='detailed',batch=17},
 {mode='fast',batch=64,specialize=true},{mode='fast',batch=17,specialize=true},
 {mode='detailed',batch=17,specialize=true},
 {mode='fast',batch=17,specialize=true,aggregate=true},
 {mode='detailed',batch=17,specialize=true,aggregate=true},
 {mode='fast',batch=17,budget=1},{mode='fast',batch=17,budget=1,specialize=true},
}
local compareFields={'total','done','highWater','events','taps','procs','isbProcs','isbConsumed','petBrandDamage','petDamage','petCasts','rngCalls','petMeleeDamage','petMeleeCasts','petMeleeHits','petMeleeCrits','petMeleeMisses','petSpellDamage','petSpellCasts','petSpellHits','petSpellCrits','petSpellMisses','mana','spent'}
function Check.new(options)
 local selected={};for _,case in ipairs(cases) do selected[#selected+1]=case end
 options=options or {}
 if options.subgroupSize then
  for _,case in ipairs({{mode='fast',batch=17},{mode='detailed',batch=17},{mode='fast',batch=17,specialize=true},{mode='detailed',batch=17,specialize=true},{mode='fast',batch=17,specialize=true,aggregate=true}}) do
   case.subgroupSize=options.subgroupSize;selected[#selected+1]=case
  end
 end
 return setmetatable({status='idle',runner=Runner.new(),index=0,cases=selected,blockingReadback=options.blockingReadback},Check)
end
function Check:next()
 self.index=self.index+1
 local case=self.cases[self.index]
 if not case then self.status='passed';self.message='Warlock GPU checks passed: generic/specialized fast/detailed replay, aggregate decoding, batching, candidate mapping, and event-budget rejection.';self.runner:dispose();return end
 local ok,err=self.runner:start({schemaVersion=1,classId='warlock',mode=case.mode,iterations=example.iterations,seed=example.seed,batchSize=case.batch,eventBudget=case.budget,retainSamples=not case.aggregate,retainRawReports=true,specializeFeatures=case.specialize==true,subgroupSize=case.subgroupSize,blockingReadback=self.blockingReadback,
 candidates={{id='first',packed=example.packed},{id='second',packed=example.packed}}})
 assert(ok,err)
 self.message=('Warlock GPU check %d/%d (%s, %s, width %s, batch %d)'):format(self.index,#self.cases,case.mode,case.specialize and 'specialized' or 'generic',tostring(case.subgroupSize or 'default'),case.batch)
end
function Check:start()
 self.status='running';self.baseline=nil;self.rawBaselines={};self.index=0
 local ok,err=pcall(function() self:next() end)
 if not ok then self.status='failed';self.message=tostring(err);self.runner:dispose() end
end
function Check:update()
 if self.status~='running' then return end
 local ok,err=pcall(function()
  self.runner:update()
  local case=self.cases[self.index]
  if case.budget then
   if self.runner.status=='running' then return end
   assert(self.runner.status=='failed' and self.runner.error:find('Incomplete fight',1,true),'Event-budget case must reject an incomplete fight')
   self:next();return
  end
  if self.runner.status=='failed' then error(self.runner.error) end
  if self.runner.status~='complete' then return end
  local variant=(case.specialize and 'shared-features' or 'generic')..(case.subgroupSize and '-wave'..case.subgroupSize or '')
  assert(self.runner.result.optimization.variant==variant,'Requested shader variant was not used')
  local raw=self.runner.result.rawReports
  assert(type(raw)=='string' and #raw>0,'Missing raw replay reports')
  if not self.rawBaselines[case.mode] then self.rawBaselines[case.mode]=raw end
  assert(raw==self.rawBaselines[case.mode],'Report-word replay mismatch for '..variant..' '..case.mode)
  local results=self.runner.result.results
  if not self.baseline then self.baseline=results end
  for candidate,result in ipairs(results) do
   assert(result.candidateId==(candidate==1 and 'first' or 'second'),'Candidate identity changed')
   assert(result.sampleCount==example.iterations,'Incorrect sample count')
   local baseline=self.baseline[candidate]
   assert(result.meanDps==baseline.meanDps and result.sd==baseline.sd,'Aggregate statistics changed')
   assert(result.maxHeap==baseline.maxHeap,'Aggregate heap maximum changed')
   for _,field in ipairs(compareFields) do
    if field~='done' and field~='highWater' then assert(result.totals[field]==baseline.totals[field],'Aggregate total changed: '..field) end
   end
   assert(case.aggregate and result.samples==nil or not case.aggregate and #result.samples==example.iterations,'Sample retention changed')
   for i,state in ipairs(result.samples or {}) do
    for _,field in ipairs(compareFields) do
     local expected=self.baseline[1].samples[i][field]
     assert(state[field]==expected,('Replay mismatch in case %d candidate %d sample %d field %s'):format(self.index,candidate,i,field))
    end
   end
  end
  self:next()
 end)
 if not ok then self.status='failed';self.message=tostring(err);self.runner:dispose() end
end
function Check:dispose() self.runner:dispose() end
return Check
