-- Clipped Proximal Policy Optimization (PPO) over the shared 75-12-26 MLP.
-- Uses Common Random Numbers (CRN) paired APL baselines, GAE-lambda, Adam,
-- inverse-temperature exploration around the APL prior, and batched GPU rollouts.
-- Zero external dependencies; exact float32 weight compatibility with neuro_shader.
local ffi=require('ffi')
local bit=require('bit')
local neuro=require('tools.neuroevolution')
local regret=require('tools.regret')

local M={
  inputs=neuro.inputs,
  hidden=neuro.hidden,
  outputs=neuro.outputs,
  parameterCount=neuro.parameterCount,
  criticCount=(neuro.inputs+1)*neuro.hidden+(neuro.hidden+1),
  aplPrior=neuro.aplPrior,
  version=neuro.version,
  features=neuro.features,
  trainingFights=neuro.trainingFights,
  rolloutFights=64,
  ppoEpochs=4,
  minibatchSize=256,
  clipEps=0.2,
  gamma=0.995,
  gaeLambda=0.95,
  entropyCoef=0.001,
  valueCoef=0.5,
  maxGradNorm=0.5,
  rewardScale=1000.0,
  invTemp=40.0,
}

function M.trainingSeeds()
  return neuro.trainingSeeds()
end

function M.random(seed)
  return neuro.random(seed)
end

local function randn(random)
  return math.sqrt(-2*math.log(math.max(1e-12,random())))*math.cos(2*math.pi*random())
end

function M.initializeActor(random)
  return neuro.initialize(random)
end

function M.initializeCritic(random)
  local weights={}
  local scale=1/math.sqrt(M.inputs)
  local hiddenWords=(M.inputs+1)*M.hidden
  for i=1,hiddenWords do weights[i]=scale*randn(random) end
  for i=hiddenWords+1,M.criticCount do weights[i]=0 end
  return weights
end

local function float32(x)
  local buf=ffi.new('float[1]',x)
  return tonumber(buf[0])
end

function M.quantizeWeights(weights)
  local out={}
  for i=1,#weights do out[i]=float32(weights[i]) end
  return out
end

function M.buildInput(state,duration)
  local x=ffi.new('float[?]',M.inputs)
  x[0]=math.min(1,state.time/duration)
  x[1]=math.min(2,state.mana/10000)
  x[2]=math.min(2,state.totalDamage/1000000)
  local mask=state.mask
  local apl=state.aplAction
  for a=0,25 do
    x[3+a]=bit.band(mask,bit.lshift(1,a))~=0 and 1 or 0
    x[29+a]=apl==a and 1 or 0
  end
  local feats=state.features
  if feats then
    for i=1,#M.features do x[54+i]=feats[i] or 0 end
  end
  return x
end

-- Forward pass for actor on a single observation x (0-indexed float[75])
function M.forwardActor(actor,x,mask,aplAction,invTemp)
  local scale=invTemp or 1.0
  local h=ffi.new('double[?]',M.hidden)
  local k=1
  for j=0,M.hidden-1 do
    local v=actor[k]; k=k+1
    for i=0,M.inputs-1 do v=v+actor[k]*x[i]; k=k+1 end
    h[j]=math.tanh(v)
  end
  local logits=ffi.new('double[26]')
  local maxLogit=-1e38
  for a=0,25 do
    local v=actor[k]; k=k+1
    for j=0,M.hidden-1 do v=v+actor[k]*h[j]; k=k+1 end
    v=(v+(a==aplAction and M.aplPrior or 0))*scale
    logits[a]=v
    if bit.band(mask,bit.lshift(1,a))~=0 and v>maxLogit then
      maxLogit=v
    end
  end
  local probs=ffi.new('double[26]')
  local sumExp=0
  for a=0,25 do
    if bit.band(mask,bit.lshift(1,a))~=0 then
      local e=math.exp(logits[a]-maxLogit)
      probs[a]=e
      sumExp=sumExp+e
    else
      probs[a]=0
    end
  end
  local entropy=0
  if sumExp>0 then
    for a=0,25 do
      if probs[a]>0 then
        probs[a]=probs[a]/sumExp
        entropy=entropy-probs[a]*math.log(math.max(1e-12,probs[a]))
      end
    end
  end
  return probs,logits,h,entropy
end

function M.forwardCritic(critic,x)
  local h=ffi.new('double[?]',M.hidden)
  local k=1
  for j=0,M.hidden-1 do
    local v=critic[k]; k=k+1
    for i=0,M.inputs-1 do v=v+critic[k]*x[i]; k=k+1 end
    h[j]=math.tanh(v)
  end
  local v=critic[k]; k=k+1
  for j=0,M.hidden-1 do v=v+critic[k]*h[j]; k=k+1 end
  return v,h
