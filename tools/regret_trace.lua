-- On-demand observation only. No changes to the normal simulation path.
local bit=require('bit')
local regret=require('tools.regret')
local M={stride=52}
M.words=32+regret.capacity*M.stride
M.actions={['Shadow Bolt']=16,['Soul Fire']=4,['Corruption']=6,['Bane of Doom']=7,['Bane of Agony']=8,['Immolate']=9,['Conflagrate']=10,['Shadowburn']=11,['Incinerate']=12,['Searing Pain']=13,['Siphon Life']=17,['Wrack']=18,['Hellfire']=19}
local costs={[2]=0,[4]=13,[6]=1,[7]=6,[8]=2,[9]=3,[10]=8,[11]=9,[12]=4,[13]=5,[16]=0,[17]=9,[18]=11,[19]=23}
local function once(s,a,b)
 local p=assert(s:find(a,1,true),'Trace anchor missing: '..a)
 assert(not s:find(a,p+#a,true),'Ambiguous trace anchor')
 return s:sub(1,p-1)..b..s:sub(p+#a)
end
function M.instrument(source)
 local s=regret.instrument(source)
 s=s:gsub('regretLane%*'..regret.words..'u','regretLane*'..M.words..'u'):gsub('lane%*'..regret.words..'u','lane*'..M.words..'u')
 s=once(s,'ordinal*5u','ordinal*'..M.stride..'u')
 s=once(s,'void policyDecide(){','uint traceRule=0u;\nvoid policyDecide(){')
 s=s:gsub('regretTaken=action;return;','traceRule=r+1u;regretTaken=action;return;')
 local affordable='uint affordable=2u;\n'
 for action,spell in pairs(costs) do affordable=affordable..('if(s.mana>=cost(%du))affordable|=1u<<%du;\n'):format(spell,action) end
 affordable=affordable..'if(s.mana>=200.0)affordable|=1u<<25u;\n'
 local fields={'s.rngCalls','regretForced!=0u?1u:0u','passed','enabled','affordable','floatBitsToUint(currentPower())','floatBitsToUint(hasteMultiplier())','s.decimationEnd','s.doomEnd','s.agonyEnd','s.corrEnd','s.immEnd','s.siphonEnd','s.soulFireReady','s.doomReady','s.conflagReady','s.shadowburnReady','s.drainHopeReady','s.trinketEnd'}
 local writes=''
 for i,v in ipairs(fields) do writes=writes..('resultData[p+%du]=%s;\n'):format(5+i,v) end
 local capture=([[
 traceRule=0u;
 if(ordinal<%du){
  uint p=regretLane*%du+32u+ordinal*%du;
  uint passed=0u,enabled=0u,failed[27];
  for(uint a=0u;a<27u;a++)failed[a]=0u;
  float hp=max(0.0,(1.0-float(s.now)/max(1.0,float(c.end)))*100.0);
  for(uint r=0u;r<numAplRules;r++){
   uint h=getConfigWord(currentCfgIdx,APL_HEADER0_OFFSET+r),h1=getConfigWord(currentCfgIdx,APL_HEADER1_OFFSET+r);
   uint a=h&255u;if(a==3u||a==5u)a=13u;if(a==14u||a==15u)a=16u;
   if(a>=27u||(h>>24u)==0u)continue;
   enabled|=1u<<a;
   bool first=evalCond((h>>8u)&255u,uintBitsToFloat(getConfigWord(currentCfgIdx,APL_PARAM0_OFFSET+r)),(h>>16u)&255u,s.mana/c.maxMana*100.0,hp);
   bool second=evalCond(h1&255u,uintBitsToFloat(getConfigWord(currentCfgIdx,APL_PARAM1_OFFSET+r)),(h1>>8u)&255u,s.mana/c.maxMana*100.0,hp);
   if(first&&second)passed|=1u<<a;
   else if(failed[a]==0u)failed[a]=!first?((h>>8u)&255u):(h1&255u);
  }
  // Filler is also available through the fallback path.
  uint filler=c.filler==0u?16u:c.filler==4u?12u:13u;
  enabled|=(1u<<filler)|2u;passed|=(1u<<filler)|2u;
  %s
  %s
  for(uint a=0u;a<27u;a++)resultData[p+25u+a]=failed[a];
 }
 policyDecide();
]]):format(regret.capacity,M.words,M.stride,affordable,writes)
 s=once(s,' policyDecide();',capture)
 s=once(s,'resultData[base+4u]=mask;','resultData[base+4u]=mask;resultData[base+5u]=traceRule;')
 return s
end
local function has(mask,id) return bit.band(mask,bit.lshift(1,id))~=0 end
function M.reason(e,id)
 if e.actionId==id or id==16 and e.actionId==2 then return 'Selected' end
 if (e.cooldowns[id] or 0)>e.time then return 'Cooldown' end
 if not has(e.affordable,id) then return 'Mana' end
 if not has(e.enabled,id) then return 'Not in APL' end
 if not has(e.passed,id) then
  local cond=e.failed[id] or 0
  if cond==33 then return 'Decimation inactive' end
  if cond==34 then return 'Decimation active' end
  return 'APL condition '..cond
 end
 if not has(e.legal,id) then return 'Spell unavailable' end
 return e.forced and 'Forced action' or 'Other priority'
end
function M.decode(data,base,float,target,duration)
 local n=tonumber(data[base+16]);assert(n<=regret.capacity,'Trace capacity exceeded')
 local out={timeline={},dps=float(data[base])/duration}
 for ordinal=target,n-1 do
  local p=base+32+ordinal*M.stride
  local function uint(i) return tonumber(data[p+i]) end
  local action=regret.canonical(uint(3))
  local e={decision=ordinal,time=uint(0)/1e6,mana=float(data[p+1]),actionId=action,action=regret.names[action] or tostring(action),legal=uint(4),rule=uint(5),rngCalls=uint(6),forced=uint(7)~=0,passed=uint(8),enabled=uint(9),affordable=uint(10),power=float(data[p+11]),haste=float(data[p+12]),decimationEnd=uint(13)/1e6,trinketEnd=uint(24)/1e6,cooldowns={},effects={},failed={}}
  for i,id in ipairs({7,8,6,9,17}) do e.effects[id]=uint(13+i)/1e6 end
  for i,id in ipairs({4,7,10,11,18}) do e.cooldowns[id]=uint(18+i)/1e6 end
  for id=0,26 do e.failed[id]=uint(25+id) end
  -- Export maps with string keys, consistently with JSON decoding in the UI.
  e.gates={};e.remaining={}
  for _,id in pairs(M.actions) do
   e.gates[tostring(id)]=M.reason(e,id)
   e.remaining[tostring(id)]=math.max(0,(e.effects[id] or 0)-e.time)
  end
  e.cooldowns=nil;e.effects=nil;e.failed=nil
  out.timeline[#out.timeline+1]=e
 end
 return out
end
function M.drivers(left,right)
 local values={}
 for index,detail in ipairs({left,right}) do
  for _,spell in ipairs(detail.damage) do
   local d=values[spell.name] or {name=spell.name,taken={},alternative={},actionId=M.actions[spell.name] or 0}
   d[index==1 and 'taken' or 'alternative']=spell;values[spell.name]=d
  end
  for _,effect in ipairs(detail.uptimes or {}) do
   if values[effect.name] then values[effect.name][index==1 and 'takenUptime' or 'alternativeUptime']=effect.seconds end
  end
 end
 local out={}
 for _,d in pairs(values) do
  d.dpsDelta=(d.alternative.dps or 0)-(d.taken.dps or 0)
  d.castsDelta=(d.alternative.casts or 0)-(d.taken.casts or 0)
  d.ticksDelta=(d.alternative.ticks or 0)-(d.taken.ticks or 0)
  if math.abs(d.dpsDelta)>0.000001 then out[#out+1]=d end
 end
 table.sort(out,function(a,b) if math.abs(a.dpsDelta)==math.abs(b.dpsDelta) then return a.name<b.name end;return math.abs(a.dpsDelta)>math.abs(b.dpsDelta) end)
 return out
end
-- Match by wall-clock time, not post-fork decision ordinal.
function M.opportunities(a,b,id)
 local out={};local j=1
 for _,e in ipairs(a.timeline) do if e.actionId==id or id==16 and e.actionId==2 then
  while j<#b.timeline and b.timeline[j+1].time<=e.time do j=j+1 end
  out[#out+1]={event=e,other=b.timeline[j]}
 end end
 return out
end
function M.summarize(samples,details)
 local result={schemaVersion=2,samples=#samples,drivers=M.drivers(details.taken,details.alternative),randomConsumptionDivergedPairs=0,outcomes={}}
 local mean,m2=0,0
 for i,p in ipairs(samples) do
  local x=p.alternative.dps-p.taken.dps;local d=x-mean;mean=mean+d/i;m2=m2+d*(x-mean)
  result.outcomes[i]={sample=p.sample,dpsDelta=x}
  for k=1,math.min(#p.taken.timeline,#p.alternative.timeline) do
   if p.taken.timeline[k].rngCalls~=p.alternative.timeline[k].rngCalls then result.randomConsumptionDivergedPairs=result.randomConsumptionDivergedPairs+1;break end
  end
 end
 result.dpsDelta=mean;result.standardError=math.sqrt(m2/math.max(1,#samples-1)/#samples)
 result.ciLow=mean-1.96*result.standardError;result.ciHigh=mean+1.96*result.standardError
 -- One neutral example for all spell views; never select by a particular spell's wins.
 local sorted={};for i,p in ipairs(result.outcomes) do sorted[i]={index=i,delta=p.dpsDelta} end
 table.sort(sorted,function(a,b) if a.delta==b.delta then return a.index<b.index end;return a.delta<b.delta end)
 local example=samples[sorted[math.ceil(#sorted/2)].index]
 result.example={sample=example.sample,taken=example.taken,alternative=example.alternative}
 for i=2,math.min(#example.taken.timeline,#example.alternative.timeline) do
  local a,b=example.taken.timeline[i],example.alternative.timeline[i]
  if a.actionId~=b.actionId or a.time~=b.time then result.example.firstChangedDecision={taken=a,alternative=b};break end
 end
 for _,driver in ipairs(result.drivers) do
  driver.gates={taken={},alternative={}};driver.windows={}
  if driver.actionId~=0 then
   local id=tostring(driver.actionId)
   for _,p in ipairs(samples) do for _,side in ipairs({'taken','alternative'}) do
    for _,e in ipairs(p[side].timeline) do
     local reason=e.gates[id];driver.gates[side][reason]=(driver.gates[side][reason] or 0)+1/#samples
    end
   end end
   driver.windows.taken=M.opportunities(example.taken,example.alternative,driver.actionId)
   driver.windows.alternative=M.opportunities(example.alternative,example.taken,driver.actionId)
  end
 end
 return result
end
return M
