local vm=require('sim.classes.warlock.generated.apl')
local registry=require('policies.apl.warlock')
local words=require('sim.common.words')
local C={}
local function trim(text) return text:match('^%s*(.-)%s*$') end
local comparisons={mana_pct={['<=']='MANA_LE',['>=']='MANA_GE',['<']='MANA_LT',['>']='MANA_GT'},target_hp_pct={['<=']='TARGET_HP_LE',['>=']='TARGET_HP_GE',['<']='TARGET_HP_LT',['>']='TARGET_HP_GT'},target_ttd={['<=']='FIGHT_TIME_LE',['>=']='FIGHT_TIME_GE',['<']='FIGHT_TIME_LT',['>']='FIGHT_TIME_GT'},pet_mana={['<=']='PET_MANA_LE',['>=']='PET_MANA_GE',['<']='PET_MANA_LT',['>']='PET_MANA_GT'}}
local booleans={['true']='ALWAYS',['buff.shadow_trance']='SHADOW_TRANCE',['debuff.demonic_brand_missing']='DEMONIC_BRAND_MISSING',['buff.amplify_curse']='AMPLIFY_ACTIVE',['!buff.amplify_curse']='AMPLIFY_INACTIVE',['debuff.assigned_curse']='ASSIGNED_CURSE_ACTIVE',['!debuff.assigned_curse']='ASSIGNED_CURSE_INACTIVE'}
local function condition(text)
 text=trim(text)
 if booleans[text] then return {id=assert(vm.conditions[booleans[text]]),param=0,target=0,text=text} end
 local field,operator,raw=text:match('^([%w_%.]+)%s*([<>]=?)%s*([%+%-]?[%d%.]+)$')
 assert(field,'Unsupported condition: '..text)
 local param=tonumber(raw)
 assert(param and param==param and param>=0 and param<=1000000,'Invalid condition parameter: '..tostring(raw))
 local key=comparisons[field] and comparisons[field][operator]
 local target=0
 if field=='mana_pct' or field=='target_hp_pct' then assert(param<=100,'Percentage must be 0–100') end
 local dot=field:match('^dot%.([%w_]+)%.remains$')
 if dot then
  target=assert(registry.dots[dot],'Unknown DoT: '..dot)
  key=({['<=']='DOT_REM_LE',['<']='DOT_REM_LT',['>=']='DOT_REM_GE',['>']='DOT_REM_GT'})[operator]
 end
 assert(key,'Unsupported condition field: '..field)
 return {id=assert(vm.conditions[key],'Missing VM condition '..key),param=param,target=target,text=text}
end
local function predicates(text)
 assert(not text:find('||',1,true),'OR expressions are not supported by this compiler')
 local result={};local rest=text
 while true do
  local at=rest:find('&&',1,true)
  result[#result+1]=condition(at and rest:sub(1,at-1) or rest)
  if not at then break end
  rest=rest:sub(at+2)
 end
 assert(#result<=2,'At most two AND conditions are supported per row')
 return result
end
function C.compile(source)
 assert(type(source)=='string' and #source<=8192,'APL source must be at most 8192 bytes')
 local policy={schemaVersion=1,classId='warlock',kind='apl',rows={},source=source,warnings={}}
 local line=0
 for text in (source..'\n'):gmatch('(.-)\n') do
  line=line+1;text=trim(text)
  if text~='' and text:sub(1,2)~='--' then
   local ok,result=pcall(function()
    local enabled=true
    if text:sub(1,6)=='[off] ' then enabled=false;text=trim(text:sub(7)) end
    local action,expression=text:match('^([%w_]+)%s+if%s+(.+)$')
    action=action or text
    local info=assert(registry.actions[action],'Unsupported Warlock action: '..action)
    return {action=action,label=info.label,actionId=info.id,enabled=enabled,conditions=predicates(expression or 'true'),line=line}
   end)
   assert(ok,('APL line %d: %s'):format(line,tostring(result)))
   policy.rows[#policy.rows+1]=result
  end
 end
 assert(#policy.rows>0 and #policy.rows<=vm.maxRules,('APL must contain 1–%d rows'):format(vm.maxRules))
 for i,row in ipairs(policy.rows) do
  if row.action=='life_tap' and row.enabled and #row.conditions==1 and row.conditions[1].id==vm.conditions.ALWAYS then policy.warnings[#policy.warnings+1]='Unconditional Life Tap can prevent all lower-priority actions (row '..i..').' end
 end
 policy.packed={}
 for i,row in ipairs(policy.rows) do
  local first=row.conditions[1];local second=row.conditions[2] or {id=0,param=0,target=0}
  policy.packed[i]={header0=(row.enabled and 2^24 or 0)+first.target*2^16+first.id*256+row.actionId,
   param0=words.floatBits(first.param),header1=second.target*256+second.id,param1=words.floatBits(second.param)}
 end
 return policy
end
function C.attach(packed,policy)
 assert(type(packed)=='table' and #packed==require('sim.classes.warlock.generated.layout').configWords,'Invalid packed configuration')
 -- Recompile source instead of trusting mutable cached policy bytecode.
 local verified=C.compile(assert(policy.source,'Expected compiled policy source'))
 local result={};for i,value in ipairs(packed) do result[i]=value end
 for i=0,vm.maxRules-1 do
  local rule=verified.packed[i+1] or {header0=0,param0=0,header1=0,param1=0}
  result[vm.header0+i]=rule.header0;result[vm.param0+i]=rule.param0;result[vm.header1+i]=rule.header1;result[vm.param1+i]=rule.param1
 end
 return result
end
function C.format(rows)
 local lines={}
 for _,row in ipairs(rows) do
  local parts={};for _,c in ipairs(row.conditions) do parts[#parts+1]=c.text end
  lines[#lines+1]=(row.enabled and '' or '[off] ')..row.action..' if '..table.concat(parts,' && ')
 end
 return table.concat(lines,'\n')
end
function C.fromLabels(labels)
 local lines={}
 for _,label in ipairs(labels) do local action=assert(registry.byLabel[label],'Unknown action label: '..label);lines[#lines+1]=action.name..' if '..action.defaultCondition end
 return C.compile(table.concat(lines,'\n'))
end
return C
