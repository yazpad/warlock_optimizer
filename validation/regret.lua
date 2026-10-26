-- Search semantics tests; no GPU timing or benchmarks.
local regret=require('tools.regret')
local iohelp=require('tools.native_support')
local source=iohelp.read('sim/classes/warlock/generated/fast.glsl')
local derived=regret.instrument(source)
assert(source~=derived and derived:find('void policyDecide()',1,true))
assert(derived:find('seedRandom(0u)',1,true))
assert(not pcall(regret.instrument,source:gsub('void decide','void brokenDecide')))
local calls=0
local best=regret.search(function(plan,n,offset)
 calls=calls+1;assert(n==4 and offset==0)
 -- First action 6 is initially worse, but its second action unlocks a better plan.
 local mean=plan[1]==1 and 10 or 8
 if #plan>=2 and plan[1]==6 and plan[2]==1 then mean=30 end
 return {valid=true,mean=mean,mask=2+64}
end,2+64,{rounds=40,depth=3,samples=4,exploration=100})
assert(calls==40 and best[1] and best[6])
assert(best[6].mean==30 and #best[6].plan>=2,'Search must explore multi-action improvements')
local rejected=regret.search(function(plan)
 return {valid=plan[1]==1,mean=20,mask=0}
end,2+64,{rounds=8,depth=2,samples=4,exploration=100})
assert(rejected[1] and not rejected[6],'Illegal prefixes must not become best plans')
assert(regret.canonical(3)==13 and regret.canonical(14)==16)
assert(regret.planNames({1,6}):find('Life Tap',1,true))
print('PASS: derived-only shader, multi-step UCT exploration and illegal-prefix rejection')
