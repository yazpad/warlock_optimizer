-- Small recurrent-free MLP, evolved by elitist Gaussian mutation. No rollout search.
local regret=require('tools.regret')
-- Shared ordered feature schema: GLSL computes these before the consuming action;
-- captured normalized values feed the Lua reference, avoiding two definitions.
local M={hidden=12,outputs=26,trainingFights=1000,aplPrior=0.1,version=2}
M.features={
  {'remainingFight','min(1.0,float(c.end-s.now)/30000000.0)'},
  {'corruption','s.corrTicks>0u?remaining(s.corrEnd,18000000.0):0.0'},
  {'agony','s.agonyTicks>0u?remaining(s.agonyEnd,24000000.0):0.0'},
  {'immolate','s.immTicks>0u?remaining(s.immEnd,15000000.0):0.0'},
  {'siphon','s.siphonTicks>0u?remaining(s.siphonEnd,30000000.0):0.0'},
  {'doom','s.doomActive!=0u?remaining(s.doomEnd,60000000.0):0.0'},
  {'conflagrateCooldown','remaining(s.conflagReady,10000000.0)'},
  {'shadowburnCooldown','remaining(s.shadowburnReady,15000000.0)'},
  {'soulFireCooldown','remaining(s.soulFireReady,60000000.0)'},
  {'drainHopeCooldown','remaining(s.drainHopeReady,30000000.0)'},
  {'doomCooldown','remaining(s.doomReady,60000000.0)'},
  {'trance','s.trance!=0u?remaining(s.tranceEnd,10000000.0):0.0'},
  {'shadowVulnerability','s.isbCharges>0u?remaining(s.isbEnd,12000000.0):0.0'},
  {'shadowCharges','min(1.0,float(s.isbCharges)/4.0)'},
  {'decimation','remaining(s.decimationEnd,10000000.0)'},
  {'trinket','remaining(s.trinketEnd,20000000.0)'},
  {'racial','remaining(s.racialEnd,15000000.0)'},
  {'amplify','remaining(s.amplifyEnd,30000000.0)'},
  {'shadowBuff','remaining(s.snfShadowEnd,15000000.0)'},
  {'fireBuff','remaining(s.snfFireEnd,15000000.0)'},
}
M.inputs=55+#M.features
M.parameterCount=(M.inputs+1)*M.hidden+(M.hidden+1)*M.outputs
function M.trainingSeeds()
  local seeds={};for i=1,M.trainingFights do seeds[i]=65535+i end
  return seeds
end
function M.random(seed)
  local s=seed
  return function() s=(s*16807)%2147483647; return s/2147483647 end
end
function M.mutate(parent,random,sigma)
  local child={}
  for i=1,M.parameterCount do
    local normal=math.sqrt(-2*math.log(math.max(1e-12,random())))*math.cos(2*math.pi*random())
    child[i]=(parent and parent[i] or 0)+sigma*normal
  end
  return child
end
function M.initialize(random)
  local weights=M.mutate(nil,random,1/math.sqrt(M.inputs))
  for i=(M.inputs+1)*M.hidden+1,M.parameterCount do weights[i]=0 end
  return weights -- Random features, but exactly the APL until output weights move.
end
function M.propose(parent,random,sigma,member)
  local scale=({0.25,1,4})[(member-1)%3+1]
  local child={};for i,w in ipairs(parent) do child[i]=w end
  local hiddenWords=(M.inputs+1)*M.hidden
  -- Alternate local output-row changes and whole-network changes. Normalize
  -- each layer by fan-in so increasing feature count does not increase disruption.
  local row=math.floor(random()*M.outputs)
  for i=1,M.parameterCount do
    local output=i>hiddenWords
    if member%2==0 or (output and math.floor((i-hiddenWords-1)/(M.hidden+1))==row) then
      local z=math.sqrt(-2*math.log(math.max(1e-12,random())))*math.cos(2*math.pi*random())
      child[i]=child[i]+sigma*scale*z/math.sqrt(output and M.hidden or M.inputs)
    end
  end
  return child
