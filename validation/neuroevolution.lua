local n=require('tools.neuroevolution')
local bit=require('bit')
local zero={};for i=1,n.parameterCount do zero[i]=0 end
local state={time=1,mana=1000,totalDamage=500,mask=bit.bor(2,65536),aplAction=16}
assert(n.select(zero,state,5)==16,'Zero model must reproduce legal APL prior')
state.mask=2;assert(n.select(zero,state,5)==1,'Mask must forbid unavailable actions')
state.mask=0;assert(n.select(zero,state,5)==0)
local a=n.mutate(zero,n.random(42),0.2)
local b=n.mutate(zero,n.random(42),0.2)
for i=1,#a do assert(a[i]==b[i] and a[i]==a[i]) end
local engine={}
function engine:observe(history,seed)
  assert(seed>=65536,'Training leaked evaluation seeds')
  if #history==0 then return {valid=true,terminal=false,baselineDps=100,time=0,mana=500,totalDamage=0,mask=2,aplAction=1} end
  assert(history[1]==1)
  return {valid=true,terminal=true,decisions=1,dps=100}
end
function engine:evaluate() error('Neural policy must never search') end
local points={}
local weights,curve,seeds=n.train(engine,{duration=5,maxDecisions=10,evolutionSeed=42,generations=2,population=3,mutationSigma=0.2},function(p) points[#points+1]=p end)
assert(#weights==n.parameterCount and #curve==3 and #points==3 and #seeds==1000)
for i,p in ipairs(curve) do assert(p.generation==i-1 and math.abs(p.baselineDps-100)<1e-8 and math.abs(p.policyDps-100)<1e-8 and p.trainingFights==1000) end
print('PASS: masked MLP, reproducible mutation, complete-fight evolution, isolated seeds and no search')

-- Training must submit whole populations together and reuse the elite fitness.
local batches={}
local batchEngine={}
function batchEngine:neuralBatch(models,seeds)
  assert(#seeds==1000 and seeds[1]==65536 and seeds[1000]==66535)
  for i=2,#seeds do assert(seeds[i]==seeds[i-1]+1,'Repeated or missing training seed') end
  batches[#batches+1]=#models
  local scores={}
  for i in ipairs(models) do scores[i]={mean=100+i} end
  return scores
end
local _,batched=n.train(batchEngine,{evolutionSeed=42,generations=3,population=8,mutationSigma=0.2},function() end)
assert(#batches==5 and batches[1]==1 and batches[2]==1)
for i=3,5 do assert(batches[i]==7,'Elite was redundantly evaluated or mutants not batched') end
assert(batched[4].policyDps>=batched[3].policyDps)
print('PASS: population batching and cached elite fitness')

local declining={trainingBaseline=100}
local calls=0
function declining:neuralBatch(models,seeds)
  calls=calls+1
  local rows={};for i in ipairs(models) do rows[i]={mean=calls==1 and 100 or 40+i} end
  return rows
end
local _,decline=n.train(declining,{evolutionSeed=42,generations=2,population=3,mutationSigma=0.2},function() end)
assert(decline[2].policyDps==100 and decline[2].generationBestDps==42 and decline[2].generationWorstDps==41)
assert(decline[2].generationMeanDps==41.5 and #decline[2].candidateDps==2)
print('PASS: worse mutations stay visible while retained best is preserved')

-- A contextual two-action task: identical legality/APL, different DoT state.
-- The policy must learn when to depart from the APL, not merely favor one action.
local learn={trainingBaseline=100}
local early={time=0,mana=1000,totalDamage=0,mask=6,aplAction=1,features={}}
local late={time=0,mana=1000,totalDamage=0,mask=6,aplAction=1,features={}}
for i=1,#n.features do early.features[i]=0;late.features[i]=0 end
late.features[2]=1
function learn:neuralBatch(models)
  local rows={}
  for i,w in ipairs(models) do
    local a=n.select(w,early,30);local b=n.select(w,late,30)
    rows[i]={mean=100+(a==2 and 10 or 0)-(b==2 and 10 or 0)}
  end
  return rows
end
local initialized=n.initialize(n.random(42))
assert(n.select(initialized,early,30)==1 and n.select(initialized,late,30)==1,'Initialization must preserve APL')
local learned,learning=n.train(learn,{evolutionSeed=42,generations=256,population=32,mutationSigma=0.1},function() end)
assert(learning[#learning].policyDps==110,'Evolution failed to learn contextual improvement')
assert(n.select(learned,early,30)==2 and n.select(learned,late,30)==1,'Learned policy ignored combat state')
print('PASS: learns a known contextual improvement using combat observations')