end

function M.select(weights,state,duration)
  return neuro.select(weights,state,duration)
end

function M.sampleAction(weights,state,duration,random,invTemp)
  local actions=regret.actions(state.mask)
  if #actions==0 then return 0,0,0 end
  local x=M.buildInput(state,duration)
  local probs,_,_,entropy=M.forwardActor(weights,x,state.mask,state.aplAction,invTemp)
  local r=random()
  local acc=0
  local chosen=actions[#actions]
  for _,a in ipairs(actions) do
    acc=acc+probs[a]
    if r<=acc then chosen=a; break end
  end
  local logp=math.log(math.max(1e-12,probs[chosen]))
  return chosen,logp,entropy
end

function M.fight(engine,seed,opts,weights)
  return neuro.fight(engine,seed,opts,weights)
end

local function createAdam(n,lr)
  local m=ffi.new('double[?]',n+1)
  local v=ffi.new('double[?]',n+1)
  local step=0
  return function(params,grads,scaleOverride)
    step=step+1
    local norm2=0
    for i=1,n do norm2=norm2+grads[i]*grads[i] end
    local norm=math.sqrt(norm2)
    local scale=norm>M.maxGradNorm and (M.maxGradNorm/norm) or 1.0
    local b1,b2,eps=0.9,0.999,1e-8
    local bc1=1-b1^step
    local bc2=1-b2^step
    local stepLr=lr*(scaleOverride or 1.0)
    for i=1,n do
      local g=grads[i]*scale
      m[i]=b1*m[i]+(1-b1)*g
      v[i]=b2*v[i]+(1-b2)*g*g
      local mhat=m[i]/bc1
      local vhat=v[i]/bc2
      params[i]=params[i]-stepLr*mhat/(math.sqrt(vhat)+eps)
    end
  end
end

-- Collect trajectories from a batch of fights and compute paired CRN + GAE advantages
function M.collectRollouts(engine,actor,critic,rolloutSeeds,seedBaselineDps,opts,random)
  local invTemp=opts.invTemp or M.invTemp
  local trajectories={}
  if engine.neuralBatch then
    local qActor=M.quantizeWeights(actor)
    local batch=engine:neuralBatch({qActor},rolloutSeeds,true,invTemp)[1]
    for sample,fight in ipairs(batch.fights) do
      local seed=rolloutSeeds[sample]
      local baseDps=seedBaselineDps and seedBaselineDps[seed] or (engine.trainingBaseline or fight.dps)
      local steps=fight.timeline
      local n=#steps
      local traj={dps=fight.dps,baseDps=baseDps,steps=steps}
      -- Common Random Numbers paired whole-fight delta (eliminates cross-seed RNG variance & DoT myopia)
      local pairedDelta=(fight.dps-baseDps)/10.0
      for t=1,n do
        local s=steps[t]
        s.reward=(t==n) and pairedDelta or 0.0
        s.x=M.buildInput(s,opts.duration)
        local probs,_,_,ent=M.forwardActor(qActor,s.x,s.mask,s.aplAction,invTemp)
        s.oldLogp=math.log(math.max(1e-12,probs[s.action]))
        s.entropy=ent
        s.value=M.forwardCritic(critic,s.x)
      end
      trajectories[sample]=traj
    end
  else
    for sample,seed in ipairs(rolloutSeeds) do
      local history,steps={},{}
      local baseDps=seedBaselineDps and seedBaselineDps[seed]
      while true do
        local state=engine:observe(history,seed)
        assert(state.valid,'Invalid PPO trajectory')
        if #history==0 and not baseDps then baseDps=state.baselineDps end
        if state.terminal then
          assert(state.decisions==#history,'Incomplete PPO trajectory')
          local n=#steps
          local pairedDelta=(state.dps-(baseDps or state.dps))/10.0
          for t=1,n do
            local s=steps[t]
            s.reward=(t==n) and pairedDelta or 0.0
          end
          trajectories[sample]={dps=state.dps,baseDps=baseDps or state.dps,steps=steps}
          break
        end
        assert(#history<opts.maxDecisions,'PPO decision capacity exceeded')
        local a,logp,ent=M.sampleAction(actor,state,opts.duration,random,invTemp)
        local x=M.buildInput(state,opts.duration)
        local val=M.forwardCritic(critic,x)
        steps[#steps+1]={ordinal=#history,time=state.time,mana=state.mana,totalDamage=state.totalDamage,
          action=a,aplAction=state.aplAction,mask=state.mask,features=state.features,
          x=x,oldLogp=logp,entropy=ent,value=val}
        history[#history+1]=a
      end
    end
  end

  local transitions={}
  local totalReward,totalEntropy,rolloutBest,rolloutWorst,rolloutMean=0,0,-math.huge,math.huge,0
  local candidateDps={}
  for i,traj in ipairs(trajectories) do
    candidateDps[i]=traj.dps
    rolloutBest=math.max(rolloutBest,traj.dps)
    rolloutWorst=math.min(rolloutWorst,traj.dps)
    rolloutMean=rolloutMean+traj.dps/#trajectories
    local steps=traj.steps
    local gae=0
    for t=#steps,1,-1 do
      local s=steps[t]
      local nextVal=(t<#steps) and steps[t+1].value or 0.0
      local delta=s.reward+nextVal-s.value
      gae=delta+M.gaeLambda*gae
      s.adv=gae
      s.ret=gae+s.value
      totalReward=totalReward+s.reward
      totalEntropy=totalEntropy+s.entropy
      transitions[#transitions+1]=s
    end
  end
  return transitions,{
    meanDps=rolloutMean,
    bestDps=rolloutBest,
    worstDps=rolloutWorst,
    candidateDps=candidateDps,
    meanEntropy=#transitions>0 and totalEntropy/#transitions or 0,
  }
end

function M.update(actor,critic,actorOpt,criticOpt,transitions,opts,random)
  local n=#transitions
  if n==0 then return 0,0,0 end
  -- Normalize advantages across the rollout batch
  local meanAdv,m2=0,0
  for i,tr in ipairs(transitions) do
    local d=tr.adv-meanAdv
    meanAdv=meanAdv+d/i
    m2=m2+d*(tr.adv-meanAdv)
  end
  local stdAdv=math.sqrt(math.max(0,m2)/math.max(1,n-1))+1e-8
  for _,tr in ipairs(transitions) do
    tr.normAdv=(tr.adv-meanAdv)/stdAdv
  end

  local indices={}
  for i=1,n do indices[i]=i end

  local actorGrads=ffi.new('double[?]',M.parameterCount+1)
  local criticGrads=ffi.new('double[?]',M.criticCount+1)
  local dLogits=ffi.new('double[26]')
  local dh=ffi.new('double[?]',M.hidden)
  local hiddenWords=(M.inputs+1)*M.hidden
  local mbSize=math.min(opts.minibatchSize or M.minibatchSize,n)
  local epochs=opts.ppoEpochs or M.ppoEpochs
  local clipEps=opts.clipEps or M.clipEps
  local entropyCoef=opts.entropyCoef or M.entropyCoef
  local valueCoef=opts.valueCoef or M.valueCoef
  local invTemp=opts.invTemp or 1.0

  local totalPolicyLoss,totalValueLoss,totalEntropy,batches=0,0,0,0

  for _=1,epochs do
    -- Fisher-Yates shuffle
    for i=n,2,-1 do
      local j=math.floor(random()*i)+1
      indices[i],indices[j]=indices[j],indices[i]
    end
    for start=1,n,mbSize do
      local stop=math.min(n,start+mbSize-1)
      local count=stop-start+1
      local invCount=1.0/count
      ffi.fill(actorGrads,ffi.sizeof(actorGrads))
      ffi.fill(criticGrads,ffi.sizeof(criticGrads))
      local batchPLoss,batchVLoss,batchEnt=0,0,0

      for idx=start,stop do
        local tr=transitions[indices[idx]]
        local x=tr.x
        local mask=tr.mask
        local a=tr.action
        local adv=tr.normAdv
        if actorOpt then
          local probs,_,hActor,ent=M.forwardActor(actor,x,mask,tr.aplAction,invTemp)
          local logp=math.log(math.max(1e-12,probs[a]))
          local ratio=math.exp(logp-tr.oldLogp)
          local surr1=ratio*adv
          local clippedRatio=math.max(1-clipEps,math.min(1+clipEps,ratio))
          local surr2=clippedRatio*adv
          batchPLoss=batchPLoss-math.min(surr1,surr2)*invCount
          batchEnt=batchEnt+ent*invCount

          -- Gradient of negative PPO surrogate w.r.t. logp
          local dLoss_dLogp=0
          if surr1<=surr2 or (adv>0 and ratio<=1+clipEps) or (adv<0 and ratio>=1-clipEps) then
            dLoss_dLogp=-ratio*adv
          end

          -- Softmax Jacobian + entropy gradient w.r.t. raw unscaled logits (chain rule factor invTemp)
          for act=0,25 do
            local p=probs[act]
            if p>0 then
              local dLogp_dLogit=((act==a) and 1.0 or 0.0)-p
              local dEnt_dLogit=-p*(math.log(math.max(1e-12,p))+ent)
              dLogits[act]=(dLoss_dLogp*dLogp_dLogit-entropyCoef*dEnt_dLogit)*invCount*invTemp
            else
              dLogits[act]=0
            end
          end

          -- Backprop Actor output layer: hidden -> logits
          for j=0,M.hidden-1 do dh[j]=0 end
          for act=0,25 do
            local dl=dLogits[act]
            if dl~=0 then
              local base=hiddenWords+act*(M.hidden+1)
              actorGrads[base+1]=actorGrads[base+1]+dl
              for j=0,M.hidden-1 do
                actorGrads[base+2+j]=actorGrads[base+2+j]+dl*hActor[j]
                dh[j]=dh[j]+dl*actor[base+2+j]
              end
            end
          end

          -- Backprop Actor hidden layer: inputs -> tanh(hidden)
          for j=0,M.hidden-1 do
            local dz=dh[j]*(1-hActor[j]*hActor[j])
            if dz~=0 then
              local base=j*(M.inputs+1)
              actorGrads[base+1]=actorGrads[base+1]+dz
              for i=0,M.inputs-1 do
                actorGrads[base+2+i]=actorGrads[base+2+i]+dz*x[i]
              end
            end
          end
        end

        -- Critic forward + backprop (MSE loss: 0.5 * valueCoef * (v - ret)^2)
        local vPred,hCritic=M.forwardCritic(critic,x)
        local vErr=vPred-tr.ret
        batchVLoss=batchVLoss+0.5*vErr*vErr*invCount
        local dv=valueCoef*vErr*invCount
        criticGrads[hiddenWords+1]=criticGrads[hiddenWords+1]+dv
        for j=0,M.hidden-1 do
          criticGrads[hiddenWords+2+j]=criticGrads[hiddenWords+2+j]+dv*hCritic[j]
          local dz=dv*critic[hiddenWords+2+j]*(1-hCritic[j]*hCritic[j])
          local base=j*(M.inputs+1)
          criticGrads[base+1]=criticGrads[base+1]+dz
          for i=0,M.inputs-1 do
            criticGrads[base+2+i]=criticGrads[base+2+i]+dz*x[i]
          end
        end
      end

      if actorOpt then actorOpt(actor,actorGrads) end
      if criticOpt then criticOpt(critic,criticGrads) end
      totalPolicyLoss=totalPolicyLoss+batchPLoss
      totalValueLoss=totalValueLoss+batchVLoss
      totalEntropy=totalEntropy+batchEnt
      batches=batches+1
    end
  end
  return totalPolicyLoss/math.max(1,batches),totalValueLoss/math.max(1,batches),totalEntropy/math.max(1,batches)
end

function M.train(engine,opts,progress)
  local random=M.random(opts.evolutionSeed or 42)
  local actor=M.initializeActor(random)
  local critic=M.initializeCritic(random)
  local lr=opts.ppoLr or opts.mutationSigma or 3e-3
  local invTemp=opts.invTemp or M.invTemp
  local actorOpt=createAdam(M.parameterCount,lr)
  local criticOpt=createAdam(M.criticCount,lr*2.0)
  local seeds=M.trainingSeeds()
  local rolloutCount=math.min(#seeds,opts.population or M.rolloutFights)
  local baseline=0
  local seedBaselineDps=engine.trainingSeedDps or {}
  if engine.trainingBaseline then
    baseline=engine.trainingBaseline
  elseif engine.neuralBatch then
    local b=engine:neuralBatch({false},seeds)[1]
    baseline=b.mean
    for sIdx,fight in ipairs(b.fights) do seedBaselineDps[seeds[sIdx]]=fight.dps end
  else
    for _,seed in ipairs(seeds) do
      local d=engine:observe({},seed).baselineDps
      seedBaselineDps[seed]=d
      baseline=baseline+d/#seeds
    end
  end

  local retainedModel=M.quantizeWeights(actor)
  local retainedFitness=baseline
  local curve={}

  -- Generation 0: initial model reproduces APL exactly; record baseline before PPO updates
  curve[1]={
    generation=0,
    baselineDps=baseline,
    policyDps=retainedFitness,
    trainingFights=#seeds,
    improvedCandidates=0,
    equalFitnessCandidates=1,
    mutationSigma=lr,
    generationBestDps=baseline,
    generationWorstDps=baseline,
    generationMeanDps=baseline,
    candidateDps={baseline},
  }
  progress(curve[1])

  local updateOpts={
    ppoEpochs=opts.ppoEpochs or M.ppoEpochs,
    minibatchSize=opts.minibatchSize or M.minibatchSize,
    clipEps=opts.clipEps or M.clipEps,
    entropyCoef=opts.entropyCoef or M.entropyCoef,
    valueCoef=opts.valueCoef or M.valueCoef,
    invTemp=invTemp,
    duration=opts.duration,
    maxDecisions=opts.maxDecisions,
  }

  local criticWarmup=math.min(2,math.floor(opts.generations/4))

  for generation=1,opts.generations do
    -- Select a subset of training seeds for stochastic rollout collection
    local rolloutSeeds={}
    local offset=((generation-1)*rolloutCount)%#seeds
    for i=1,rolloutCount do
      rolloutSeeds[i]=seeds[(offset+i-1)%#seeds+1]
    end

    -- Start each PPO step from the best retained policy (with micro-perturbation when stagnant)
    for i=1,M.parameterCount do actor[i]=retainedModel[i] end
    if generation>criticWarmup+1 then
      local hiddenWords=(M.inputs+1)*M.hidden
      local row=math.floor(random()*M.outputs)
      local perturbScale=0.04*lr*((generation%3)+1)
      for i=hiddenWords+1,M.parameterCount do
        if generation%2==0 or math.floor((i-hiddenWords-1)/(M.hidden+1))==row then
          actor[i]=actor[i]+perturbScale*randn(random)
        end
      end
    end

    local transitions,rolloutStats=M.collectRollouts(engine,actor,critic,rolloutSeeds,seedBaselineDps,updateOpts,random)
    local activeActorOpt=(generation>criticWarmup) and actorOpt or nil
    local pLoss,vLoss,ent=M.update(actor,critic,activeActorOpt,criticOpt,transitions,updateOpts,random)

    -- Evaluate updated deterministic policy on the full 1000-fight training cohort
    -- along with trust-region line-search scales along the PPO gradient direction
    local candidateWeights=M.quantizeWeights(actor)
    local evalScore=baseline
    local candidateDps={}
    local genBest,genWorst,genMean=baseline,baseline,baseline
    local improved,equal=0,0
    if engine.neuralBatch then
      local candidates={}
      -- Line-search trust-region scales along the PPO update step (0.1x, 0.25x, 0.5x, 1.0x, 2.0x, 4.0x)
      for _,alpha in ipairs({0.1,0.25,0.5,1.0,2.0,4.0}) do
        local interp={}
        for i=1,M.parameterCount do
          interp[i]=retainedModel[i]+alpha*(actor[i]-retainedModel[i])
        end
        candidates[#candidates+1]=M.quantizeWeights(interp)
      end
      local scores=engine:neuralBatch(candidates,seeds)
      genBest,genWorst,genMean=-math.huge,math.huge,0
      evalScore=-math.huge
      for idx,res in ipairs(scores) do
        candidateDps[idx]=res.mean
        if res.mean>retainedFitness then improved=improved+1 end
        if res.mean==retainedFitness then equal=equal+1 end
        genBest=math.max(genBest,res.mean)
        genWorst=math.min(genWorst,res.mean)
        genMean=genMean+res.mean/#scores
        if res.mean>evalScore then
          evalScore=res.mean
          candidateWeights=candidates[idx]
        end
      end
    else
      evalScore=0
      for _,seed in ipairs(seeds) do evalScore=evalScore+M.fight(engine,seed,opts,candidateWeights).dps/#seeds end
      candidateDps={evalScore}
      genBest=math.max(evalScore,rolloutStats.meanDps)
      genWorst=math.min(evalScore,rolloutStats.meanDps)
      genMean=evalScore
      if evalScore>retainedFitness then improved=1 end
      if evalScore==retainedFitness then equal=1 end
    end

    if evalScore>retainedFitness then
      retainedModel=candidateWeights
      retainedFitness=evalScore
    end

    curve[#curve+1]={
      generation=generation,
      baselineDps=baseline,
      policyDps=retainedFitness,
      trainingFights=#seeds,
      improvedCandidates=improved,
      equalFitnessCandidates=equal,
      mutationSigma=lr,
      generationBestDps=genBest,
      generationWorstDps=genWorst,
      generationMeanDps=genMean,
      rolloutMeanDps=rolloutStats.meanDps,
      candidateDps=candidateDps,
      policyLoss=pLoss,
      valueLoss=vLoss,
      entropy=ent,
    }
    progress(curve[#curve])
  end

  return retainedModel,curve,seeds
end

return M
