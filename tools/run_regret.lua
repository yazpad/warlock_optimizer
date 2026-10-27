#!/usr/bin/env luajit
-- Separate process/derived shader; no change to run_native.lua or Vulkan transport.
local ffi=require('ffi')
ffi.cdef[[int setenv(const char*,const char*,int);int unsetenv(const char*);]]
local bit=require('bit')
local iohelp=require('tools.native_support')
local json=require('sim.common.json')
local decode=require('sim.common.json_input').decode
local regret=require('tools.regret')
local opts={candidate=0,decisions=128,rounds=32,samples=16,validation=1000,depth=1,batchSize=65536}
local flags={['--trace-request']='traceRequest',['--output']='output',['--work-dir']='work',['--suite']='suite',['--regret-candidate']='candidate',['--regret-decisions']='decisions',['--regret-rounds']='rounds',['--regret-samples']='samples',['--regret-validation']='validation',['--regret-depth']='depth',['--batch-size']='batchSize'}
local ignored={['--total-fights']=true,['--batch-size']=true,['--warmup-batches']=true}
local i=1
while i<=#arg do
 local a=arg[i]
 if flags[a] then i=i+1;opts[flags[a]]=assert(arg[i],'Missing '..a)
 elseif ignored[a] then i=i+1;assert(arg[i],'Missing '..a)
 elseif a=='--allow-software' then opts.software=true
 elseif a~='--generic' and a~='--no-subgroup-size' and a~='--no-histogram' then error('Unknown argument: '..a) end
 i=i+1
