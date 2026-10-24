local s=require('sim.classes.warlock.specialize')
local suite=require('data.validation.forever_baseline')
local helper=require('tools.native_support')
local layout=require('sim.classes.warlock.generated.layout')
for _,mode in ipairs({'fast','detailed'}) do
 local source=helper.read('sim/classes/warlock/generated/'..mode..'.glsl')
 local actual,features=s.specialize(source,suite.scenarios)
 assert(features.race==0 and features.petChoice==nil)
 assert(not actual:find('c.race',1,true))
 assert(actual:find('c.petChoice',1,true))
 assert(actual:find('APL_HEADER0_OFFSET+r',1,true))
 for _,entry in ipairs(s.CONFIG) do
  assert(entry[3]==layout.configTypes[entry[1]+1],'Configuration layout drift')
  if entry[3]=='f32' or entry[2]:find('^apl') then assert(features[entry[2]]==nil) end
 end
 assert(features['end']==nil)
 local candidates={}
 for i,c in ipairs(suite.scenarios) do candidates[i]={packed={}};for j,v in ipairs(c.packed) do candidates[i].packed[j]=v end end
 candidates[2].packed[12]=1
 local mixed,flags=s.specialize(source,candidates)
 assert(flags.race==nil and mixed:find('c.race',1,true))
 assert(not pcall(s.specialize,source,{}))
 assert(not pcall(s.specialize,source:gsub('c.race=%(getConfigWord','c.race=other(getConfigWord'),suite.scenarios))
 assert(not pcall(s.specialize,source..'\nvoid mutate(){c.race++;}\n',suite.scenarios))
end
print('PASS: native specialization, shared/mixed fields, runtime APLs and source-drift rejection')
