-- Postprocessing for detailed jobs only. No work is added to fast evaluation.
local iohelp=require('tools.native_support')
local decoder=require('sim.common.json_input')
local M={}
local effects={{'Corruption','corr'},{'Bane of Agony','agony'},{'Immolate','imm'},{'Siphon Life','siphon'},{'Bane of Doom','doom'},{'Improved Shadow Bolt','isb'},{'Nightfall','trance'},{'Trinket','trinket'},{'Racial buff','racial'},{'Decimation','decimation'},{'Shadow and Flame: shadow','snfShadow'},{'Shadow and Flame: fire','snfFire'}}
function M.build(summaries,suite,timeline)
 local manifest=decoder.decode(iohelp.read('sim/classes/warlock/generated/manifest.json'))
 local result={schemaVersion=1,mode='detailed',scope='Damage and uptime averages across detailed fights; timeline is fight index 0 for each build',candidates={}}
 assert(#summaries==#suite.scenarios and #timeline.candidates==#suite.scenarios,'Detailed candidate mismatch')
 for index,row in ipairs(summaries) do
  local scenario=suite.scenarios[index];local totals=row.totals;local n=row.sampleCount
  assert(n>0 and scenario.duration>0,'Invalid detailed sample count')
  local total=totals.total
  local candidate={candidateId=scenario.id,fights=n,duration=scenario.duration,meanDps=row.meanDps,damage={},uptimes={},timeline=timeline.candidates[index]}
  local accounted=0
  local function add(name,damage,casts,hits,crits,misses,ticks)
   if damage==0 and casts==0 and hits==0 and ticks==0 then return end
   assert(damage>=0,'Invalid detailed damage')
   candidate.damage[#candidate.damage+1]={name=name,damage=damage/n,dps=damage/n/scenario.duration,share=total>0 and damage/total or 0,casts=casts/n,hits=hits/n,crits=crits/n,misses=misses/n,ticks=ticks/n}
   accounted=accounted+damage
  end
  for i,name in ipairs(manifest.spells) do local key=tostring(i-1);add(name,totals['damage'..key] or 0,totals['casts'..key] or 0,totals['hits'..key] or 0,totals['crits'..key] or 0,totals['misses'..key] or 0,totals['ticks'..key] or 0) end
  for _,pet in ipairs({{'Pet melee','petMelee'},{'Pet spells','petSpell'},{'Pet: Demonic Brand','petBrand'}}) do local prefix=pet[2];add(pet[1],totals[prefix..'Damage'] or 0,totals[prefix..'Casts'] or 0,totals[prefix..'Hits'] or 0,totals[prefix..'Crits'] or 0,totals[prefix..'Misses'] or 0,0) end
  table.sort(candidate.damage,function(a,b)return a.damage>b.damage end)
  candidate.damageReconciliation={total=total/n,accounted=accounted/n,difference=(total-accounted)/n}
  for _,effect in ipairs(effects) do
   local fraction=(assert(totals['uptime_'..effect[2]],'Missing detailed uptime')/n)
   assert(fraction>=0 and fraction<=1.000001,'Invalid detailed uptime')
   candidate.uptimes[#candidate.uptimes+1]={name=effect[1],fraction=fraction,seconds=fraction*scenario.duration}
  end
  candidate.mana={spent=totals.spent/n,gained=totals.gained/n,wasted=totals.manaWasted/n,remaining=totals.mana/n,taps=totals.taps/n}
  result.candidates[#result.candidates+1]=candidate
 end
 return result
end
return M
