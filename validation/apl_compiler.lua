local C=require('policies.apl.compiler')
local vm=require('sim.classes.warlock.generated.apl')
local layout=require('sim.classes.warlock.generated.layout')
local words=require('sim.common.words')
local fixtures=require('validation.apl_fixtures')
local example=require('sim.classes.warlock.generated.example')
for _,fixture in ipairs(fixtures.rules) do
 local policy=C.compile(fixture.source)
 local rule=policy.packed[1]
 assert(rule.header0==fixture.words[1] and rule.param0==fixture.words[2] and rule.header1==fixture.words[3] and rule.param1==fixture.words[4],fixture.source..' differs from source encoder')
end
for _,fixture in ipairs(fixtures.floats) do assert(words.floatBits(fixture.value)==fixture.bits,'Float packing mismatch: '..fixture.value) end
local all=C.fromLabels({'Life Tap','Corruption','Shadow Bolt'})
local attached=C.attach(example.packed,all)
assert(#attached==layout.configWords and example.packed[vm.header0]~=attached[vm.header0+1],'Attaching policy must not mutate input')
for i=1,layout.configWords do
 local changed=false
 for _,offset in ipairs({vm.header0,vm.param0,vm.header1,vm.param1}) do if i>=offset and i<offset+vm.maxRules then changed=true end end
 if not changed then assert(attached[i]==example.packed[i],'Policy changed combat configuration') end
end
local disabled=C.compile('[off] blood_fury\nshadow_bolt')
assert(disabled.packed[1].header0>0 and disabled.packed[1].header0<2^24,'Disabled rows must remain nonzero to preserve VM row scanning')
local short=C.attach(attached,C.compile('shadow_bolt'))
for i=1,vm.maxRules-1 do assert(short[vm.header0+i]==0 and short[vm.param0+i]==0 and short[vm.header1+i]==0 and short[vm.param1+i]==0,'Old APL rows must be cleared') end
assert(#C.compile('life_tap').warnings==1)
local full={};for _=1,vm.maxRules do full[#full+1]='shadow_bolt' end
assert(#C.compile(table.concat(full,'\n')).rows==vm.maxRules)
for _,source in ipairs({'','drain_soul','drain_life','unknown','shadow_bolt if unsupported < 1','life_tap if mana_pct > 101','shadow_bolt if target_ttd < -1','shadow_bolt if true || true','shadow_bolt if true && true && true','corruption if dot.wrong.remains <= 0',table.concat(full,'\n')..'\nshadow_bolt'}) do
 assert(not pcall(C.compile,source),'Must reject '..source)
end
for _,fixture in ipairs(fixtures.rules) do
 local a=C.compile(fixture.source);local b=C.compile(C.format(a.rows))
 assert(a.packed[1].header0==b.packed[1].header0 and a.packed[1].param0==b.packed[1].param0,'Policy round trip changed VM data')
end
for _,preset in ipairs(require('sim.classes.warlock.presets')) do assert(#C.fromLabels(preset.actions).rows==#preset.actions) end
print('PASS: source-encoder APL agreement, float32 packing, disabled rows, policy attachment, stale-row clearing and invalid policy rejection')
