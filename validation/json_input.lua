local decode=require('sim.common.json_input').decode
local value=decode(' {"scenarios":[{"id":"x\\n\\u2603\\ud83d\\ude80","packed":[0,4294967295],"enabled":true}],"float":-1.5e2,"flag":false} ')
assert(value.scenarios[1].packed[2]==4294967295 and value.float==-150 and value.flag==false)
assert(value.scenarios[1].id=='x\n\226\152\131\240\159\154\128')
for _,text in ipairs({'{"x":1,"x":2}','[1,]','{"x":1} extra','01','1.','1e','1e2e3','+1','"\\ud800"','null','"\n"','[NaN]'}) do assert(not pcall(decode,text),'Accepted invalid JSON: '..text) end
print('PASS: data-only JSON input, exact u32 words, Unicode and malformed-input rejection')
