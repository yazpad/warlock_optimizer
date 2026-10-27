#!/usr/bin/env luajit
-- Complete-fight MCTS controller evaluation. Never loaded by the production fast runner.
local ffi = require('ffi')
ffi.cdef[[int setenv(const char*,const char*,int);int unsetenv(const char*);
struct timespec { long tv_sec; long tv_nsec; }; int clock_gettime(int, struct timespec*);]]
local bit = require('bit')
local iohelp = require('tools.native_support')
local json = require('sim.common.json')
local decode = require('sim.common.json_input').decode
local shader = require('tools.policy_shader')
local controller = require('tools.policy_controller')
local neuro = require('tools.neuroevolution')
local ppo = require('tools.ppo')
local neuralShader = require('tools.neuro_shader')
local regret = require('tools.regret')
local transport = require('tools.regret_transport')
local function now()
  local ts = ffi.new('struct timespec'); assert(ffi.C.clock_gettime(1, ts) == 0)
  return tonumber(ts.tv_sec) + tonumber(ts.tv_nsec) * 1e-9
end
local started = now()
local opts = {candidate=0, fights=8, rounds=8, depth=3, batchSize=16, samples=8,
  cPuct=2, aplWeight=0.7, maxDecisions=shader.capacity, evaluationStart=0,
  algorithm='mcts', generations=1024, population=32, mutationSigma=0.1, evolutionSeed=42, work='/tmp/fastsim_mcts_work', output='artifacts/mcts_benchmark.json'}
local flags = {['--algorithm']='algorithm', ['--generations']='generations', ['--population']='population', ['--mutation-sigma']='mutationSigma', ['--ppo-lr']='ppoLr', ['--evolution-seed']='evolutionSeed', ['--candidate']='candidate', ['--fights']='fights', ['--rounds']='rounds',
  ['--depth']='depth', ['--mcts-batch-size']='batchSize', ['--samples']='samples',
  ['--c-puct']='cPuct', ['--apl-weight']='aplWeight', ['--evaluation-start']='evaluationStart',
  ['--work-dir']='work', ['--output']='output', ['--suite']='suite'}
local ignoredValues = {['--total-fights']=true, ['--batch-size']=true, ['--warmup-batches']=true}
local ignoredFlags = {['--generic']=true, ['--no-subgroup-size']=true, ['--no-histogram']=true}
local i = 1
while i <= #arg do
  local name = arg[i]
  if flags[name] then i=i+1; opts[flags[name]]=assert(arg[i], 'Missing '..name)
  elseif ignoredValues[name] then i=i+1; assert(arg[i], 'Missing '..name)
  elseif name=='--verify-neural' then opts.verifyNeural=true
  elseif name=='--allow-software' then opts.software=true
  elseif not ignoredFlags[name] then error('Unknown argument: '..name) end
  i=i+1
end
if opts.algorithm=='neuroevolution' or opts.algorithm=='ppo' then
  local explicitFights,explicitGen,explicitPop,explicitSigma=false,false,false,false
  for _,v in ipairs(arg) do
    if v=='--fights' then explicitFights=true end
    if v=='--generations' then explicitGen=true end
    if v=='--population' then explicitPop=true end
    if v=='--mutation-sigma' or v=='--ppo-lr' then explicitSigma=true end
  end
  if not explicitFights then opts.fights=2048 end
  if opts.algorithm=='ppo' then
    if not explicitGen then opts.generations=128 end
    if not explicitPop then opts.population=64 end
    if not explicitSigma then opts.mutationSigma=0.003 end
  end
end
if opts.ppoLr then opts.mutationSigma=opts.ppoLr end
local function bounded(key, lo, hi)
  local value = tonumber(opts[key])
  assert(value and value%1==0 and value>=lo and value<=hi, 'Invalid '..key); opts[key]=value
