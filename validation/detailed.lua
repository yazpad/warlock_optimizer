local iohelp=require('tools.native_support')
local trace=require('tools.detailed_trace')
local analysis=require('tools.detailed_analysis')
local source=iohelp.read('sim/classes/warlock/generated/detailed.glsl')
local instrumented=trace.instrument(source)
assert(instrumented:find('void traceStore',1,true) and instrumented:find('lane*65701u+162u',1,true))
assert(not pcall(trace.instrument,iohelp.read('sim/classes/warlock/generated/fast.glsl')),'Fast source must not become a trace shader')
assert(not pcall(trace.instrument,source:gsub('void beginCast','void staleBeginCast')),'Trace source drift must reject')
local totals={total=200,damage0=120,casts0=4,hits0=4,crits0=2,misses0=0,ticks0=0,petMeleeDamage=40,petMeleeCasts=4,petMeleeHits=4,petMeleeCrits=0,petMeleeMisses=0,petSpellDamage=20,petSpellCasts=2,petSpellHits=2,petSpellCrits=0,petSpellMisses=0,petBrandDamage=20,petBrandCasts=2,petBrandHits=2,petBrandCrits=0,petBrandMisses=0,spent=80,gained=100,manaWasted=10,mana=20,taps=2}
for _,key in ipairs({'corr','agony','imm','siphon','doom','isb','trance','trinket','racial','decimation','snfShadow','snfFire'}) do totals['uptime_'..key]=key=='corr' and 1.5 or 0 end
local rows={{sampleCount=2,meanDps=10,totals=totals}}
local suite={scenarios={{id='test',duration=10}}}
local timeline={candidates={{candidateId='test',events={{time=0}},truncated=false}}}
local data=analysis.build(rows,suite,timeline).candidates[1]
assert(#data.damage==4 and data.damage[1].name=='Shadow Bolt' and data.damage[1].damage==60 and data.damage[1].dps==6 and data.damage[1].share==.6)
assert(data.damageReconciliation.difference==0 and data.uptimes[1].fraction==.75 and data.uptimes[1].seconds==7.5 and data.mana.taps==1)
totals.uptime_corr=3;assert(not pcall(analysis.build,rows,suite,timeline),'Out-of-range uptimes must reject')
print('PASS: detailed damage/pet reconciliation, uptime averages, trace-only instrumentation and source-drift rejection')