end
local function bounded(k,lo,hi) local n=tonumber(opts[k]);assert(n and n%1==0 and n>=lo and n<=hi,'Invalid '..k);opts[k]=n end
bounded('candidate',0,4);bounded('decisions',1,128);bounded('rounds',1,256);bounded('samples',2,256);bounded('validation',2,4096);bounded('batchSize',1,65536);bounded('depth',1,regret.depth)
assert(opts.output and opts.work,'--output and --work-dir required')
local suiteSource=iohelp.read(opts.suite or 'data/validation/forever_baseline.json')
local suite=opts.suite and decode(suiteSource) or require('data.validation.forever_baseline')
local scenario=assert(suite.scenarios[opts.candidate+1],'Candidate index out of range')
assert(#scenario.packed==229 and scenario.duration>=1 and scenario.duration<=1800,'Invalid candidate')
for p,h in pairs(suite.kernelHashes) do assert(iohelp.hashFile(p)==h,'Kernel identity mismatch: '..p) end
local source=iohelp.read('sim/classes/warlock/generated/fast.glsl')
local traceRequest=opts.traceRequest and decode(iohelp.read(opts.traceRequest))
if traceRequest then
 assert(traceRequest.suiteHash==iohelp.hash(suiteSource),'Trace requires the original suite snapshot')
 assert(traceRequest.kernelHash==iohelp.hash(source) and traceRequest.detailKernelHash==iohelp.hashFile('sim/classes/warlock/generated/detailed.glsl'),'Trace kernel differs from the saved analysis; rerun regret analysis')
 assert(traceRequest.seed==suite.seed and traceRequest.candidateId==scenario.id,'Trace candidate/seed mismatch')
 assert(type(traceRequest.decision)=='number' and traceRequest.decision%1==0 and traceRequest.decision>=0 and traceRequest.decision<regret.capacity,'Invalid trace decision')
end
iohelp.run('mkdir -p artifacts/.cache')
iohelp.run('mkdir '..iohelp.quote(opts.work))
local function execute()
 local probeKey=iohelp.hash(iohelp.read('tools/vulkan_probe.c')..iohelp.read('tools/dps_histogram.h')..'cc -O2 -Wall -Wextra -Werror -lvulkan')
 local probe='artifacts/.cache/probe-'..probeKey
 if not iohelp.exists(probe) then
  iohelp.run('cc -O2 -Wall -Wextra -Werror tools/vulkan_probe.c -lvulkan -o '..iohelp.quote(opts.work..'/probe'))
  iohelp.run('mv '..iohelp.quote(opts.work..'/probe')..' '..iohelp.quote(probe))
 end
 local function compile(text)
  local wrapped=iohelp.wrap(text);local shader='artifacts/.cache/'..iohelp.hash(wrapped)..'.spv'
  if not iohelp.exists(shader) then
   iohelp.write(opts.work..'/shader.comp',wrapped)
   iohelp.run('glslangValidator -V '..iohelp.quote(opts.work..'/shader.comp')..' -o '..iohelp.quote(opts.work..'/shader.spv')..' > '..iohelp.quote(opts.work..'/compile.log')..' 2>&1')
   iohelp.run('spirv-val '..iohelp.quote(opts.work..'/shader.spv'))
   iohelp.run('mv '..iohelp.quote(opts.work..'/shader.spv')..' '..iohelp.quote(shader))
  end
  return shader
 end
 local shader=compile(regret.instrument(source));local compactShader=compile(regret.instrument(iohelp.read('sim/classes/warlock/generated/detailed.glsl'),true,true));local baselineShader=compile(source)
 ffi.C.setenv('FASTSIM_WARMUP_BATCHES','0',1);ffi.C.unsetenv('FASTSIM_SUBGROUP_SIZE')
 ffi.C.unsetenv('FASTSIM_PIPELINE_STATS_PATH');ffi.C.setenv('FASTSIM_DPS_HISTOGRAM','0',1)
 if opts.software then ffi.C.unsetenv('FASTSIM_REQUIRE_GPU') else ffi.C.setenv('FASTSIM_REQUIRE_GPU','1',1) end
 local detailWords=regret.detailWords
 local layout=require('sim.classes.warlock.generated.layout')
 local budget=scenario.duration*32+1024
 local capacity,server
 local rolloutFights,rolloutDispatches=0,0
 local transport=require('tools.regret_transport')
 local adapter=transport.build(opts.work)
 local function openServer(spv,words,lanes)
  capacity=lanes
  local config=ffi.new('uint32_t[?]',229+capacity*regret.stride)
  for k,w in ipairs(scenario.packed) do config[k-1]=w end
  local params=ffi.new('uint32_t[6]',{1,capacity,suite.seed,0,capacity,budget})
  server=transport.open(adapter,spv,opts.work,words,capacity,ffi.string(config,ffi.sizeof(config)),ffi.string(params,ffi.sizeof(params)))
 end
 openServer(shader,regret.words,1)
 local function dispatch(plans,target,sampleOffset,words,spv,sampleIds)
  local n=#plans
  if words==detailWords and sampleOffset~=nil then rolloutFights=rolloutFights+n;rolloutDispatches=rolloutDispatches+1 end
  local configs=ffi.new('uint32_t[?]',229+capacity*regret.stride)
  for k,w in ipairs(scenario.packed) do configs[k-1]=w end
  for lane,plan in ipairs(plans) do
   local base=229+(lane-1)*regret.stride
   configs[base]=type(target)=="table" and target[lane] or target;configs[base+1]=sampleIds and sampleIds[lane] or (sampleOffset==nil and 4294967295 or sampleOffset+lane)
   configs[base+2]=#plan
   for j,a in ipairs(plan) do configs[base+2+j]=a end
  end
  local params=ffi.new('uint32_t[6]',{1,n,suite.seed,0,n,budget})
  local bytes
  if spv~=baselineShader then
   local lanes=ffi.new('uint32_t[1]',n)
   bytes=server:dispatch(ffi.string(lanes,4),ffi.string(configs,ffi.sizeof(configs)),n)
  else
   iohelp.write(opts.work..'/configs.bin',ffi.string(configs,ffi.sizeof(configs)))
   iohelp.write(opts.work..'/params.bin',ffi.string(params,ffi.sizeof(params)))
   local cmd={probe,spv,opts.work..'/configs.bin',opts.work..'/params.bin',opts.work..'/reports.bin',words,n}
   for k,v in ipairs(cmd) do cmd[k]=iohelp.quote(v) end
   iohelp.run(table.concat(cmd,' ')..' > '..iohelp.quote(opts.work..'/dispatch.json'))
   bytes=iohelp.read(opts.work..'/reports.bin')
  end
  assert(#bytes==n*words*4,'Incomplete regret dispatch')
  local data=ffi.new('uint32_t[?]',n*words);ffi.copy(data,bytes,#bytes)
  for lane=0,n-1 do
   assert(tonumber(data[lane*words+1])%65536==1,'Incomplete regret fight')
   if words~=16 then assert(data[lane*words+25]==scenario.duration*1000000,'Rollout did not reach fight end') end
  end
  return data,bytes
 end
 local captured,raw=dispatch({{}},4294967295,nil,regret.words,shader)
 local _,reference=dispatch({{}},4294967295,nil,16,baselineShader)
 assert(raw:sub(1,64)==reference,'Regret instrumentation changed the baseline fight')
 local function float(w) local a=ffi.new('uint32_t[1]',w);local v=tonumber(ffi.cast('float*',a)[0]);assert(v==v and math.abs(v)~=math.huge,'Nonfinite diagnostic value');return v end
 local function accumulate(totals,data,base,n)
  for key,field in pairs(layout.fields.detailed) do
   local value=tonumber(data[base+32+field.word])
   if field.type=='f32' then value=float(value)
   else
    if field.part=='low' then value=value%65536 elseif field.part=='high' then value=math.floor(value/65536) end
    if field.type=='fraction' then value=value/65535 end
   end
   totals[key]=(totals[key] or 0)+value/n
  end
 end
 local function detail(totals,mean)
  return require('tools.detailed_analysis').build({{totals=totals,sampleCount=1,meanDps=mean}},
   {scenarios={scenario}},{candidates={{}}}).candidates[1]
 end
 local totalDecisions=tonumber(captured[16]);local stored=math.min(totalDecisions,regret.capacity)
 if traceRequest then
  local trace=require('tools.regret_trace')
  local target=traceRequest.decision
  assert(target<stored and math.abs(float(captured[0])/scenario.duration-traceRequest.baselineDps)<1e-6,'Saved baseline no longer reproduces')
  local base=32+target*5
  assert(math.abs(tonumber(captured[base])/1e6-traceRequest.time)<1e-6 and math.abs(float(captured[base+1])-traceRequest.mana)<0.001,'Saved decision no longer reproduces')
  local function plan(value)
   if type(value)=='string' then
    local out={}
    for word in value:gsub(' → ',' -> '):gmatch('[^>]+') do
     word=word:gsub('%s*%-%s*$',''):match('^%s*(.-)%s*$')
     if word~='APL' then
      local found;for id,name in pairs(regret.names) do if word==name then found=id;break end end
      assert(found,'Unknown saved plan action: '..word);out[#out+1]=found
     end
    end
    value=out
   end
   assert(type(value)=='table' and #value>=1 and #value<=regret.depth,'Invalid trace plan')
   for _,v in ipairs(value) do assert(type(v)=='number' and regret.names[v] and v>0,'Invalid trace action') end
   return value
  end
  local plans={plan(traceRequest.takenPlan),plan(traceRequest.alternativePlan)}
  assert(plans[1][1]==regret.canonical(tonumber(captured[base+3])),'Taken trace plan mismatch')
  local traceShader=compile(trace.instrument(source))
  local n=64
  local inputs,ids={},{}
  for sample=1,n do for side=1,2 do inputs[#inputs+1]=plans[side];ids[#ids+1]=2000000+sample end end
  print('Regret analysis: tracing selected pair across '..n..' fresh futures');io.stdout:flush()
  server:close();openServer(shader,regret.words,#inputs)
  local referenceData=dispatch(inputs,target,0,regret.words,shader,ids)
  server:close();openServer(traceShader,trace.words,#inputs)
  local data=dispatch(inputs,target,0,trace.words,traceShader,ids)
  local pairs={}
  for sample=1,n do
   local pair={sample=2000000+sample}
   for side,name in ipairs({'taken','alternative'}) do
    local lane=(sample-1)*2+side-1;local p=lane*trace.words;local ref=lane*regret.words
    for w=0,25 do assert(data[p+w]==referenceData[ref+w],'Trace instrumentation changed rollout results') end
    assert(data[p+17]==0 and data[p+18]==1 and data[p+20]==#plans[side],'Trace plan is not legal in every future')
    for w=0,2 do assert(data[p+21+w]==captured[base+w],'Trace root changed') end
    pair[name]=trace.decode(data,p,float,target,scenario.duration,scenario.packed)
   end
   pairs[#pairs+1]=pair
  end
  -- Detailed totals use the same futures as the action trace, with exact fast-report parity.
  server:close();openServer(compactShader,detailWords,#inputs)
  local detailed=dispatch(inputs,target,0,detailWords,compactShader,ids)
  local details={}
  for side,name in ipairs({'taken','alternative'}) do
   local totals,mean={},0
   for sample=1,n do
    local lane=(sample-1)*2+side-1;local p=lane*detailWords;local ref=lane*regret.words
    for w=0,25 do assert(detailed[p+w]==referenceData[ref+w],'Detailed trace changed rollout results') end
    accumulate(totals,detailed,p,n);mean=mean+float(detailed[p])/scenario.duration/n
   end
   details[name]=detail(totals,mean)
  end
  local explanation=trace.summarize(pairs,details)
  explanation.decision=target;explanation.alternativePlan=traceRequest.alternativePlan;explanation.takenPlan=traceRequest.takenPlan
  explanation.alternativeAction=regret.names[plans[2][1]];explanation.takenAction=regret.names[plans[1][1]]
  explanation.instrumentationVerified=true
  explanation.scope='Observed decision gates and timing, not causal attribution. Fresh paired seeds; random draws can shift between branches. Decision gates cover the continuation; damage totals include the shared prefix.'
  local result={schemaVersion=1,status='complete',mode='regret',candidateId=scenario.id,suiteHash=iohelp.hash(suiteSource),seed=suite.seed,fightIndex=0,
   reportReplay='bit-identical',terminalVerified=true,baselineDps=float(captured[0])/scenario.duration,duration=scenario.duration,analyzedDecisions=0,rows={},trace=explanation}
  server:close()
  iohelp.write(opts.output..'.tmp',json.encode(result):gsub('"rows":{}','"rows":[]'):gsub('"drivers":{}','"drivers":[]')..'\n');assert(os.rename(opts.output..'.tmp',opts.output))
  print('Saved '..opts.output);return
 end
 server:close();openServer(compactShader,detailWords,opts.depth==1 and opts.batchSize or math.max(opts.batchSize,opts.samples,opts.validation))
 local function evaluate(target,plan,samples,sampleOffset)
  local plans={};for k=1,samples do plans[k]=plan end
  local data=dispatch(plans,target,sampleOffset,detailWords,compactShader)
  local totals={}
  local values,mean,mask,valid,doomDamage={},0,0x7fffffff,true,0
  for lane=0,samples-1 do
   local base=lane*detailWords
   accumulate(totals,data,base,samples)
   -- Every sampled future must fork from exactly the captured root context.
   local at=base+21;local original=32+target*5
   for k=0,2 do assert(data[at+k]==captured[original+k],'Counterfactual root state changed') end
   valid=valid and data[base+17]==0 and data[base+18]==1 and data[base+20]==#plan
   mask=bit.band(mask,tonumber(data[base+19]))
   doomDamage=doomDamage+float(data[base+24])/samples
   local v=float(data[base])/scenario.duration;values[#values+1]=v;mean=mean+v/samples
  end
  return {valid=valid,mean=mean,mask=mask,values=values,doomDamage=doomDamage,totals=totals}
 end
 -- Spread a bounded set of roots over the full fight; expose coverage explicitly.
 local selected={};local eligible={}
 for ordinal=0,stored-1 do
  local base=32+ordinal*5
  local taken=regret.canonical(tonumber(captured[base+3]));local mask=tonumber(captured[base+4])
  if bit.band(mask,bit.lshift(1,taken))~=0 and #regret.actions(mask)>1 then eligible[#eligible+1]=ordinal end
 end
 local roots=math.min(opts.decisions,#eligible)
 for k=1,roots do selected[k]=eligible[1+math.floor((k-1)*#eligible/roots)] end
 local sweep={}
 if opts.depth==1 then
  local plans,targets,sampleIds,owners={},{},{},{}
  local function flush()
   if #plans==0 then return end
   local data=dispatch(plans,targets,0,detailWords,compactShader,sampleIds)
   for lane,owner in ipairs(owners) do
    local base=(lane-1)*detailWords;local ordinal,action,sample=unpack(owner)
    assert(data[base+17]==0 and data[base+18]==1 and data[base+20]==1,'Invalid one-action continuation')
    local original=32+ordinal*5
    for k=0,2 do assert(data[base+21+k]==captured[original+k],'Batched counterfactual root changed') end
    local value=sweep[ordinal][action];accumulate(value.totals,data,base,opts.validation);local dps=float(data[base])/scenario.duration
    value.values[sample]=dps;value.mean=value.mean+dps/opts.validation
    value.doomDamage=value.doomDamage+float(data[base+24])/opts.validation
   end
   plans,targets,sampleIds,owners={},{},{},{}
  end
  for index,ordinal in ipairs(selected) do
   print(('Regret analysis: batching decision %d/%d (fight decision %d)'):format(index,roots,ordinal));io.stdout:flush()
   sweep[ordinal]={}
   for _,action in ipairs(regret.actions(tonumber(captured[32+ordinal*5+4]))) do
    sweep[ordinal][action]={plan={action},action=action,mean=0,values={},doomDamage=0,totals={}}
    for sample=1,opts.validation do
     plans[#plans+1]={action};targets[#targets+1]=ordinal;sampleIds[#sampleIds+1]=1000000+sample
     owners[#owners+1]={ordinal,action,sample}
     if #plans==capacity then flush() end
    end
   end
  end
  flush()
 end
 local rows={}
 for index,ordinal in ipairs(selected) do
  print(('Regret analysis: decision %d/%d (fight decision %d)'):format(index,roots,ordinal));io.stdout:flush()
  local _,replayed=dispatch({{}},ordinal,nil,detailWords,compactShader)
  assert(replayed:sub(1,64)==reference,'Decision prefix replay changed the baseline report')
  local base=32+ordinal*5;local taken=regret.canonical(tonumber(captured[base+3]));local mask=tonumber(captured[base+4])
  local validation={}
  if opts.depth==1 then
   for _,a in ipairs(regret.actions(mask)) do validation[#validation+1]=sweep[ordinal][a] end
  else
   local best=regret.search(function(plan,n,offset) return evaluate(ordinal,plan,n,offset) end,mask,
    {rounds=opts.rounds,depth=opts.depth,samples=opts.samples,exploration=100})
   for _,a in ipairs(regret.actions(mask)) do if not best[a] then best[a]={plan={a}} end end
   for _,a in ipairs(regret.actions(mask)) do
    local plan=best[a].plan
    local value=evaluate(ordinal,plan,opts.validation,1000000)
    if not value.valid then plan={a};value=evaluate(ordinal,plan,opts.validation,1000000) end
    assert(value.valid,'Root action failed held-out legality');value.plan=plan;value.action=a;validation[#validation+1]=value
   end
  end
  table.sort(validation,function(a,b) if a.mean==b.mean then return a.action<b.action end;return a.mean>b.mean end)
  local actual
  for _,v in ipairs(validation) do if v.action==taken then actual=v end end
  assert(actual,'Missing taken action comparison')
  local winner=validation[1];local delta=winner.mean-actual.mean
  local mean,m2=0,0
  for k=1,opts.validation do local x=winner.values[k]-actual.values[k];local d=x-mean;mean=mean+d/k;m2=m2+d*(x-mean) end
  local se=math.sqrt(m2/(opts.validation-1)/opts.validation)
  local alternatives={}
  for _,v in ipairs(validation) do alternatives[#alternatives+1]={action=regret.names[v.action],meanDps=v.mean,doomDamage=v.doomDamage,detail=detail(v.totals,v.mean),plan=regret.planNames(v.plan),actionIds=v.plan} end
  rows[#rows+1]={decision=ordinal,time=tonumber(captured[base])/1000000,mana=float(captured[base+1]),shadowTrance=captured[base+2]~=0,
   takenAction=regret.names[taken],bestAction=regret.names[winner.action],dpsDelta=delta,standardError=se,ciLow=delta-1.96*se,ciHigh=delta+1.96*se,
   samples=opts.validation,plan=regret.planNames(winner.plan),takenPlan=regret.planNames(actual.plan),alternatives=alternatives}
 end
 table.sort(rows,function(a,b) if a.dpsDelta==b.dpsDelta then return a.time<b.time end;return a.dpsDelta>b.dpsDelta end)
 local actions={}
 for ordinal=0,stored-1 do
  local base=32+ordinal*5
  local action=tonumber(captured[base+3])
  actions[#actions+1]={decision=ordinal,time=tonumber(captured[base])/1000000,
   actionId=action,action=regret.names[action] or ('Action '..action),
   mana=float(captured[base+1]),shadowTrance=captured[base+2]~=0}
 end
 local result={schemaVersion=1,status='complete',mode='regret',algorithm=opts.depth==1 and 'Batched one-action alternatives / APL to terminal' or 'GPU rollouts / CPU open-loop UCT',candidateId=scenario.id,
  suiteSource=suiteSource,suiteHash=iohelp.hash(suiteSource),seed=suite.seed,fightIndex=0,duration=scenario.duration,baselineDps=float(captured[0])/scenario.duration,
  reportReplay='bit-identical',terminalVerified=true,rolloutFights=rolloutFights,rolloutDispatches=rolloutDispatches,totalDecisions=totalDecisions,storedDecisions=stored,eligibleDecisions=#eligible,analyzedDecisions=#rows,
  actions=actions,truncated=totalDecisions>stored,settings={depth=opts.depth,rounds=opts.rounds,searchSamples=opts.samples,validationSamples=opts.validation},
  sourceHash=iohelp.hashFile('tools/regret.lua'),kernelHash=iohelp.hash(source),detailKernelHash=iohelp.hashFile('sim/classes/warlock/generated/detailed.glsl'),rows=rows,
  continuation=opts.depth==1 and 'Current APL to terminal after one forced action' or 'Searched action prefix then current APL to terminal',
  interpretation='Best found plans conditioned on each root action; paired fresh future RNG samples, full-fight DPS. Open-loop plans fall back to APL after the prefix. Intervals are approximate and do not correct for selecting among alternatives. Regrets are not additive.'}
 server:close()
 local encoded=json.encode(result):gsub('"rows":{}','"rows":[]')
 iohelp.write(opts.output..'.tmp',encoded..'\n');assert(os.rename(opts.output..'.tmp',opts.output),'Cannot save regret analysis')
 print('Saved '..opts.output)
end
local ok,err=xpcall(execute,debug.traceback)
if not ok then
 local log=opts.work..'/compile.log';if iohelp.exists(log) then io.stderr:write(iohelp.read(log)) end
end
-- The parent owns this work directory and removes it on success/failure/cancellation.
if not ok then error(err) end
