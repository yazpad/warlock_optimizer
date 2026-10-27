local ppo=require('tools.ppo')
local bit=require('bit')

local zero={};for i=1,ppo.parameterCount do zero[i]=0 end
local state={time=1,mana=1000,totalDamage=500,mask=bit.bor(2,65536),aplAction=16}
assert(ppo.select(zero,state,5)==16,'Zero model must reproduce legal APL prior')
state.mask=2;assert(ppo.select(zero,state,5)==1,'Mask must forbid unavailable actions')
state.mask=0;assert(ppo.select(zero,state,5)==0)

-- Verify forward pass probabilities sum to 1 over legal actions and 0 on illegal actions
state.mask=bit.bor(2,4,65536)
local x=ppo.buildInput(state,5)
local probs,logits,h,ent=ppo.forwardActor(zero,x,state.mask,16)
local sum=0
for a=0,25 do
  if bit.band(state.mask,bit.lshift(1,a))==0 then
    assert(probs[a]==0,'Illegal action received nonzero probability')
  else
    assert(probs[a]>0,'Legal action received zero probability')
    sum=sum+probs[a]
  end
end
assert(math.abs(sum-1.0)<1e-6,'Action probabilities must sum to 1')
assert(probs[16]>probs[1] and probs[16]>probs[2],'APL prior must boost recommended action probability')
assert(ent>0,'Entropy must be positive for multi-action distribution')
print('PASS: masked softmax probabilities, APL prior and entropy')

-- Verify finite-difference gradient check on Actor and Critic backprop
local rng=ppo.random(42)
local actor=ppo.initializeActor(rng)
local critic=ppo.initializeCritic(rng)
for i=1,#actor do actor[i]=(rng()-0.5)*0.4 end
for i=1,#critic do critic[i]=(rng()-0.5)*0.4 end

local trState={time=2,mana=2500,totalDamage=12000,mask=bit.bor(2,4,64,65536),aplAction=6,features={}}
for i=1,#ppo.features do trState.features[i]=rng() end
local tx=ppo.buildInput(trState,30)
local tProbs,_,_,tEnt=ppo.forwardActor(actor,tx,trState.mask,trState.aplAction)
local oldLogp=math.log(tProbs[2])
local vPred=ppo.forwardCritic(critic,tx)
local tr={
  x=tx,mask=trState.mask,action=2,aplAction=trState.aplAction,
  oldLogp=oldLogp,adv=1.5,ret=vPred+1.2
}

-- Compute analytical gradients by running 1 update with a mock optimizer that captures gradients
local capturedActorGrad,capturedCriticGrad
local function mockActorOpt(_,g)
  capturedActorGrad={}
  for i=1,ppo.parameterCount do capturedActorGrad[i]=g[i] end
end
local function mockCriticOpt(_,g)
  capturedCriticGrad={}
  for i=1,ppo.criticCount do capturedCriticGrad[i]=g[i] end
end
-- With 2 transitions so stdAdv is well-defined
local tr2={
  x=tx,mask=trState.mask,action=6,aplAction=trState.aplAction,
  oldLogp=math.log(tProbs[6]),adv=-1.5,ret=vPred-0.8
}
ppo.update(actor,critic,mockActorOpt,mockCriticOpt,{tr,tr2},{ppoEpochs=1,minibatchSize=2,clipEps=0.2,entropyCoef=0.01,valueCoef=0.5},ppo.random(99))

local function computeLoss(actW,critW)
  local list={tr,tr2}
  local pLoss,vLoss=0,0
  for _,item in ipairs(list) do
    local p,_,_,e=ppo.forwardActor(actW,item.x,item.mask,item.aplAction)
    local lp=math.log(math.max(1e-12,p[item.action]))
    local ratio=math.exp(lp-item.oldLogp)
    local s1=ratio*item.normAdv
    local s2=math.max(0.8,math.min(1.2,ratio))*item.normAdv
    pLoss=pLoss+(-math.min(s1,s2)-0.01*e)*0.5
    local vp=ppo.forwardCritic(critW,item.x)
    vLoss=vLoss+0.5*0.5*(vp-item.ret)^2*0.5
  end
  return pLoss,vLoss
end

