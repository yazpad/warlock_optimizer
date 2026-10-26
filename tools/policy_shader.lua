-- Opt-in policy replay/rollout shader. Production shaders and regret semantics stay unchanged.
local regret = require('tools.regret')
local M = {capacity=1024, depth=regret.depth, words=32}
-- target ordinal, fork seed, plan length, four actions, live seed, then committed history.
M.historyOffset = 8
M.stride = M.historyOffset + M.capacity
local function once(s, anchor, replacement)
  local at = assert(s:find(anchor, 1, true), 'Policy anchor missing: ' .. anchor)
  assert(not s:find(anchor, at + #anchor, true), 'Ambiguous policy anchor: ' .. anchor)
  return s:sub(1, at-1) .. replacement .. s:sub(at+#anchor)
end
function M.instrument(source)
  local derived = regret.instrument(source, true)
  -- Run the normal APL up to (but not including) its first consuming action.
  -- This executes only the off-GCD rows the APL would actually reach, with
  -- their original conditions/order. Forks and legality checks then see those
  -- effects, and both forced and unforced execution avoid repeating them.
  local start = assert(source:find('void decide(){', 1, true))
  local finish = assert(source:find('\nvoid creditManaRegen', start, true))
  local prepare = source:sub(start, finish-1)
  local prelude = assert(prepare:find(' float fightProgress=', 1, true))
  prepare = 'uint policyResumeRow=0u;\nvoid policyPrepare(){\n' .. prepare:sub(prelude)
  prepare = once(prepare, '  // Execute Action', '  policyResumeRow=r;\n  // Execute Action')
  -- Stop before the first side effect of each consuming action. Keep its
  -- original availability checks, including the Hellfire -> Life Tap fallback.
  local effects = {
    's.mana-=200.0;', 'tap();', 's.trance=0u;', 'beginCast(5u);',
    'beginCast(13u);', 'beginCast(1u);', 'castDoom();', 'beginCast(2u);',
    'beginCast(3u);', 'castConflagrate();', 'spend(10u);', 'spend(23u);',
    'spend(11u);', 'castShadowburn();', 'beginCast(4u);', 'beginCast(0u);',
  }
  local fallback = assert(prepare:find(' // Fallback', 1, true))
  prepare = prepare:sub(1, fallback-1) .. ' policyResumeRow=numAplRules;\n}\n'
  for _, effect in ipairs(effects) do
    local count
    prepare, count = prepare:gsub(effect:gsub('(%W)', '%%%1'), 'return;' .. effect)
    assert(count == (effect == 'tap();' and 2 or 1), 'Policy consuming action changed: ' .. effect)
  end
  derived = once(derived, 'void policyDecide(){', prepare .. '\nvoid policyDecide(){')
  derived = once(derived, 'for(uint r=0u;r<max(1u,numAplRules);r++){',
    'for(uint r=regretForced!=0u?0u:policyResumeRow;r<max(1u,numAplRules);r++){')
  derived = once(derived, ' uint ordinal=regretDecisions++;',
    ' policyPrepare();\n uint ordinal=regretDecisions++;')
  derived = once(derived, 'regretGlobal*' .. regret.stride .. 'u+n', 'regretGlobal*' .. M.stride .. 'u+n')
  derived = once(derived, 'seedRandom(0u);', 'seedRandom(control(7u));')
  -- Fingerprint every FightState field, the active event heap and RNG before forking.
  local body = assert(source:match('struct FightState%s*{(.-)};'))
  assert(not body:gsub('%w+%s+%w+%s*;', ''):find('%S'), 'Unsupported policy state layout')
  local hash = {'uvec2 policyHash(){uvec2 h=uvec2(2166136261u,2246822519u);uint w;'}
  local function word(expr)
    hash[#hash+1] = 'w=' .. expr .. ';h=(h^uvec2(w))*uvec2(16777619u,3266489917u);'
  end
  local fields = 0
  for kind, name in body:gmatch('(%w+)%s+(%w+)%s*;') do
    assert(kind == 'uint' or kind == 'float', 'Unsupported policy state field: ' .. kind)
    word(kind == 'float' and ('floatBitsToUint(s.' .. name .. ')') or ('s.' .. name))
    fields = fields + 1
  end
  assert(fields > 100, 'Incomplete policy state fingerprint')
  hash[#hash+1] = 'for(uint i=0u;i<s.size;i++){'
  word('queue[i].at'); word('queue[i].data'); hash[#hash+1] = '}'
  hash[#hash+1] = 'for(uint i=0u;i<4u;i++){'
  word('rng[i].x'); word('rng[i].y'); hash[#hash+1] = '}'
  word('floatBitsToUint(eventDamage)'); word('eventFlags')
  word('lastEvent.at'); word('lastEvent.data'); word('uint(dealingPeriodic)')
  hash[#hash+1] = 'return h;}'
  local helpers = [[
uint policyHistoryApplied=0u,policyRootMask=0u,policyRootApl=0u,policyRootDamage=0u;
uvec2 policySignature=uvec2(0u);
uint policyCanonical(uint a){return a==3u||a==5u?13u:(a==14u||a==15u?16u:a);}
]] .. table.concat(hash, '\n') .. '\n'
  derived = once(derived, 'uint regretMask(){', helpers .. 'uint regretMask(){')
  derived = once(derived,
    'if(control(0u)==0xffffffffu||ordinal>=control(0u))mask=regretMask();',
    'mask=regretMask();')
  derived = once(derived,
    'regretReached=1u;regretRootTime=at;regretRootMana=floatBitsToUint(mana);regretRootTrance=trance;',
    [[regretReached=1u;regretRootTime=at;regretRootMana=floatBitsToUint(mana);regretRootTrance=trance;
  policySignature=policyHash();policyRootMask=mask;policyRootDamage=floatBitsToUint(s.total);]])
  derived = once(derived, 'regretForced=0u;\n if(ordinal>=control(0u)', [[regretForced=0u;
 if(ordinal<control(0u)){
  uint a=control(8u+ordinal);
  if(a==0u)policyHistoryApplied++;
  else if((mask&(1u<<a))!=0u){regretForced=a;policyHistoryApplied++;}
  else regretInvalid++;
 }
 if(ordinal>=control(0u)]])
  derived = once(derived, ' policyDecide();\n regretForced=0u;', [[ policyDecide();
 if(regretForced!=0u&&policyCanonical(regretTaken)!=regretForced)regretInvalid++;
 if(ordinal==control(0u))policyRootApl=policyCanonical(regretTaken);
 regretForced=0u;]])
  local outputs = [[
 resultData[lane*32u+26u]=policyHistoryApplied;
 resultData[lane*32u+27u]=policySignature.x;
 resultData[lane*32u+28u]=policySignature.y;
 resultData[lane*32u+29u]=policyRootMask;
 resultData[lane*32u+30u]=policyRootApl;
 resultData[lane*32u+31u]=policyRootDamage;
]]
  derived = once(derived, ' resultData[lane*32u+25u]=s.now;', ' resultData[lane*32u+25u]=s.now;\n' .. outputs)
  return derived
end
return M