end
function M.select(weights,state,duration)
  local actions=regret.actions(state.mask)
  if #actions==0 then return 0 end
  local x={math.min(1,state.time/duration),math.min(2,state.mana/10000),math.min(2,state.totalDamage/1000000)}
  for a=0,25 do x[#x+1]=math.floor(state.mask/2^a)%2 end
  for a=0,25 do x[#x+1]=state.aplAction==a and 1 or 0 end
  for i=1,#M.features do x[#x+1]=(state.features or {})[i] or 0 end
  local h,k={},1
  for j=1,M.hidden do
    local v=weights[k];k=k+1
    for i=1,M.inputs do v=v+weights[k]*x[i];k=k+1 end
    h[j]=math.tanh(v)
  end
  local scores={}
  for a=0,25 do
    local v=weights[k];k=k+1
    for j=1,M.hidden do v=v+weights[k]*h[j];k=k+1 end
    scores[a]=v+(a==state.aplAction and M.aplPrior or 0)
  end
  local best=actions[1]
  for _,a in ipairs(actions) do if scores[a]>scores[best] then best=a end end
  return best
end
function M.fight(engine,seed,opts,weights)
  local history,timeline={},{}
  while true do
    local state=engine:observe(history,seed)
    assert(state.valid,'Invalid neural trajectory')
    if state.terminal then
      assert(state.decisions==#history,'Incomplete neural trajectory')
      return {dps=state.dps,decisions=#history,searches=0,history=history,timeline=timeline}
    end
    assert(#history<opts.maxDecisions,'Neural decision capacity exceeded')
    local a=M.select(weights,state,opts.duration)
    timeline[#timeline+1]={ordinal=#history,time=state.time,mana=state.mana,totalDamage=state.totalDamage,
      action=a,actionName=regret.names[a],aplAction=state.aplAction,aplActionName=regret.names[state.aplAction],signature=state.signature}
    history[#history+1]=a
  end
end
function M.train(engine,opts,progress)
  local random=M.random(opts.evolutionSeed)
  local retainedModel=M.initialize(random)
  local archive={}
  local seeds=M.trainingSeeds() -- Disjoint from held-out evaluation indices.
  local baseline=0
  if engine.trainingBaseline then
    baseline=engine.trainingBaseline
  elseif engine.neuralBatch then
    baseline=engine:neuralBatch({false},seeds)[1].mean
  else
    for _,seed in ipairs(seeds) do baseline=baseline+engine:observe({},seed).baselineDps/#seeds end
  end
  local curve,retainedFitness={},nil
  for generation=0,opts.generations do
    local population={}
    -- Fitness is deterministic on these fixed seeds, so retain the elite score.
    if generation==0 then population[1]=retainedModel
    else
      for member=2,opts.population do
        local parent=member%2==0 and retainedModel or archive[math.floor(random()*#archive)+1].weights
        population[#population+1]=M.propose(parent,random,opts.mutationSigma,member)
      end
    end
    local scores={}
    if engine.neuralBatch then scores=engine:neuralBatch(population,seeds)
    else
      for member,weights in ipairs(population) do
        local score=0
        for _,seed in ipairs(seeds) do score=score+M.fight(engine,seed,opts,weights).dps/#seeds end
        scores[member]={mean=score}
      end
    end
    local best,fitness=retainedModel,retainedFitness or -math.huge
    local generationBest,generationWorst,generationMean=-math.huge,math.huge,0
    local candidateDps={}
    local improved,equal=0,0
    for member,result in ipairs(scores) do
      candidateDps[member]=result.mean
      if retainedFitness and result.mean>retainedFitness then improved=improved+1 end
      if retainedFitness and result.mean==retainedFitness then equal=equal+1 end
      archive[#archive+1]={weights=population[member],fitness=result.mean,tie=random()}
      generationBest=math.max(generationBest,result.mean);generationWorst=math.min(generationWorst,result.mean)
      generationMean=generationMean+result.mean/#scores
      if result.mean>fitness then best,fitness=population[member],result.mean end
    end
    -- Keep four parents, including neutral replacements. The reported champion
    -- changes only on strict improvement; neutral drift cannot lose that model.
    table.sort(archive,function(a,b) return a.fitness>b.fitness or (a.fitness==b.fitness and a.tie>b.tie) end)
    while #archive>4 do table.remove(archive) end
    for _,entry in ipairs(archive) do entry.tie=random() end
    retainedModel,retainedFitness=best,fitness
    curve[#curve+1]={generation=generation,baselineDps=baseline,policyDps=fitness,trainingFights=#seeds,
      improvedCandidates=improved,equalFitnessCandidates=equal,mutationSigma=opts.mutationSigma,
      generationBestDps=generationBest,generationWorstDps=generationWorst,generationMeanDps=generationMean,candidateDps=candidateDps}
    progress(curve[#curve])
  end
  return retainedModel,curve,seeds
end
return M