local eps=1e-5
for _,idx in ipairs({1,2,76,77,913,914,925,1000,1250}) do
  local orig=actor[idx]
  actor[idx]=orig+eps; local lPlus=computeLoss(actor,critic)
  actor[idx]=orig-eps; local lMinus=computeLoss(actor,critic)
  actor[idx]=orig
  local numGrad=(lPlus-lMinus)/(2*eps)
  assert(math.abs(capturedActorGrad[idx]-numGrad)<1e-4,
    ('Actor grad mismatch at %d: analytical %.6f vs numerical %.6f'):format(idx,capturedActorGrad[idx],numGrad))
end
for _,idx in ipairs({1,2,76,77,913,914,925}) do
  local orig=critic[idx]
  critic[idx]=orig+eps; local _,vPlus=computeLoss(actor,critic)
  critic[idx]=orig-eps; local _,vMinus=computeLoss(actor,critic)
  critic[idx]=orig
  local numGrad=(vPlus-vMinus)/(2*eps)
  assert(math.abs(capturedCriticGrad[idx]-numGrad)<1e-4,
    ('Critic grad mismatch at %d: analytical %.6f vs numerical %.6f'):format(idx,capturedCriticGrad[idx],numGrad))
end
print('PASS: analytical Actor and Critic backpropagation matches finite-difference gradients')

-- Contextual two-step bandit/MDP test:
-- State 1 (time=0, feature[2]=0) and State 2 (time=1, feature[2]=1) both have legal actions {1, 2} and aplAction=1.
-- Action 2 is optimal in State 1 (+50 damage), while Action 1 is optimal in State 2 (+50 damage).
-- PPO must learn the state-dependent policy (Action 2 at t=0, Action 1 at t=1) and improve retained DPS from 100 to 110.
local learn={trainingBaseline=100}
local f1={};local f2={}
for i=1,#ppo.features do f1[i]=0;f2[i]=0 end
f2[2]=1
function learn:neuralBatch(models,seeds,capture,stochastic)
  local rows={}
  local r=ppo.random(12345+#seeds)
  local invTemp=type(stochastic)=='number' and stochastic or 1.0
  for m,w in ipairs(models) do
    if not w then
      local fights={}
      for sIdx in ipairs(seeds) do fights[sIdx]={dps=100} end
      rows[m]={mean=100,fights=fights}
    else
      local sum=0
      local fights={}
      for sIdx in ipairs(seeds) do
        local s1={ordinal=0,time=0,mana=1000,totalDamage=0,mask=6,aplAction=1,features=f1,signature={1,1}}
        local a1=stochastic and ppo.sampleAction(w,s1,10,r,invTemp) or ppo.select(w,s1,10)
        local dmg1=(a1==2) and 600 or 500
        local s2={ordinal=1,time=5,mana=800,totalDamage=dmg1,mask=6,aplAction=1,features=f2,signature={2,2}}
        local a2=stochastic and ppo.sampleAction(w,s2,10,r,invTemp) or ppo.select(w,s2,10)
        local dmg2=dmg1+((a2==1) and 500 or 400)
        local dps=dmg2/10
        sum=sum+dps/#seeds
        if capture then
          s1.action=a1;s2.action=a2
          fights[sIdx]={dps=dps,decisions=2,searches=0,timeline={s1,s2}}
        end
      end
      rows[m]={mean=sum,fights=fights}
    end
  end
  return rows
end

local points={}
local trained,curve,seeds=ppo.train(learn,{duration=10,maxDecisions=10,evolutionSeed=42,generations=40,population=16,mutationSigma=0.02,invTemp=10.0},function(p) points[#points+1]=p end)
assert(#trained==ppo.parameterCount and #curve==41 and #points==41 and #seeds==1000)
assert(math.abs(curve[1].policyDps-100)<1e-8,'Generation 0 must start at APL baseline')
assert(math.abs(curve[#curve].policyDps-110)<1e-8,('PPO failed to learn contextual two-step optimum (got %.2f)'):format(curve[#curve].policyDps))
local checkS1={time=0,mana=1000,totalDamage=0,mask=6,aplAction=1,features=f1}
local checkS2={time=5,mana=800,totalDamage=600,mask=6,aplAction=1,features=f2}
assert(ppo.select(trained,checkS1,10)==2 and ppo.select(trained,checkS2,10)==1,'Trained PPO policy did not select contextual actions')
print('PASS: PPO learns contextual multi-step trajectory improvement with GAE and Adam')
