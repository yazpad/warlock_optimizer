-- Detailed-only instrumentation. The pinned fast/detailed kernels are never edited.
local ffi=require('ffi')
local iohelp=require('tools.native_support')
local M={capacity=8192,recordWords=8,headerWords=165}
M.words=M.headerWords+M.capacity*M.recordWords
local function once(source,anchor,replacement)
 local at=assert(source:find(anchor,1,true),'Trace anchor missing: '..anchor)
 assert(not source:find(anchor,at+#anchor,true),'Ambiguous trace anchor: '..anchor)
 return source:sub(1,at-1)..replacement..source:sub(at+#anchor)
end
function M.instrument(source)
 local helper=([[
uint traceLane;
void traceStore(uint slot,uint at,uint data,float damageValue,uint flags){
 if(slot>=%du)return;
 uint base=traceLane*%du+165u+slot*8u;
 resultData[base]=at;resultData[base+1u]=data;
 resultData[base+2u]=floatBitsToUint(damageValue);
 resultData[base+3u]=floatBitsToUint(s.mana);
 resultData[base+4u]=floatBitsToUint(s.petMana);
 resultData[base+5u]=floatBitsToUint(s.total);
 resultData[base+6u]=flags;
 resultData[base+7u]=s.casting;
}
void traceAction(uint kind,uint spell){traceStore(s.traceCount++,s.now,kind|(spell<<8u),0.0,0u);}
]]):format(M.capacity,M.words)
 source=once(source,'uint numAplRules;','uint numAplRules;\n'..helper)
 source=once(source,'uint globalLane=offset+lane;','uint globalLane=offset+lane;traceLane=lane;')
 source=once(source,'void countCast(uint spell){','void countCast(uint spell){traceAction(101u,spell);')
 source=once(source,'void countMiss(uint spell){','void countMiss(uint spell){traceStore(s.traceCount++,s.now,105u|(spell<<8u),0.0,2u);')
 source=once(source,'s.total+=amount;eventDamage+=amount;if(crit)eventFlags|=1u;','s.total+=amount;eventDamage+=amount;if(crit)eventFlags|=1u;traceStore(s.traceCount++,s.now,(dealingPeriodic?104u:103u)|(spell<<8u),amount,uint(crit));')
 source=once(source,'void beginCast(uint spell){','void beginCast(uint spell){traceAction(100u,spell);')
 source=once(source,'s.gained+=c.tapGain;s.taps++;','s.gained+=c.tapGain;s.taps++;traceAction(102u,0u);')
 local anchor='creditManaRegen(e.at);s.now=e.at;s.events++;eventDamage=0.0;eventFlags=0u;'
 source=once(source,anchor,anchor..'uint traceSlot=s.traceCount++;')
 source=once(source,'default:s.done=5u;break;\n }\n}', 'default:s.done=5u;break;\n }\n traceStore(traceSlot,e.at,e.data,EV_KIND(e)==6u?eventDamage:0.0,EV_KIND(e)==6u?eventFlags:0u);\n}')
 local count
 source,count=source:gsub('resultData%[lane%*163u%+',('resultData[lane*%du+'):format(M.words))
 assert(count==163,'Detailed report store layout changed')
 local final=assert(source:match('()\n}%s*$'))
 source=source:sub(1,final-1)..('\n resultData[lane*%du+163u]=min(s.traceCount,%du);\n resultData[lane*%du+164u]=s.traceCount;'):format(M.words,M.capacity,M.words)..source:sub(final)
 return source
end
local kinds={[1]='Cast complete',[2]='Impact',[3]='Periodic tick',[5]='Decision',[6]='Pet attack',[8]='Shadow Trance expires',[10]='Heartbeat',[11]='Trinket expires',[12]='Trinket activates',[13]='Fight ends',[14]='Pet inheritance',[15]='Racial expires',[16]='Explosive cast',[17]='Explosive impact',[100]='Cast starts',[101]='Cast',[102]='Life Tap',[103]='Damage',[104]='Periodic damage',[105]='Miss'}
local spells={[0]='Shadow Bolt',[1]='Corruption',[2]='Bane of Agony',[3]='Immolate',[4]='Incinerate',[5]='Searing Pain',[6]='Bane of Doom',[7]='Soul Fire',[8]='Conflagrate',[9]='Shadowburn',[10]='Siphon Life',[11]='Wrack',[12]='Touch of the Grave',[13]='Hellfire',[14]='Ez-Thro Dynamite II',[15]='Curse of the Elements'}
local eventSpells={[0]='Shadow Bolt',[1]='Corruption',[2]='Bane of Agony',[3]='Immolate',[4]='Incinerate',[5]='Searing Pain',[6]='Bane of Doom',[8]='Conflagrate',[9]='Shadowburn',[13]='Soul Fire',[15]='Siphon Life',[19]='Hellfire',[22]='Bane of Doom',[100]='Firebolt',[200]='Pet melee',[201]='Lash of Pain'}
function M.run(source,baselineSpv,probe,suite,work)
 local actual=M.instrument(source)
 local wrapped=iohelp.wrap(actual)
 local spv='artifacts/.cache/'..iohelp.hash(wrapped)..'.spv'
 if not iohelp.exists(spv) then
  iohelp.write(work..'/trace.comp',wrapped)
  iohelp.run('glslangValidator -V '..iohelp.quote(work..'/trace.comp')..' -o '..iohelp.quote(work..'/trace.spv')..' >/dev/null')
  iohelp.run('spirv-val '..iohelp.quote(work..'/trace.spv'))
  iohelp.run('mv '..iohelp.quote(work..'/trace.spv')..' '..iohelp.quote(spv))
 end
 local count=#suite.scenarios;local budget=0
 for _,s in ipairs(suite.scenarios) do budget=math.max(budget,s.duration*32+1024) end
 local params=ffi.new('uint32_t[6]',{count,1,suite.seed,0,count,budget})
 iohelp.write(work..'/trace-params.bin',ffi.string(params,ffi.sizeof(params)))
 ffi.C.setenv('FASTSIM_WARMUP_BATCHES','0',1)
 local function dispatch(shader,words,name)
  local command={probe,shader,work..'/configs.bin',work..'/trace-params.bin',work..'/'..name..'.bin',words,1}
  for i,v in ipairs(command) do command[i]=iohelp.quote(v) end
  iohelp.run(table.concat(command,' ')..' > '..iohelp.quote(work..'/'..name..'.json'))
  return iohelp.read(work..'/'..name..'.bin')
 end
 local baseline=dispatch(baselineSpv,163,'trace-reference')
 local bytes=dispatch(spv,M.words,'trace')
 assert(#baseline==count*163*4 and #bytes==count*M.words*4,'Incomplete trace output')
 local data=ffi.new('uint32_t[?]',count*M.words);ffi.copy(data,bytes,#bytes)
 local function float(word) local value=ffi.new('uint32_t[1]',word);local result=tonumber(ffi.cast('float*',value)[0]);assert(result==result and math.abs(result)~=math.huge,'Nonfinite timeline value');return result end
 local traces={}
 for c,scenario in ipairs(suite.scenarios) do
  local offset=(c-1)*M.words
  assert(bytes:sub((c-1)*M.words*4+1,(c-1)*M.words*4+163*4)==baseline:sub((c-1)*163*4+1,c*163*4),'Tracing changed the detailed fight report')
  assert(tonumber(data[offset+1])%65536==1,'Incomplete timeline fight')
  local stored,total=tonumber(data[offset+163]),tonumber(data[offset+164]);assert(stored==math.min(total,M.capacity),'Invalid trace header')
  local trace={candidateId=scenario.id,fightIndex=0,seed=suite.seed,capacity=M.capacity,recordCount=stored,totalRecords=total,truncated=total>stored,reportReplay='bit-identical',events={}}
  local previous=0
  for i=0,stored-1 do
   local base=offset+165+i*8
   local micros,packed=tonumber(data[base]),tonumber(data[base+1]);local kind=packed%256;local spell=math.floor(packed/256)%256
   assert(micros>=previous and micros<=scenario.duration*1000000,'Invalid timeline event ordering');previous=micros
   local label=''
   if kind==101 or kind==103 or kind==104 or kind==105 then label=spells[spell] or ('Spell '..spell)
   elseif kind==102 then label='Life Tap'
   elseif kind==100 or kind==1 or kind==2 or kind==3 or kind==6 then label=eventSpells[spell] or ('Spell '..spell) end
   trace.events[#trace.events+1]={time=micros/1000000,kind=kind,event=kinds[kind] or ('Event '..kind),spell=label,actor=kind==6 and 'Pet' or (kind==1 or kind==2 or kind==3 or kind>=100) and 'Player' or 'System',damage=float(data[base+2]),mana=float(data[base+3]),petMana=float(data[base+4]),totalDamage=float(data[base+5]),flags=tonumber(data[base+6]),casting=tonumber(data[base+7])~=0}
  end
  traces[#traces+1]=trace
 end
 return {schemaVersion=1,scope='First fight of each build; separate from aggregate averages',shaderHash=iohelp.hash(actual),capacity=M.capacity,candidates=traces}
end
return M
