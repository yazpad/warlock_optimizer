local trace=require('tools.regret_trace')
local bit=require('bit')
local function flag(id) return bit.lshift(1,id) end
-- The same gates work for direct spells and DoTs.
for _,id in ipairs({4,6,8,9,13,16}) do
 local e={actionId=1,time=10,cooldowns={[id]=11},affordable=0,enabled=flag(id),passed=flag(id),legal=flag(id),failed={}}
 assert(trace.reason(e,id)=='Cooldown');e.cooldowns[id]=10
 assert(trace.reason(e,id)=='Mana');e.affordable=flag(id)
 assert(trace.reason(e,id)=='Other priority');e.passed=0;e.failed[id]=33
 assert(trace.reason(e,id)=='Decimation inactive');e.failed[id]=5
 assert(trace.reason(e,id)=='APL condition 5');e.enabled=0
 assert(trace.reason(e,id)=='Not in APL');e.actionId=id
 assert(trace.reason(e,id)=='Selected')
end
local a={timeline={{time=3,actionId=8},{time=8,actionId=8},{time=10,actionId=4}}}
local b={timeline={{time=0},{time=2},{time=4},{time=8}}}
local windows=trace.opportunities(a,b,8)
assert(#windows==2 and windows[1].other.time==2 and windows[2].other.time==8)
local function detail(name,dps,casts,ticks,uptime)
 return {damage={{name=name,dps=dps,damage=dps*10,casts=casts,ticks=ticks}},uptimes={{name=name,seconds=uptime}}}
end
local left=detail('Bane of Agony',10,2,10,20)
local right=detail('Bane of Agony',14,3,15,30)
right.damage[2]={name='Pet spells',dps=2,damage=20,casts=1,ticks=0}
local drivers=trace.drivers(left,right)
assert(drivers[1].name=='Bane of Agony' and drivers[1].actionId==8)
assert(drivers[1].castsDelta==1 and drivers[1].ticksDelta==5 and drivers[1].takenUptime==20 and drivers[1].alternativeUptime==30)
assert(drivers[2].actionId==0,'Pet damage must not invent a player action')
local function side(dps,draws)
 return {dps=dps,timeline={{time=0,actionId=8,rngCalls=draws,gates={['8']='Selected'}}}}
end
local samples={{sample=1,taken=side(10,1),alternative=side(12,2)},
 {sample=2,taken=side(10,1),alternative=side(14,1)},
 {sample=3,taken=side(10,1),alternative=side(16,2)}}
local result=trace.summarize(samples,{taken=left,alternative=right})
assert(result.schemaVersion==2 and result.dpsDelta==4 and math.abs(result.standardError-math.sqrt(4/3))<1e-10)
assert(result.example.sample==2 and result.randomConsumptionDivergedPairs==2)
assert(result.drivers[1].gates.taken.Selected==1)
assert(#result.drivers[2].windows==0)
assert(#trace.drivers(left,left)==0,'Identical damage must not invent a contributor')
print('PASS: generic gates, DoT contributors, pet fallback, time alignment and neutral example selection')
