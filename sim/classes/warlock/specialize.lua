local M = {}

local DYNAMIC_U32 = {
 ['end'] = true,
 corrCast = true,
 channelClipDelay = true,
 trinketDuration = true,
 trinketCD = true,
}

-- 0-based config word index, field name, and type ('u32' or 'f32') matching manifest.json
M.CONFIG = {
 {0,'externalElements','u32'},{1,'assignedElements','u32'},{2,'demonicRune','u32'},{3,'ezThroDynamite','u32'},
 {4,'manualTrinket','u32'},{5,'judgementOfWisdom','u32'},{6,'giftOfArthas','u32'},{7,'autoCooldowns','u32'},
 {8,'spiritRegen','f32'},{9,'petMaxMana','f32'},{10,'channelClipDelay','u32'},{11,'race','u32'},
 {12,'racialPolicy','u32'},{13,'targetIsBeast','u32'},{14,'basePower','f32'},{15,'maxHealth','f32'},
 {16,'end','u32'},{17,'corrCast','u32'},{18,'filler','u32'},{19,'corr','u32'},
 {20,'agony','u32'},{21,'immolate','u32'},{22,'baneRank','u32'},{23,'decimationRank','u32'},
 {24,'charges','u32'},{25,'partial','u32'},{26,'piercing','u32'},{27,'demonicPact','u32'},
 {28,'demonicSacrifice','u32'},{29,'felDomination','u32'},{30,'masterSummoner','u32'},{31,'soulLink','u32'},
 {32,'sacImp','u32'},{33,'sacSucc','u32'},{34,'masterDemo','u32'},{35,'petChoice','u32'},
 {36,'trinketDuration','u32'},{37,'trinketCD','u32'},{38,'conflagrate','u32'},{39,'shadowburn','u32'},
 {40,'curseOfDoom','u32'},{41,'incinerate','u32'},{42,'decimation','u32'},{43,'demonicBrand','u32'},
 {44,'demonicBrandRank','u32'},{45,'siphonLife','u32'},{46,'drainHope','u32'},{47,'hellfirePowerOffset','f32'},
 {48,'power','f32'},{49,'maxMana','f32'},{50,'tapGain','f32'},{51,'tapThreshold','f32'},
 {52,'mp5','f32'},{53,'petMP5','f32'},{54,'hit','f32'},{55,'crit','f32'},
 {56,'fireCrit','f32'},{57,'shadowCrit','f32'},{58,'directCrit','f32'},{59,'nightfall','f32'},
 {60,'isb','f32'},{61,'resistance','f32'},{62,'penetration','f32'},{63,'boltCost','f32'},
 {64,'boltMin','f32'},{65,'boltMax','f32'},{66,'corrCost','f32'},{67,'corrBase','f32'},
 {68,'corrMultiplier','f32'},{69,'immCost','f32'},{70,'immDirect','f32'},{71,'immTick','f32'},
 {72,'shadowMult','f32'},{73,'fireMult','f32'},{74,'shadowMasteryBonus','f32'},{75,'afBonus','f32'},
 {76,'aftermathBonus','f32'},{77,'maledictionBonus','f32'},{78,'improvedCorruptionBonus','f32'},{79,'improvedDrainsBonus','f32'},
 {80,'soulSiphonBonus','f32'},{81,'demonicEnergies','f32'},{82,'trinketSP','f32'},{83,'petSP','f32'},
 {84,'petAP','f32'},{85,'petOwnPower','f32'},{86,'petBaseAP','f32'},{87,'petPowerOffset','f32'},
 {88,'snfChance','f32'},{89,'snfBonus','f32'},{90,'dotCrit','f32'},{91,'fnbCrit','f32'},
 {92,'petMult','f32'},{93,'petFireboltMult','f32'},{94,'petMeleeMult','f32'},{95,'petLashMult','f32'},
 {96,'brandMult','f32'},{97,'cataclysmCostMult','f32'},{98,'improvedAgonyBonus','f32'},{99,'felVitalityBonus','f32'},
 {100,'amplifyCurse','u32'},
}
for i = 0, 31 do M.CONFIG[#M.CONFIG + 1] = {101 + i, 'aplHeader0_' .. i, 'u32'} end
for i = 0, 31 do M.CONFIG[#M.CONFIG + 1] = {133 + i, 'aplParam0_' .. i, 'f32'} end
for i = 0, 31 do M.CONFIG[#M.CONFIG + 1] = {165 + i, 'aplHeader1_' .. i, 'u32'} end
for i = 0, 31 do M.CONFIG[#M.CONFIG + 1] = {197 + i, 'aplParam1_' .. i, 'f32'} end

local function replacePlain(s, target, replacement)
 local startPos, endPos = s:find(target, 1, true)
 if not startPos then return s, 0 end
 local out = {}
 local pos = 1
 local count = 0
 while startPos do
  out[#out + 1] = s:sub(pos, startPos - 1)
  out[#out + 1] = replacement
  count = count + 1
  pos = endPos + 1
  startPos, endPos = s:find(target, pos, true)
 end
 out[#out + 1] = s:sub(pos)
 return table.concat(out), count
end

local function removeFromStruct(source, structName, fieldName)
 local sStart = source:find('struct ' .. structName .. ' {', 1, true)
 if not sStart then return source end
 local sEnd = source:find('};', sStart, true)
 if not sEnd then return source end
 local block = source:sub(sStart, sEnd + 1)
 block = block:gsub('([^{%w_])uint%s+' .. fieldName .. ';\n', '%1', 1)
 block = block:gsub('([^{%w_])float%s+' .. fieldName .. ';\n', '%1', 1)
 block = block:gsub('([^{%w_])uint%s+' .. fieldName .. ';', '%1', 1)
 block = block:gsub('([^{%w_])float%s+' .. fieldName .. ';', '%1', 1)
 return source:sub(1, sStart - 1) .. block .. source:sub(sEnd + 2)
end

local function optimizeSpecializedShader(source, features)
 local fastLoop = 'for(uint word=0u;word<16u;word++) resultData[lane*16u+word]=outputWord(word);'
 local isFast = source:find(fastLoop, 1, true) ~= nil
 if isFast then
  local unrolled = {}
  for w = 0, 15 do
   unrolled[#unrolled + 1] = ('resultData[lane*16u+%du]=fastWord(%du);'):format(w, w)
  end
  source = replacePlain(source, fastLoop, table.concat(unrolled, '\n '))
  source = replacePlain(
   source,
   's.rng0=rng[0].x;s.rng1=rng[0].y;\ns.rng2=rng[1].x;s.rng3=rng[1].y;\ns.rng4=rng[2].x;s.rng5=rng[2].y;\ns.rng6=rng[3].x;s.rng7=rng[3].y;\n',
   ''
  )
 end

 for name in pairs(features) do
  source = removeFromStruct(source, 'FightConfig', name)
 end

 for _, entry in ipairs(M.CONFIG) do
  local idx, name, kind = entry[1], entry[2], entry[3]
  if features[name] == nil then
   local initLine = kind == 'f32'
    and ('c.%s=uintBitsToFloat(getConfigWord(currentCfgIdx,%du));'):format(name, idx)
    or ('c.%s=(getConfigWord(currentCfgIdx,%du));'):format(name, idx)
   local withoutInit = replacePlain(source, initLine .. '\n', '')
   withoutInit = replacePlain(withoutInit, initLine, '')
   if not withoutInit:find('[^%w_]c%.' .. name .. '[^%w_]') then
    source = removeFromStruct(withoutInit, 'FightConfig', name)
   end
  end
 end

 source = replacePlain(
  source,
  '#define EV_KIND(e) ((e).data & 0xFFu)\n#define EV_SPELL(e) (((e).data >> 8u) & 0xFFu)\n#define EV_GEN(e) ((e).data >> 16u)\n#define MAKE_EVENT(at, kind, spell, gen) Event((at), ((kind) & 0xFFu) | (((spell) & 0xFFu) << 8u) | (((gen) & 0xFFFFu) << 16u))',
  '#define EV_PRIO(k) ((k)==13u?2u:(((k)==3u||(k)==14u)?0u:1u))\n#define EV_KIND(e) (((e).data >> 2u) & 0x3Fu)\n#define EV_SPELL(e) (((e).data >> 8u) & 0xFFu)\n#define EV_GEN(e) ((e).data >> 16u)\n#define MAKE_EVENT(at, kind, spell, gen) Event((at), (EV_PRIO(kind)) | (((kind) & 0x3Fu) << 2u) | (((spell) & 0xFFu) << 8u) | (((gen) & 0xFFFFu) << 16u))'
 )
 source = replacePlain(
  source,
  'bool eventBefore(Event a,Event b){\n uint ak=EV_KIND(a),bk=EV_KIND(b);\n uint ap=ak==13u?2u:((ak==3u||ak==14u)?0u:1u);\n uint bp=bk==13u?2u:((bk==3u||bk==14u)?0u:1u);\n return a.at<b.at||(a.at==b.at&&ap<bp);\n}',
  'bool eventBefore(Event a,Event b){\n return a.at<b.at||(a.at==b.at&&(a.data&3u)<(b.data&3u));\n}'
 )

 source = replacePlain(
  source,
  'void queueFirebolt(){\n if(s.petMana>=115.0){if(s.now+2000000u<=c.end)enqueue(s.now+2000000u,6u,100u,0u);}\n else{uint next=s.now+petManaWait(115.0);if(next<=c.end)enqueue(next,6u,102u,0u);}\n}',
  'void queueFirebolt(){\n bool ok=s.petMana>=115.0;uint next=s.now+(ok?2000000u:petManaWait(115.0));if(next<=c.end)enqueue(next,6u,ok?100u:102u,0u);\n}'
 )

 source = replacePlain(source, 'void advance(){', 'bool advance(){')
 source = replacePlain(source, 'if(s.size==0u){s.done=3u;return;}', 'if(s.size==0u){s.done=3u;return false;}')
 source = replacePlain(source, 'if(e.at<s.now){s.done=4u;return;}', 'if(e.at<s.now){s.done=4u;return false;}')
 source = replacePlain(source, 'decide();break;', 'return true;')
 source = replacePlain(source, 'default:s.done=5u;break;\n }', 'default:s.done=5u;break;\n }\n return false;')
 source = replacePlain(
  source,
  'decide();\n for(uint step=0u;step<eventBudget;step++){\n  if(s.done!=0u)break;advance();if(mode==2u&&s.events==lane+1u)break;\n }',
  'bool needDecide=true;\n for(uint step=0u;;step++){\n  if(needDecide)decide();\n  if(step>=eventBudget||s.done!=0u)break;\n  needDecide=advance();\n  if(mode==2u&&s.events==lane+1u)break;\n }'
 )

 local oldEval = [[  uint action=header0&0xFFu;
  uint cond1=(header0>>8u)&0xFFu;
  uint targetSpell1=(header0>>16u)&0xFFu;
  float param1=uintBitsToFloat(getConfigWord(currentCfgIdx,APL_PARAM0_OFFSET+r));

  if(!evalCond(cond1,param1,targetSpell1,playerManaPct,targetHpPct))continue;

  uint header1=getConfigWord(currentCfgIdx,APL_HEADER1_OFFSET+r);
  uint cond2=header1&0xFFu;
  uint targetSpell2=(header1>>8u)&0xFFu;
  float param2=uintBitsToFloat(getConfigWord(currentCfgIdx,APL_PARAM1_OFFSET+r));

  if(!evalCond(cond2,param2,targetSpell2,playerManaPct,targetHpPct))continue;]]
 local newEval = [[  uint action=header0&0xFFu;
  bool ok=true;
  for(uint ci=0u;ci<2u;ci++){
   uint hdr=ci==0u?header0:getConfigWord(currentCfgIdx,APL_HEADER1_OFFSET+r);
   uint cond=ci==0u?((hdr>>8u)&0xFFu):(hdr&0xFFu);
   if(cond==0u)continue;
   uint tSpell=ci==0u?((hdr>>16u)&0xFFu):((hdr>>8u)&0xFFu);
   float prm=uintBitsToFloat(getConfigWord(currentCfgIdx,(ci==0u?APL_PARAM0_OFFSET:APL_PARAM1_OFFSET)+r));
   if(!evalCond(cond,prm,tSpell,playerManaPct,targetHpPct)){ok=false;break;}
  }
  if(!ok)continue;]]
 source = replacePlain(source, oldEval, newEval)

 source = replacePlain(
  source,
  ' for(uint r=0u;r<numAplRules;r++){\n',
  ' uint chosenCast=255u;\n for(uint r=0u;r<numAplRules;r++){\n'
 )
 local fillerToken = features.filler ~= nil and (tostring(features.filler) .. 'u') or 'c.filler'
 source = replacePlain(
  source,
  (' // Fallback\n if(s.mana>=cost(%s)){beginCast(%s);return;}\n tap();'):format(fillerToken, fillerToken),
  (' if(chosenCast==255u&&s.mana>=cost(%s))chosenCast=%s;\n if(chosenCast!=255u){beginCast(chosenCast);return;}\n tap();'):format(fillerToken, fillerToken)
 )
 for _, sp in ipairs({'5u', '13u', '1u', '2u', '3u', '4u', '0u'}) do
  source = replacePlain(source, ('{beginCast(%s);return;}'):format(sp), ('{chosenCast=%s;break;}'):format(sp))
 end

 source = replacePlain(
  source,
  '   queueFirebolt();\n  }else if(espell==102u){queueFirebolt();\n  }else if(espell==200u){',
  '  }else if(espell==200u){'
 )
 source = replacePlain(
  source,
  '    delay=petManaWait(160.0);\n   }\n   if(s.now+delay<=c.end)enqueue(s.now+delay,6u,201u,0u);\n  }',
  '    delay=petManaWait(160.0);\n   }\n   if(s.now+delay<=c.end)enqueue(s.now+delay,6u,201u,0u);\n  }\n  if(espell==100u||espell==102u)queueFirebolt();'
 )

 if isFast then
  for _, gname in ipairs({'eventDamage', 'eventFlags', 'lastEvent'}) do
   source = source:gsub('([^%w_])' .. gname .. '%s*[+*/|^-]?=[^;]+;', '%1;')
   source = source:gsub('\nfloat ' .. gname .. ';\n', '\n')
   source = source:gsub('\nuint ' .. gname .. ';\n', '\n')
   source = source:gsub('\nEvent ' .. gname .. ';\n', '\n')
  end
  local sStart = source:find('struct FightState {', 1, true)
  local sEnd = sStart and source:find('};', sStart, true) or nil
  if sStart and sEnd then
   local stateBlock = source:sub(sStart, sEnd)
   local fields = {}
   for ftype, fname in stateBlock:gmatch('(%a+)%s+([%w_]+);') do
    if ftype == 'uint' or ftype == 'float' then
     fields[#fields + 1] = {ftype, fname}
    end
   end
   if features.amplifyCurse == 0 then
    source = replacePlain(source, 's.amplifyEnd=0u;', ';')
   end
   for _, item in ipairs(fields) do
    local ftype, fname = item[1], item[2]
    local initLine = ('s.%s=%s;'):format(fname, ftype == 'uint' and '0u' or '0.0')
    local withoutInit = replacePlain(source, initLine .. '\n', '')
    withoutInit = replacePlain(withoutInit, initLine, '')
    local mutated = withoutInit:find('[^%w_]s%.' .. fname .. '%s*[+*/%%&|^]?=[^=]')
     or withoutInit:find('[^%w_]s%.' .. fname .. '%+%+')
     or withoutInit:find('[^%w_]s%.' .. fname .. '%-%-')
    if not mutated then
     local zeroLit = ftype == 'uint' and '0u' or '0.0'
     source = withoutInit:gsub('([^%w_])s%.' .. fname .. '([^%w_])', '%1' .. zeroLit .. '%2')
     source = removeFromStruct(source, 'FightState', fname)
    end
   end
  end
 end

 return source
end

function M.specialize(source, scenarios)
 assert(type(scenarios) == 'table' and #scenarios > 0, 'Specialization requires candidates')
 local features = {}
 for _, entry in ipairs(M.CONFIG) do
  local idx, name, kind = entry[1], entry[2], entry[3]
  if kind == 'u32' and not name:find('^apl') and not DYNAMIC_U32[name] then
   local value = scenarios[1].packed[idx + 1]
   local shared = true
   for i = 2, #scenarios do
    if scenarios[i].packed[idx + 1] ~= value then
     shared = false
     break
    end
   end
   if shared then
    local initLine = ('c.%s=(getConfigWord(currentCfgIdx,%du));'):format(name, idx)
    local updated, count = replacePlain(source, '\n ' .. initLine, '')
    if count == 0 then
     updated, count = replacePlain(source, '\n' .. initLine, '')
    end
    assert(count == 1, 'Feature initializer changed: ' .. name)
    assert(not updated:find('c%.' .. name .. '%s*[%+%-*/%%&|^]?=[^=]')
     and not updated:find('c%.' .. name .. '%s*%+%+')
     and not updated:find('c%.' .. name .. '%s*%-%-'), 'Mutable feature: ' .. name)
    source = updated
    source = source:gsub('([^%w_])c%.' .. name .. '([^%w_])', '%1' .. tostring(value) .. 'u%2')
    features[name] = value
   end
  end
 end
 source = optimizeSpecializedShader(source, features)
 return source, features
end

return M
