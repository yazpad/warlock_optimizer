local mcts = require('tools.mcts')
local bit = require('bit')
local mask = 2 + 64
local calls, diversified = 0, false
local best, root = mcts.search(function(plans, samples, offset)
  calls = calls + 1
  assert(samples == 4)
  if calls == 1 then
    assert(offset == 0 and #plans == 2, 'Warmup must evaluate all root actions')
  else
    local seen = {}
    for _, plan in ipairs(plans) do
      local key = table.concat(plan, ',')
      assert(not seen[key], 'Duplicate plan wasted a dispatch slot')
      seen[key] = true
    end
    local n = 0; for _ in pairs(seen) do n = n + 1 end
    diversified = diversified or n > 1
  end
  local results = {}
  for i, plan in ipairs(plans) do
    results[i] = {valid = true, mean = (#plan > 1 and plan[1] == 6) and 1200 or 1000, mask = mask}
  end
  return results
end, mask, 1, {depth=3, rounds=12, batchSize=16, samples=4})
assert(calls == 13 and diversified, 'Reservations must diversify batched selection')
assert(best[1] and best[6] and #best[6].plan > 1, 'Search must explore deeper alternatives')
assert(root.children[1].prior == 0.7 and root.children[6].prior > 0.29)
local function clean(node)
  assert((node.pending or 0) == 0, 'Pending reservation leaked')
  for _, child in pairs(node.children) do clean(child) end
end
clean(root)
local _, rejectedRoot = mcts.search(function(plans)
  local results = {}
  for i, plan in ipairs(plans) do
    results[i] = {valid = #plan == 1, mean = 1000, mask = mask}
  end
  return results
end, mask, 1, {depth=2, rounds=4, batchSize=4, samples=4})
assert(rejectedRoot.mean == 1000, 'Illegal descendants must not poison ancestor rewards')
print('PASS: root warmup, batched reservations, deep search, illegal-prefix rejection')
local controller = require('tools.policy_controller')
local observations, forks = {}, 0
local engine = {}
function engine:observe(history, seed)
  observations[#observations+1] = table.concat(history, ',')
  if #history == 0 then
    return {valid=true, terminal=false, mask=mask, aplAction=1, time=0, mana=100, signature={1,2}}
  elseif #history == 1 then
    assert(history[1] == 6, 'Controller must choose the better searched action')
    -- The chosen action changes the next reachable action, time and mana.
    return {valid=true, terminal=false, mask=2^12, aplAction=12, time=1, mana=80, signature={3,4}}
  end
  assert(#history==2 and history[2]==12, 'Controller must follow its own next state')
  return {valid=true, terminal=true, decisions=2, dps=900+seed}
end
function engine:evaluate(history, seed, state, plans, samples, offset)
  assert(#history==0 and state.signature[1]==1, 'Search must fork the actual controller state')
  assert(offset>=1000000 and offset~=seed, 'Search must not use the live future seed')
  forks=forks+1
  local results={}
  for i,plan in ipairs(plans) do
    results[i]={valid=true,mean=plan[1]==6 and 1200 or 1000,mask=0}
    results[i].values={};for j=1,samples do results[i].values[j]=results[i].mean end
  end
  return results
end
local fight=controller.fight(engine,7,{depth=1,rounds=12,batchSize=8,samples=4,maxDecisions=4})
assert(forks>0 and fight.history[1]==6 and fight.history[2]==12)
assert(observations[1]=='' and observations[2]=='6' and observations[3]=='6,12',
  'Decisions must advance from committed history, not baseline decision states')
assert(fight.timeline[2].time==1 and fight.timeline[2].mana==80 and fight.dps==907)
assert(fight.decisions==2 and fight.searches==1)
local summary=controller.summarize({
  {baselineDps=1000,policyDps=900,decisions=2,searches=1},
  {baselineDps=1000,policyDps=1100,decisions=3,searches=2}})
assert(summary.completedFightPairs==2 and summary.dpsDelta==0 and summary.winRatePct==50)
assert(summary.standardError==100 and summary.ciLow<0 and summary.ciHigh>0)
assert(summary.totalDecisions==5 and summary.totalSearches==3)
assert(not pcall(controller.fight,engine,7,{depth=1,rounds=2,batchSize=2,samples=2,maxDecisions=0}),
  'Capacity exhaustion must reject incomplete controller evaluation')
local iohelp=require('tools.native_support')
local policyShader=require('tools.policy_shader')
local source=iohelp.read('sim/classes/warlock/generated/fast.glsl')
local derived=policyShader.instrument(source)
assert(derived~=source and derived:find('seedRandom(control(7u))',1,true))
assert(derived:find('policySignature=policyHash()',1,true) and derived:find('queue[i].data',1,true))
assert(derived:find('control(8u+ordinal)',1,true) and derived:find('policyHistoryApplied',1,true))
assert(not pcall(policyShader.instrument,source:gsub('void decide','void brokenDecide')))
print('PASS: own-trajectory control, independent search futures, complete-fight paired statistics, capacity rejection, derived shader')
-- A modest end-of-fight improvement must beat the 70% APL prior, even
-- when PUCT allocates more visits to the inferior APL recommendation.
local endgame={}
function endgame:observe(history)
 if #history==0 then return {valid=true,terminal=false,mask=mask,aplAction=1,time=177} end
 assert(history[1]==6,'APL visit prior overrode a measured end-of-fight improvement')
 return {valid=true,terminal=true,decisions=1,dps=727}
end
function endgame:evaluate(history,seed,state,plans,samples)
 local results={}
 for i,plan in ipairs(plans) do
  results[i]={valid=true,mean=plan[1]==6 and 727 or 720,mask=mask}
  results[i].values={};for j=1,samples or 8 do results[i].values[j]=results[i].mean end
 end
 return results
end
local endgameOpts={depth=3,rounds=16,batchSize=16,samples=8,cPuct=2,aplWeight=0.7,maxDecisions=2}
local endgameSearch=mcts.search(function(plans) return endgame:evaluate(nil,nil,nil,plans) end,
 mask,1,endgameOpts)
assert(endgameSearch[1].visits>endgameSearch[6].visits and endgameSearch[1].meanDps<endgameSearch[6].meanDps,
 'Regression must exercise the conflict between APL visits and measured value')
controller.fight(endgame,0,endgameOpts)
print('PASS: measured end-of-fight improvement overrides APL visit prior')
-- Training estimates can be misleading: validation must use fresh seeds and
-- reject a noisy advantage instead of executing the largest tree mean.
local noisy={}
local freshValidation=false
function noisy:observe(history)
 if #history==0 then return {valid=true,terminal=false,mask=mask,aplAction=1,time=177} end
 assert(history[1]==1,'Noisy validation gain must retain APL')
 return {valid=true,terminal=true,decisions=1,dps=720}
end
function noisy:evaluate(history,seed,state,plans,samples,offset)
 if offset==1261000 then freshValidation=true;assert(samples==32) end
 local results={}
 for i,plan in ipairs(plans) do
  local values,mean={},0
  for j=1,samples do
   local value=720
   if plan[1]==6 then
    value=920
    if offset==1261000 then value=770+(j%2==0 and 100 or -100) end
   end
   values[j]=value;mean=mean+value/samples
  end
  results[i]={valid=true,mean=mean,mask=mask,values=values}
 end
 return results
end
controller.fight(noisy,0,endgameOpts)
assert(freshValidation,'Final choice must be validated on fresh search seeds')
print('PASS: fresh paired validation rejects noisy training gains')
