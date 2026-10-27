-- Opt-in diagnostic shader and open-loop MCTS. Never loaded by the fast runner.
local ffi=require('ffi')
local bit=require('bit')
local iohelp=require('tools.native_support')
local M={capacity=1024,depth=4}
M.detailWords=32+require('sim.classes.warlock.generated.layout').detailedWords
M.words=32+M.capacity*5
M.stride=3+M.depth
local function once(source,anchor,replacement)
 local at=assert(source:find(anchor,1,true),'Regret anchor missing: '..anchor)
 assert(not source:find(anchor,at+#anchor,true),'Ambiguous regret anchor: '..anchor)
 return source:sub(1,at-1)..replacement..source:sub(at+#anchor)
end
function M.instrument(source,compact,details)
 local reportWords=details and M.detailWords or (compact and 32 or M.words)
 local detailedOutput
 if details then
  local first=assert(source:find(" resultData[lane*163u+0u]",1,true))
  detailedOutput=source:sub(first):match("^(.-)\n}")
  assert(detailedOutput,"Missing detailed output")
  source=once(source,detailedOutput,"for(uint word=0u;word<16u;word++) resultData[lane*16u+word]=outputWord(word);")
  source=once(source,"return mode==1u?stateWord(index):compactWord(index);","return fastWord(index);")
 end
 local start=assert(source:find('void decide(){',1,true))
 local finish=assert(source:find('\nvoid creditManaRegen',start,true))
 local original=source:sub(start,finish-1)
 local preStart=assert(original:find(' checkTrinket();',1,true))
 local preEnd=assert(original:find(' float fightProgress=',1,true))
 local prelude=original:sub(preStart,preEnd-1)
 original=original:sub(1,preStart-1)..original:sub(preEnd)
 local actionStart=assert(original:find('  // Execute Action',1,true))
 local actionEnd=assert(original:find('\n }\n\n // Fallback',actionStart,true))
 local actionCode=original:sub(actionStart,actionEnd-1)
 -- Off-GCD actions retain their existing APL behavior, but are not search edges.
 local helper=([[
uint regretLane,regretGlobal,regretDecisions=0u,regretTaken=0u,regretInvalid=0u,regretReached=0u;
uint regretRootTime=0u,regretRootMana=0u,regretRootTrance=0u;
uint regretLeafMask=0u,regretApplied=0u,regretForced=0u;
uint control(uint n){return configData[229u+regretGlobal*%du+n];}
uint regretMask(){
 uint allowed=1u<<1u;
 for(uint r=0u;r<numAplRules;r++){
  uint h=getConfigWord(currentCfgIdx,APL_HEADER0_OFFSET+r);
  if((h>>24u)!=0u)allowed|=1u<<(h&255u);
 }
 allowed|=1u<<(c.filler==0u?16u:c.filler==4u?12u:13u);
 if(c.nightfall>0.0)allowed|=1u<<2u;
 if((allowed&((1u<<3u)|(1u<<5u)))!=0u)allowed|=1u<<13u;
 if((allowed&((1u<<14u)|(1u<<15u)))!=0u)allowed|=1u<<16u;
 uint mask=1u<<1u;
 if(s.trance!=0u&&s.mana>=cost(0u))mask|=1u<<2u;
 if(s.now>=s.soulFireReady&&s.mana>=cost(13u))mask|=1u<<4u;
 if(c.corr!=0u&&s.mana>=cost(1u))mask|=1u<<6u;
 if(c.curseOfDoom!=0u&&s.now>=s.doomReady&&s.doomActive==0u&&s.agonyTicks==0u&&s.mana>=cost(6u))mask|=1u<<7u;
 if(c.agony!=0u&&s.doomActive==0u&&s.mana>=cost(2u))mask|=1u<<8u;
 if(c.immolate!=0u&&s.mana>=cost(3u))mask|=1u<<9u;
 if(c.conflagrate!=0u&&s.immTicks>0u&&s.now>=s.conflagReady&&s.mana>=cost(8u))mask|=1u<<10u;
 if(c.shadowburn!=0u&&s.now>=s.shadowburnReady&&s.mana>=cost(9u))mask|=1u<<11u;
 if(c.incinerate!=0u&&s.mana>=cost(4u))mask|=1u<<12u;
 if(s.mana>=cost(5u))mask|=1u<<13u;
 if(s.mana>=cost(0u))mask|=1u<<16u;
 if(c.siphonLife!=0u&&s.mana>=cost(9u))mask|=1u<<17u;
 if(c.drainHope!=0u&&s.now>=s.drainHopeReady&&s.mana>=cost(11u))mask|=1u<<18u;
 if(s.mana>=cost(23u))mask|=1u<<19u;
 if(c.assignedElements!=0u&&s.now>=s.elementsEnd&&s.mana>=200.0)mask|=1u<<25u;
 mask&=allowed;

 return mask;
}
]]):format(M.stride)
 -- Record the action that actually consumes the decision, including fallback.
 local patched=actionCode:gsub('return;','regretTaken=action;return;')
 original=once(original,actionCode,patched)
 original=once(original,'for(uint r=0u;r<numAplRules;r++){','for(uint r=0u;r<max(1u,numAplRules);r++){')
 original=once(original,'if(enabled==0u)continue;','if(enabled==0u&&regretForced==0u)continue;')
 original=once(original,'uint action=header0&0xFFu;','uint action=regretForced!=0u?regretForced:header0&0xFFu;')
 original=once(original,'if(!evalCond(cond1,param1,targetSpell1,playerManaPct,targetHpPct))continue;',
  'if(regretForced==0u&&!evalCond(cond1,param1,targetSpell1,playerManaPct,targetHpPct))continue;')
 original=once(original,'if(!evalCond(cond2,param2,targetSpell2,playerManaPct,targetHpPct))continue;',
  'if(regretForced==0u&&!evalCond(cond2,param2,targetSpell2,playerManaPct,targetHpPct))continue;')
 original=once(original,'void decide(){','void policyDecide(){')
 original=once(original,'if(s.mana>=cost(c.filler)){beginCast(c.filler);return;}','if(s.mana>=cost(c.filler)){regretTaken=c.filler==0u?16u:c.filler==4u?12u:13u;beginCast(c.filler);return;}')
 original=once(original,'\n tap();\n}','\n regretTaken=1u;tap();\n}')
 local wrapper=([[
void decide(){
 if(s.casting!=0u||s.now<s.ready)return;
%s
 uint ordinal=regretDecisions++;
 uint at=s.now,mask=0u;float mana=s.mana;uint trance=s.trance;
 // Only the capture lane and tree leaf need expensive legality enumeration.
 if(control(0u)==0xffffffffu||ordinal>=control(0u))mask=regretMask();
 if(ordinal==control(0u)){
  regretReached=1u;regretRootTime=at;regretRootMana=floatBitsToUint(mana);regretRootTrance=trance;
  if(control(1u)!=0xffffffffu)seedRandom(control(1u));
 }
 regretTaken=0u;
 regretForced=0u;
 if(ordinal>=control(0u)&&ordinal-control(0u)<control(2u)){
  uint a=control(3u+ordinal-control(0u));
  if(a!=0u){
   if((mask&(1u<<a))!=0u){regretForced=a;regretApplied++;}
   else regretInvalid++;
  }
 }
 if(ordinal==control(0u)+control(2u))regretLeafMask=mask;
 policyDecide();
 regretForced=0u;
 if(ordinal<%du){
  uint base=regretLane*%du+32u+ordinal*5u;
  resultData[base]=at;resultData[base+1u]=floatBitsToUint(mana);
  resultData[base+2u]=trance;resultData[base+3u]=regretTaken;resultData[base+4u]=mask;
 }
}
]]):format(prelude,compact and 0 or M.capacity,reportWords)
 source=source:sub(1,start-1)..helper..original..wrapper..source:sub(finish)
 source=once(source,'uint globalLane=offset+lane;','uint globalLane=offset+lane;regretLane=lane;regretGlobal=globalLane;')
 source=once(source,'seedRandom(mode==2u?0u:(numConfigs>1u?fightInCfg:globalLane));','seedRandom(0u);')
 source=once(source,'for(uint word=0u;word<16u;word++) resultData[lane*16u+word]=outputWord(word);',([[
 for(uint word=0u;word<16u;word++)resultData[lane*%du+word]=outputWord(word);
 resultData[lane*%du+16u]=regretDecisions;
 resultData[lane*%du+17u]=regretInvalid;
 resultData[lane*%du+18u]=regretReached;
 resultData[lane*%du+19u]=regretLeafMask;
 resultData[lane*%du+20u]=regretApplied;
 resultData[lane*%du+21u]=regretRootTime;
 resultData[lane*%du+22u]=regretRootMana;
 resultData[lane*%du+23u]=regretRootTrance;
 resultData[lane*%du+24u]=floatBitsToUint(regretDoomDamage);
 resultData[lane*%du+25u]=s.now;
]]):format(reportWords,reportWords,reportWords,reportWords,reportWords,reportWords,reportWords,reportWords,reportWords,reportWords,reportWords))
 source=once(source,'uint numAplRules;','uint numAplRules;\nfloat regretDoomDamage=0.0;')
 source=once(source,'s.total+=amount;eventDamage+=amount;if(crit)eventFlags|=1u;',
  'if(spell==6u)regretDoomDamage+=amount;s.total+=amount;eventDamage+=amount;if(crit)eventFlags|=1u;')
 if details then
  detailedOutput=detailedOutput:gsub('lane%*163u%+(%d+)u',function(word) return 'lane*'..reportWords..'u+'..(32+tonumber(word))..'u' end)
  source=once(source,'resultData[lane*'..reportWords..'u+25u]=s.now;', 'resultData[lane*'..reportWords..'u+25u]=s.now;\n'..detailedOutput)
 end
 return source
end
M.names={[0]='APL continuation',[1]='Life Tap',[2]='Shadow Bolt (Shadow Trance)',[4]='Soul Fire',[6]='Corruption',[7]='Bane of Doom',[8]='Bane of Agony',[9]='Immolate',[10]='Conflagrate',[11]='Shadowburn',[12]='Incinerate',[13]='Searing Pain',[16]='Shadow Bolt',[17]='Siphon Life',[18]='Wrack',[19]='Hellfire',[25]='Curse of the Elements'}
function M.actions(mask)
 local out={}
 for a=1,26 do if bit.band(mask,bit.lshift(1,a))~=0 then out[#out+1]=a end end
 return out
end
function M.canonical(a) if a==3 or a==5 then return 13 elseif a==14 or a==15 then return 16 end;return a end
function M.planNames(plan) local out={};for _,a in ipairs(plan) do out[#out+1]=M.names[a] or ('Action '..a) end;return table.concat(out,' → ')..' → APL' end
-- Batched open-loop UCT: nodes are action prefixes, chance is sampled by GPU lanes.
-- A prefix must be legal in every sampled state to be expanded further.
function M.search(evaluate,rootMask,opts)
 local root={plan={},children={},visits=0,mean=0,mask=rootMask}
 local best={}
 local function update(node,value)
  node.visits=node.visits+1;node.mean=node.mean+(value-node.mean)/node.visits
 end
 for iteration=1,opts.rounds do
  local node=root;local path={root}
  while #node.plan<opts.depth do
   local actions=M.actions(node.mask)
   local chosen
   for _,a in ipairs(actions) do
    if not node.children[a] then
     local plan={unpack(node.plan)};plan[#plan+1]=a
     chosen={plan=plan,children={},visits=0,mean=0,mask=0};node.children[a]=chosen;break
    end
   end
   if chosen then node=chosen;path[#path+1]=node;break end
   local score=-math.huge
   for _,child in pairs(node.children) do
    if not child.rejected then
     local ucb=child.mean+opts.exploration*math.sqrt(math.log(node.visits+1)/math.max(1,child.visits))
     if ucb>score then chosen=child;score=ucb end
    end
   end
   if not chosen then break end
   node=chosen;path[#path+1]=node
  end
  if #node.plan==0 then break end
  local result=evaluate(node.plan,opts.samples,0)
  if result.valid then
   node.mask=result.mask
   for _,p in ipairs(path) do update(p,result.mean) end
   local a=node.plan[1]
   if not best[a] or result.mean>best[a].mean then best[a]={plan=node.plan,mean=result.mean} end
  else node.rejected=true;node.mask=0;update(node,-1e9) end
 end
 return best
end
return M