end
assert(opts.algorithm=='mcts' or opts.algorithm=='neuroevolution' or opts.algorithm=='ppo','Invalid algorithm')
local neuralMode=opts.algorithm=='neuroevolution' or opts.algorithm=='ppo'
bounded('generations',1,16384); bounded('population',2,64); bounded('evolutionSeed',1,2147483646)
opts.mutationSigma=tonumber(opts.mutationSigma);assert(opts.mutationSigma and opts.mutationSigma>0 and opts.mutationSigma<=2,'Invalid mutation sigma')
bounded('candidate',0,65535); bounded('fights',2,neuralMode and 8192 or 64); bounded('rounds',1,256)
bounded('depth',1,shader.depth); bounded('batchSize',1,64); bounded('samples',2,128)
opts.validationSamples=math.max(32,opts.samples)
bounded('evaluationStart',0,65535)
assert(opts.evaluationStart+opts.fights<=65536,'Evaluation range overlaps training seeds')
opts.cPuct=tonumber(opts.cPuct); opts.aplWeight=tonumber(opts.aplWeight)
assert(opts.cPuct and opts.cPuct>=0 and opts.cPuct<=100, 'Invalid cPuct')
assert(opts.aplWeight and opts.aplWeight>=0 and opts.aplWeight<=1, 'Invalid aplWeight')
local suiteSource=iohelp.read(opts.suite or 'data/validation/forever_baseline.json')
local suite=opts.suite and decode(suiteSource) or require('data.validation.forever_baseline')
local scenario=assert(suite.scenarios[opts.candidate+1], 'Candidate index out of range')
assert(#scenario.packed==229 and scenario.duration>=1 and scenario.duration<=1800, 'Invalid candidate')
assert(suite.seed>=0 and suite.seed+opts.evaluationStart<4294967296, 'Invalid suite seed')
for path, hash in pairs(suite.kernelHashes) do assert(iohelp.hashFile(path)==hash, 'Kernel identity mismatch: '..path) end
iohelp.run('mkdir -p artifacts/.cache')
iohelp.run('mkdir '..iohelp.quote(opts.work))
local source=iohelp.read('sim/classes/warlock/generated/fast.glsl')
local function compile(text)
  local wrapped=iohelp.wrap(text)
  local path='artifacts/.cache/'..iohelp.hash(wrapped)..'.spv'
  if not iohelp.exists(path) then
    iohelp.write(opts.work..'/shader.comp', wrapped)
    iohelp.run('glslangValidator -V '..iohelp.quote(opts.work..'/shader.comp')..' -o '..iohelp.quote(opts.work..'/shader.spv')..' > '..iohelp.quote(opts.work..'/compile.log')..' 2>&1')
    iohelp.run('spirv-val '..iohelp.quote(opts.work..'/shader.spv'))
    iohelp.run('mv '..iohelp.quote(opts.work..'/shader.spv')..' '..iohelp.quote(path))
  end
  return path
end
opts.trainingFights=neuralMode and neuro.trainingFights or 0
local reportWords=neuralMode and neuralShader.words or shader.words
local policyShader=compile(neuralMode and neuralShader.instrument(source) or shader.instrument(source))
local baselineShader=compile(source)
ffi.C.setenv('FASTSIM_WARMUP_BATCHES','0',1); ffi.C.unsetenv('FASTSIM_SUBGROUP_SIZE')
ffi.C.unsetenv('FASTSIM_PIPELINE_STATS_PATH'); ffi.C.setenv('FASTSIM_DPS_HISTOGRAM','0',1)
if opts.software then ffi.C.unsetenv('FASTSIM_REQUIRE_GPU') else ffi.C.setenv('FASTSIM_REQUIRE_GPU','1',1) end
opts.duration=scenario.duration
local budget=scenario.duration*32+1024
local function float(word)
  local v=tonumber(ffi.cast('float*',ffi.new('uint32_t[1]',word))[0])
  assert(v==v and math.abs(v)~=math.huge, 'Nonfinite policy value'); return v
end
-- Compare every APL baseline report with the unchanged production shader.
local probeKey=iohelp.hash(iohelp.read('tools/vulkan_probe.c')..iohelp.read('tools/dps_histogram.h')..'cc -O2 -Wall -Wextra -Werror -lvulkan')
local probe='artifacts/.cache/probe-'..probeKey
if not iohelp.exists(probe) then
  iohelp.run('cc -O2 -Wall -Wextra -Werror tools/vulkan_probe.c -lvulkan -o '..iohelp.quote(opts.work..'/probe'))
  iohelp.run('mv '..iohelp.quote(opts.work..'/probe')..' '..iohelp.quote(probe))
end
local referenceConfigs=ffi.new('uint32_t[229]',scenario.packed)
local referenceParams=ffi.new('uint32_t[6]',{1,opts.fights,suite.seed+opts.evaluationStart,0,opts.fights,budget})
iohelp.write(opts.work..'/reference-config.bin',ffi.string(referenceConfigs,ffi.sizeof(referenceConfigs)))
iohelp.write(opts.work..'/reference-params.bin',ffi.string(referenceParams,ffi.sizeof(referenceParams)))
local command={probe,baselineShader,opts.work..'/reference-config.bin',opts.work..'/reference-params.bin',opts.work..'/reference-report.bin',16,opts.fights}
for k,v in ipairs(command) do command[k]=iohelp.quote(v) end
iohelp.run(table.concat(command,' ')..' > '..iohelp.quote(opts.work..'/reference.json'))
local references=iohelp.read(opts.work..'/reference-report.bin')
assert(#references==opts.fights*64,'Incomplete production reference reports')
local capacity=neuralMode and 32768 or math.max(opts.batchSize*opts.samples,52*opts.validationSamples)
local activeStride=neuralMode and neuralShader.stride or shader.stride
local weightOffset=229+capacity*activeStride
local maxModels=opts.algorithm=='ppo' and 8 or math.max(opts.population,2)
local configWords=weightOffset+(neuralMode and maxModels*neuro.parameterCount or 0)
local initialConfigs=ffi.new('uint32_t[?]',configWords,scenario.packed)
local params=ffi.new('uint32_t[6]',{1,capacity,suite.seed,0,capacity,budget})
local worker=transport.open(transport.build(opts.work,neuralMode),policyShader,opts.work,neuralMode and 32 or reportWords,capacity,
  ffi.string(initialConfigs,ffi.sizeof(initialConfigs)),ffi.string(params,ffi.sizeof(params)),neuralMode)
local dispatches, searchFights, replayFights, searchMs=0,0,0,0
local neuralFights,neuralDispatches,neuralWallMs,neuralGpuMs=0,0,0,0
local function dispatch(history, liveSeed, plans, samples, forkOffset)
  assert(#history<=shader.capacity,'History exceeds policy capacity')
  local n=#plans*samples; assert(n>0 and n<=capacity,'Policy batch exceeds capacity')
  local config=ffi.new('uint32_t[?]',configWords,scenario.packed)
  for p,plan in ipairs(plans) do
    assert(#plan<=shader.depth,'Search prefix exceeds capacity')
    for sample=0,samples-1 do
      local lane=(p-1)*samples+sample
      local base=229+lane*activeStride
      config[base]=#history
      config[base+1]=forkOffset and forkOffset+sample or 4294967295
      config[base+2]=#plan
      for j,action in ipairs(plan) do config[base+2+j]=action end
      config[base+6]=reportWords
      config[base+7]=liveSeed
      for j,action in ipairs(history) do config[base+shader.historyOffset+j-1]=action end
    end
  end
  local startedDispatch=now()
  local header=ffi.new('uint32_t[1]',n)
  local bytes=worker:dispatch(ffi.string(header,4),ffi.string(config,ffi.sizeof(config)),n,neuralMode and reportWords or nil)
  dispatches=dispatches+1
  if forkOffset then searchFights=searchFights+n; searchMs=searchMs+(now()-startedDispatch)*1000
  else replayFights=replayFights+n end
  local data=ffi.new('uint32_t[?]',n*reportWords); ffi.copy(data,bytes,#bytes)
  for lane=0,n-1 do
    local base=lane*reportWords
    assert(tonumber(data[base+1])%65536==1 and data[base+25]==scenario.duration*1000000,
      'Policy rollout did not reach a valid fight end')
    assert(data[base+26]==#history,'Committed history was not replayed completely')
  end
  return data,bytes
end
local engine={}
function engine:observe(history,liveSeed)
  local data=dispatch(history,liveSeed,{{}},1,nil)
  local terminal=data[18]==0
  return {valid=data[17]==0, terminal=terminal, decisions=tonumber(data[16]),
    -- Future APL continuation returns are withheld from the controller until terminal.
    dps=terminal and float(data[0])/scenario.duration or nil,
    baselineDps=#history==0 and float(data[0])/scenario.duration or nil,
    mask=tonumber(data[29]), aplAction=tonumber(data[30]), time=tonumber(data[21])*1e-6,
    mana=float(data[22]), totalDamage=float(data[31]),
    signature={tonumber(data[27]),tonumber(data[28])}}
end
function engine:evaluate(history,liveSeed,state,plans,samples,offset)
  local data=dispatch(history,liveSeed,plans,samples,offset)
  local results={}
  for p,plan in ipairs(plans) do
    local mean,mask,valid=0,0x7fffffff,true
    local values={}
    for sample=0,samples-1 do
      local base=((p-1)*samples+sample)*reportWords
      assert(data[base+27]==state.signature[1] and data[base+28]==state.signature[2],
        'Search did not fork from the controller current state')
      valid=valid and data[base+17]==0 and data[base+18]==1 and data[base+20]==#plan
      values[sample+1]=float(data[base])/scenario.duration
      mean=mean+values[sample+1]/samples
      mask=bit.band(mask,tonumber(data[base+19]))
    end
    results[p]={valid=valid,mean=mean,mask=mask,values=values}
  end
  return results
end
-- Whole population x common seeds in a single dispatch, with inference on device.
if neuralMode then
  function engine:neuralBatch(models,seeds,capture,stochastic)
    local total=#models*#seeds
    assert(total>0,'Empty neural batch')
    local width=capture and reportWords or 32
    local results,templates={},{}
    local networkWords=ffi.new('uint32_t[?]',math.max(#models,1)*neuro.parameterCount)
    local networkValues=ffi.cast('float*',networkWords)
    for member,weights in ipairs(models) do
      results[member]={mean=0,fights={}}
      local template=ffi.new('uint32_t[?]',activeStride)
      template[0]=weights and 4294967294 or 4294967295
      template[4]=weightOffset+(member-1)*neuro.parameterCount
      local invTempBits=0
      if stochastic then
        local temp=type(stochastic)=='number' and stochastic or 1.0
        invTempBits=ffi.cast('uint32_t*',ffi.new('float[1]',temp))[0]
      end
      template[1]=4294967295;template[2]=capture and 1 or 0;template[3]=invTempBits;template[6]=width
      if weights then
        assert(#weights==neuro.parameterCount,'Invalid network size')
        for j,w in ipairs(weights) do networkValues[(member-1)*neuro.parameterCount+j-1]=w end
      end
      templates[member]=template
    end
    local batchCapacity=capture and math.max(1,math.floor(capacity*32/width)) or capacity
    for first=0,total-1,batchCapacity do
      local n=math.min(batchCapacity,total-first)
      local config=ffi.new('uint32_t[?]',configWords,scenario.packed)
      ffi.copy(config+weightOffset,networkWords,#models*neuro.parameterCount*4)
      for lane=0,n-1 do
        local index=first+lane
        local member=math.floor(index/#seeds)+1
        local sample=index%#seeds+1
        local base=229+lane*activeStride
        ffi.copy(config+base,templates[member],activeStride*4)
        config[base+7]=seeds[sample]
      end
      local began=now()
      local header=ffi.new('uint32_t[1]',n)
      local bytes,gpuMs=worker:dispatch(ffi.string(header,4),ffi.string(config,ffi.sizeof(config)),n,width)
      neuralGpuMs=neuralGpuMs+(gpuMs or 0)
      neuralWallMs=neuralWallMs+(now()-began)*1000
      dispatches=dispatches+1;neuralDispatches=neuralDispatches+1;neuralFights=neuralFights+n
      local data=ffi.new('uint32_t[?]',n*width);ffi.copy(data,bytes,#bytes)
      for lane=0,n-1 do
        local member=math.floor((first+lane)/#seeds)+1
        local sample=(first+lane)%#seeds+1
        local base=lane*width
        assert(tonumber(data[base+1])%65536==1 and data[base+25]==scenario.duration*1000000 and data[base+17]==0,
          'Neural policy did not reach a valid complete fight')
        local fight={dps=float(data[base])/scenario.duration,decisions=tonumber(data[base+16]),searches=0,timeline={},
          report=bytes:sub(lane*width*4+1,lane*width*4+64)}
        assert(fight.decisions<=shader.capacity,'Neural decision capacity exceeded')
        if capture then
          for ordinal=0,fight.decisions-1 do
            local at=base+32+ordinal*neuralShader.traceWords
            local action,apl=tonumber(data[at+3]),tonumber(data[at+4])
            local features={};for j=1,#neuro.features do features[j]=float(data[at+7+j]) end
            fight.timeline[#fight.timeline+1]={ordinal=ordinal,time=tonumber(data[at])*1e-6,
              features=features,mana=float(data[at+1]),totalDamage=float(data[at+2]),action=action,actionName=regret.names[action],
              aplAction=apl,aplActionName=regret.names[apl],mask=tonumber(data[at+7]),
              signature={tonumber(data[at+5]),tonumber(data[at+6])}}
          end
        end
        results[member].fights[sample]=fight
        results[member].mean=results[member].mean+fight.dps/#seeds
      end
    end
    return results
  end
end
function engine:progress(decisions,state)
  if decisions%10==0 then
    print(('Policy evaluation: %d committed decisions, current fight time %.2fs'):format(decisions,state.time))
    io.stdout:flush()
  end
end
local neuralStartupWallMs=0
if neuralMode then
  print('Policy evaluation: preparing neural GPU kernel and original APL threshold');io.stdout:flush()
  local warmupStarted=now()
  local baselineBatch=engine:neuralBatch({false},neuro.trainingSeeds())
  engine.trainingBaseline=baselineBatch[1].mean
  engine.trainingSeedDps={}
  for sIdx,fight in ipairs(baselineBatch[1].fights) do
    engine.trainingSeedDps[neuro.trainingSeeds()[sIdx]]=fight.dps
  end
  neuralStartupWallMs=(now()-warmupStarted)*1000
end
local measuredNeuralWallStart,measuredNeuralFightStart,measuredNeuralGpuStart=neuralWallMs,neuralFights,neuralGpuMs
local setupMs=(now()-started)*1000
local evaluationStarted=now()
local rows,timeline={},nil
local weights,trainingCurve,trainingSeeds
local trainingWallMs=0
local ok,result=xpcall(function()
  if opts.verifyNeural then
    assert(neuralMode,'Neural verification needs neuroevolution')
    local seeds={70000,70001}
    local zero={};for i=1,neuro.parameterCount do zero[i]=0 end
    local reference=engine:neuralBatch({false},seeds,true)[1]
    local identity=engine:neuralBatch({zero},seeds,true)[1]
    local initialized=engine:neuralBatch({neuro.initialize(neuro.random(42))},seeds,true)[1]
    for sample in ipairs(seeds) do
      assert(identity.fights[sample].report==reference.fights[sample].report,'Zero MLP changed APL report')
      assert(initialized.fights[sample].report==reference.fights[sample].report,'Initialized MLP changed APL report')
    end
    local models={neuro.mutate(zero,neuro.random(13),0.15),neuro.mutate(zero,neuro.random(17),0.5)}
    local checked=engine:neuralBatch(models,seeds,true)
    local decisions=0
    for member,result in ipairs(checked) do
      for _,fight in ipairs(result.fights) do
        for _,state in ipairs(fight.timeline) do
          for _,v in ipairs(state.features) do assert(v>=0 and v<=1,'Combat feature outside normalized range') end
          assert(neuro.select(models[member],state,opts.duration)==state.action,'GPU MLP disagrees with Lua reference')
          assert(bit.band(state.mask,bit.lshift(1,state.action))~=0,'GPU MLP chose illegal action')
          decisions=decisions+1
        end
      end
    end
    print(('Policy evaluation: verified GPU/Lua MLP parity on %d nonzero-network decisions and exact zero-network APL reports'):format(decisions));io.stdout:flush()
  end
  if opts.algorithm=='neuroevolution' or opts.algorithm=='ppo' then
    local trainer=opts.algorithm=='ppo' and ppo or neuro
    local trainingStarted=now()
    weights,trainingCurve,trainingSeeds=trainer.train(engine,opts,function(point)
      point.elapsedTrainingMs=(now()-trainingStarted)*1000
      print('Neuro training: '..json.encode(point));io.stdout:flush()
    end)
    trainingWallMs=(now()-trainingStarted)*1000
  end
  local neuralEvaluation,neuralBaseline
  if weights then
    local seeds={};for pair=1,opts.fights do seeds[pair]=opts.evaluationStart+pair-1 end
    neuralEvaluation=engine:neuralBatch({weights},seeds,false)[1].fights
    neuralBaseline=engine:neuralBatch({false},seeds,false)[1].fights
    -- Only the displayed first fight needs a full trace; all others use compact reports.
    local traced=engine:neuralBatch({weights},{seeds[1]},true)[1].fights[1]
    assert(traced.report==neuralEvaluation[1].report,'Trace capture changed neural fight')
    neuralEvaluation[1]=traced
  end
  for pair=1,opts.fights do
    local liveSeed=opts.evaluationStart+pair-1
    local baselineDps
    if neuralBaseline then
      assert(neuralBaseline[pair].report==references:sub((pair-1)*64+1,pair*64),
        'Neural adapter changed production APL baseline')
      baselineDps=neuralBaseline[pair].dps
    else
      local baseline,bytes=dispatch({},liveSeed,{{}},1,nil)
      baselineDps=float(baseline[0])/scenario.duration
      assert(baseline[17]==0 and bytes:sub(1,64)==references:sub((pair-1)*64+1,pair*64),
      'Policy adapter changed the production APL baseline')
    end
    if not weights or pair==1 or pair%128==0 or pair==opts.fights then print(('Policy evaluation: fight pair %d/%d, seed index %d'):format(pair,opts.fights,liveSeed)); io.stdout:flush() end
    local fight=neuralEvaluation and neuralEvaluation[pair] or controller.fight(engine,liveSeed,opts)
    rows[pair]={fightIndex=liveSeed,baselineDps=baselineDps,
      policyDps=fight.dps,decisions=fight.decisions,searches=fight.searches,terminalVerified=true}
    if pair==1 then timeline=fight.timeline end
    if not weights or pair==1 or pair%128==0 or pair==opts.fights then print(('Policy evaluation: completed pair %d/%d, %d own-trajectory decisions, delta %+.2f DPS'):format(
      pair,opts.fights,fight.decisions,rows[pair].policyDps-rows[pair].baselineDps)); io.stdout:flush() end
  end
  return controller.summarize(rows)
end,debug.traceback)
worker:close(); assert(ok,result)
result.schemaVersion=2; result.status='complete'; result.mode='policy_benchmark'
result.evaluationScope='complete_fights'; result.policyType='receding_horizon_mcts'
if weights then
  result.policyType=opts.algorithm=='ppo' and 'ppo_mlp' or 'neuroevolution_mlp'
  result.trainingCurve=trainingCurve;result.trainingSeeds=trainingSeeds
  result.inferenceBackend='batched_gpu_full_fights'
  result.network={inputs=neuro.inputs,hidden=neuro.hidden,outputs=neuro.outputs,weights=weights,version=neuro.version,features=neuro.features,aplPrior=neuro.aplPrior,precision='float32'}
end
result.actionSelection=weights and 'masked_mlp_argmax' or 'fresh_paired_rollout_validation';result.validationGuardStandardErrors=weights and nil or 4
result.optimalityGuaranteed=false; result.terminalVerified=true
result.reportReplay='bit-identical'; result.seed=suite.seed; result.fightIndex=opts.evaluationStart
result.suiteHash=iohelp.hash(suiteSource); result.candidateId=scenario.id; result.duration=scenario.duration
result.algorithm=weights and (opts.algorithm=='ppo' and 'PPO MLP' or 'Neuroevolution MLP') or 'MCTS + PUCT controller'; result.settings=opts; result.results=rows; result.timeline=timeline
result.timing={setupMs=setupMs,evaluationWallMs=(now()-evaluationStarted)*1000,
  totalWallMs=(now()-started)*1000,searchDispatchWallMs=searchMs,searchRolloutFights=searchFights,
  replayFights=replayFights,dispatches=dispatches,trainingWallMs=trainingWallMs,
  trainingPolicyFights=weights and (opts.algorithm=='ppo' and opts.generations*(opts.population+6*neuro.trainingFights) or (1+opts.generations*(opts.population-1))*neuro.trainingFights) or 0,
  neuralFullFights=neuralFights,neuralDispatches=neuralDispatches,neuralDispatchWallMs=neuralWallMs,
  neuralStartupWallMs=neuralStartupWallMs,
  neuralMeasuredWallMs=neuralWallMs-measuredNeuralWallStart,
  neuralMeasuredGpuMs=neuralGpuMs-measuredNeuralGpuStart,
  neuralBatchCapacity=capacity,
  neuralMeasuredFights=neuralFights-measuredNeuralFightStart,
  neuralFightsPerSecond=neuralWallMs>measuredNeuralWallStart and
    (neuralFights-measuredNeuralFightStart)*1000/(neuralWallMs-measuredNeuralWallStart) or 0,
  scope='Includes training when selected and worker round trips; neural policies run full fights on GPU, MCTS uses prefix replay; not core simulator throughput'}
iohelp.write(opts.output,json.encode(result))
print(('Policy evaluation complete: %d complete fight pairs, APL %.2f DPS, controller %.2f DPS, delta %+.2f DPS. Saved to %s.'):format(
  opts.fights,result.meanBaselineDps,result.meanAlgoDps,result.dpsDelta,opts.output))
