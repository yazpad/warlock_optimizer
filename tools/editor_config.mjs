// GUI and CLI share this preparation boundary. No work here runs per fight.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {DEFAULTS,packConfig,CONFIG,MAX_APL_RULES} from '../policies/warlock/source/model.js';
import {buildFightConfig} from '../policies/warlock/source/config_builder.js';
import {TALENT_DEFINITIONS,TalentGraph} from '../policies/warlock/source/talent_graph.js';
import {getTalentFlagsFromRanks} from '../policies/warlock/source/talents.js';
import {resolveWarlockBuffEffects} from '../policies/warlock/source/buffs.js';
import {addWarlockBaseStats,warlockBaseResources} from '../policies/warlock/source/base_stats.js';
import {convertForeverBuildToCandidate} from '../policies/warlock/source/forever_bridge.js';
import {parseAPLText,compileAPLToBytecode,ACTION_INFO} from '../policies/warlock/source/apl.js';
export const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const read=p=>JSON.parse(fs.readFileSync(path.join(root,p),'utf8'));
const frozen=read('data/validation/forever_baseline.json');
const manifest=read('sim/classes/warlock/generated/manifest.json');
const hash=s=>createHash('sha256').update(s).digest('hex');
const buffKeys=['giftOfTheWild','greaterBlessingOfWisdom','improvedManaSpringTotem','prayerOfFortitude','prayerOfSpirit','moonkinAura','judgementOfWisdom','sunderArmor','exposeArmor','faerieFire','giftOfArthas','flaskSupremePower','greaterArcaneElixir','shadowPowerElixir','greaterFirepowerElixir','wizardOil','magebloodElixir','nightfinSoup','arcaneIntellect','markOfTheWild','blessingOfKings','blessingOfWisdom','manaSpringTotem','dragonslayer','songflower','warchiefsBlessing','spiritOfZandalar','saygesFortune','curseOfShadow','curseOfElements','shadowWeaving','improvedScorch','nightfallProcDebuff'];
const trees=['affliction','demonology','destruction'];
const check=(ok,message)=>{if(!ok)throw Error(message);};
export function validateTalents(talents) {
 check(talents && typeof talents==='object','Missing talents');
 for(const [tree,ranks] of Object.entries(talents)) {
  check(trees.includes(tree) && ranks && typeof ranks==='object' && !Array.isArray(ranks),'Unsupported talent tree: '+tree);
  for(const [id,rank] of Object.entries(ranks)) {
   const def=TALENT_DEFINITIONS.find(d=>d.id===id && trees[d.tree]===tree);
   check(def && Number.isInteger(rank) && rank>=0 && rank<=def.max,'Invalid talent rank: '+id);
  }
 }
 const vector=TalentGraph.fromTalentsObject(talents), total=TalentGraph.countTotalPoints(vector);
 check(total<=51 && TalentGraph.isValid(vector,total),'Invalid talents: check row requirements, prerequisites and 51-point limit');
}
export function resolveCandidate(c) {
 check(c?.schemaVersion===1 && c.classId==='warlock','Unsupported candidate format');
 check(Object.keys(c).every(k=>['schemaVersion','classId','candidateId','autoCooldowns','importDiagnostics','encounter','stats','equipment','buffs','talents','apl','classOptions','sourceSettings','externalElements','assignedElements','demonicRune','ezThroDynamite','manualTrinket','channelClipDelayMs','book','trinketSP','trinketDuration','trinketCD'].includes(k)),'Unsupported candidate field');
 check(Object.keys(c.classOptions).every(k=>['race','pet','sacrifice','rotation'].includes(k)),'Unsupported class option');
 check(Object.keys(c.encounter).every(k=>['duration','target'].includes(k)),'Unsupported encounter field');
 check(Object.keys(c.encounter.target).every(k=>['level','targetCount','targetHP','bossArmor','resistance','racialPolicy','targetIsBeast'].includes(k)),'Unsupported target setting');
 check(typeof c.candidateId==='string' && c.candidateId.length>0 && c.candidateId.length<=128,'Invalid candidate identity');
 check(typeof c.apl?.text==='string' && c.apl.text.length<=8192,'APL must be text, at most 8192 characters');
 check(!c.sourceSettings?.armor || c.sourceSettings.armor==='NoArmor','Armor effects are not supported');
 check(!c.importDiagnostics?.length,'Import has unsupported APL: '+JSON.stringify(c.importDiagnostics));
 check(c.equipment?.mode==='direct' && !Object.keys(c.equipment.items).length,'Equipped gear is not supported yet; export direct bonus stats');
 validateTalents(c.talents);
 const o=c.classOptions;
 check(['HUMAN','GNOME','ORC','TROLL','UNDEAD'].includes(o.race),'Unsupported race');
 check(['none','imp','succubus'].includes(o.pet) && ['none','imp','succubus'].includes(o.sacrifice),'Unsupported pet or sacrifice');
 check(['shadow','fire','searing','bolt'].includes(o.rotation),'Unsupported rotation');
 check(Object.values(c.stats).every(v=>typeof v==='boolean'||(typeof v==='number'&&Number.isFinite(v))),'Invalid stats');
 check(Object.keys(c.stats).every(k=>['swordEquipped','spellPower','shadowPower','firePower','intellect','stamina','spirit','hit','crit','mp5','penetration'].includes(k)),'Unsupported stat');
 check(Object.entries(c.buffs).every(([k,v])=>buffKeys.includes(k)&&typeof v==='boolean'),'Unsupported buff selection');
 const target=c.encounter.target;
 check((target.level??63)===63 && (target.targetCount??1)===1,'Only one level-63 target is supported');
 const flags=getTalentFlagsFromRanks(c.talents), rows=parseAPLText(c.apl.text);
 check(rows.length<=MAX_APL_RULES,'APL exceeds 32 actions');
 const requirements={nightfall:flags.nightfall,decimateSearing:flags.decimation,decimateSoulFire:flags.decimation,brand:flags.demonicBrand,conflag:flags.conflagrate,shadowburn:flags.shadowburn,incinerate:flags.incinerate,siphon:flags.siphonLife,wrack:flags.wrack,amplify:flags.amplifyCurse,petSwap:flags.felDomination&&flags.sacRank>0};
 for(const row of rows) {
  check(row.id!=='drain','Drain Soul is not implemented by the current kernel');
  check(!row.enabled || requirements[row.id]!==false,'Action requires a talent: '+row.spell);
 }
 const stats=addWarlockBaseStats(c.stats,o.race);
 const {haste,maxMana,...effective}=resolveWarlockBuffEffects(stats,c.buffs,o.race,target,o.pet);
 Object.assign(effective,warlockBaseResources(effective.intellect,effective.stamina));
 if(c.buffs.warchiefsBlessing)effective.maxHealth+=300;
 const config=buildFightConfig({base:{...c.stats,...effective,race:o.race,statsIncludeBase:true,
  ...Object.fromEntries(['externalElements','assignedElements','demonicRune','ezThroDynamite','manualTrinket','autoCooldowns','channelClipDelayMs','book','trinketSP','trinketDuration','trinketCD'].filter(k=>Object.hasOwn(c,k)).map(k=>[k,c[k]])),
  duration:c.encounter.duration,racialPolicy:target.racialPolicy??DEFAULTS.racialPolicy,targetIsBeast:target.targetIsBeast??false},
  talent:flags,pet:o.pet,sac:o.sacrifice,actionIds:rows.filter(r=>r.enabled).map(r=>r.id),aplRules:compileAPLToBytecode(rows),rotation:o.rotation});
 check(JSON.stringify(Object.entries(CONFIG))===JSON.stringify(manifest.config),'Packing layout differs from the pinned engine');
 check(hash(fs.readFileSync(path.join(root,'policies/warlock/source/model.js')))===manifest.source.hashes['model.js'],'Configuration model differs from pinned engine');
 return {config,rows,packed:Array.from(packConfig(config))};
}
// This adapter targets the local frozen Forever/Classic export schema, not Retail/SoD.
export function importBuild(payload) {
 check(payload && typeof payload==='object' && !Array.isArray(payload),'Paste a JSON export');
 if(payload.schemaVersion===1 && payload.classId==='warlock' && payload.apl?.text) { resolveCandidate(payload); return payload; }
 check(payload.player && typeof payload.player==='object','Expected a WoWSims/Forever JSON export with player');
 check(payload.player.class==='ClassWarlock' || payload.player.class===9,'Only Warlock exports are supported');
 check(payload.apiVersion===2 && (!payload.player.apiVersion || payload.player.apiVersion===2),'Unsupported export version; expected Forever apiVersion 2');
 check(typeof payload.player.talentsString==='string' && /^[0-5]*-[0-5]*-[0-5]*$/.test(payload.player.talentsString),'Invalid talents string');
 payload.player.talentsString.split('-').forEach((part,tree)=>{
  const defs=TALENT_DEFINITIONS.filter(d=>d.tree===tree);
  check(part.length<=defs.length && [...part].every((v,i)=>Number(v)<=defs[i].max),'Talent string does not match the current talent tree');
 });
 check(!payload.player.equipment?.items?.some(i=>i?.id),'Equipped gear cannot be resolved yet. Import requires direct bonus stats');
 check(!payload.player.rotation?.prepullActions?.length,'Prepull actions are not supported');
 check(!payload.player.itemSwap && !payload.player.cooldowns?.cooldowns?.length,'Item swaps and custom cooldown schedules are not supported');
 const active=v=>Array.isArray(v)?v.some(active):v&&typeof v==='object'?Object.values(v).some(active):v!==null&&v!==undefined&&v!==false&&v!==0&&v!==''&&!['TristateEffectMissing','ProfessionUnknown'].includes(v);
 const audit=(value,allowed,label)=>{for(const [k,v] of Object.entries(value||{}))check(allowed.includes(k)||!active(v),'Unsupported '+label+': '+k);};
 audit(payload,['apiVersion','player','raidBuffs','partyBuffs','debuffs','encounter','simOptions','tanks','targetDummies'], 'export field');
 audit(payload.raidBuffs,['arcaneBrilliance','giftOfTheWild','prayerOfFortitude','prayerOfSpirit','moonkinAura'],'raid buff');
 audit(payload.partyBuffs,['manaSpringTotem'],'party buff');
 audit(payload.debuffs,['judgementOfWisdom','sunderArmor','exposeArmor','faerieFire','giftOfArthas','curseOfElements','curseOfShadow','shadowWeaving','improvedScorch'],'debuff');
 audit(payload.player.buffs,['greaterBlessingOfKings','blessingOfKings','greaterBlessingOfWisdom','blessingOfWisdom','rallyingCryOfTheDragonslayer','songflowerSerenade','warchiefsBlessing','spiritOfZandalar','saygesFortune'],'player buff');
 audit(payload.player,['apiVersion','name','race','class','consumables','buffs','warlock','talentsString','profession1','profession2','rotation','bonusStats','equipment','channelClipDelayMs','enableItemSwap','itemSwap','cooldowns'],'player field');
 check(['RaceHuman','RaceGnome','RaceOrc','RaceUndead','RaceTroll'].includes(payload.player.race),'Unsupported export race');
 const options=payload.player.warlock?.options?.classOptions||{};
 audit(options,['summon','armor','curseOptions','sacrificedDemon','soulShards'],'class option');
 check(!options.armor||options.armor==='NoArmor','Armor effects are not supported by this importer');
 check(!options.summon||['NoSummon','Imp','Succubus'].includes(options.summon),'Unsupported summon');
 check(!options.sacrificedDemon||['NoSummon','Imp','Succubus'].includes(options.sacrificedDemon),'Unsupported sacrificed demon');
 check(!options.curseOptions||['NoCurse','Elements'].includes(options.curseOptions),'Unsupported assigned curse');
 const consumes=payload.player.consumables||{};
 audit(consumes,['flaskId','foodId','conjuredId','mhImbueId','offensivePotId','explosiveId','shadowPowerElixir','greaterFirepowerElixir','wizardOil','magebloodElixir'],'consumable');
 for(const [key,ids] of Object.entries({flaskId:[0,13512],foodId:[0,13931],conjuredId:[0,12662],mhImbueId:[0,20749],offensivePotId:[0,250937,9155,9264,21546],explosiveId:[0,18588]}))check(consumes[key]==null||ids.includes(consumes[key]),'Unsupported consumable: '+key);
 check(!payload.encounter?.durationVariation && !payload.encounter?.useHealth,'Variable-duration encounters are unsupported');
 check(!payload.encounter?.targets || payload.encounter.targets.length===1,'Only one target is supported');
 check(!payload.encounter?.targets?.[0]?.level || payload.encounter.targets[0].level===63,'Only level-63 targets are supported');
 for(const container of [payload.raidBuffs,payload.player.buffs,payload.debuffs])for(const [key,value] of Object.entries(container||{}))if(active(value))check(typeof value==='boolean','Unsupported buff encoding: '+key);
 check(!payload.partyBuffs?.manaSpringTotem||['TristateEffectMissing','TristateEffectRegular','TristateEffectImproved',0,1,2].includes(payload.partyBuffs.manaSpringTotem),'Unsupported Mana Spring encoding');
 const build=structuredClone(payload);
 const c=convertForeverBuildToCandidate(build);
 check(c.apl?.text,'No supported APL found');
 resolveCandidate(c);
 return c;
}
export function catalog() {
 const presets=frozen.scenarios.map(s=>{
  // Editable imports from local reference requests; frozen packed benchmarks stay separate.
  const r=s.referenceRequest;
  const build=r.player?r:{...r,player:r.raid.parties[0].players[0],raidBuffs:r.raid.buffs,partyBuffs:r.raid.parties[0].buffs,debuffs:r.raid.debuffs};
  const candidate=convertForeverBuildToCandidate(build,{candidateId:s.preset});
  return {id:s.preset,name:({sm_ruin:'Shadow Mastery / Ruin',aff_wrack:'Affliction / Wrack',destro:'Destruction',demo_shadow_brand:'Demo / Shadow Brand',demo_fire_brand:'Demo / Fire Brand'})[s.preset],candidate};
 });
 return {buffKeys,talents:TALENT_DEFINITIONS,actions:Object.values(ACTION_INFO).filter(a=>a.id!=='drain'),presets};
}
export function prepare(candidate) {
 const {config,rows,packed}=resolveCandidate(candidate);
 for(const [p,expected] of Object.entries(frozen.kernelHashes))check(hash(fs.readFileSync(path.join(root,p)))===expected,'Stale pinned kernel: '+p);
 const suite={schemaVersion:1,id:'editor-warlock-v1',seed:101,kernelHashes:frozen.kernelHashes,scenarios:[{id:candidate.candidateId||'custom',duration:config.duration,packed}]};
 const suiteSource=JSON.stringify(suite);
 return {suiteSource,suiteHash:hash(suiteSource),rows:rows.map(r=>({spell:r.spell,icon:r.icon})),candidate};
}
if(process.argv[1] && path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
 try {
  const [command,input,output]=process.argv.slice(2);
  const payload=input && command!=='catalog'?JSON.parse(fs.readFileSync(input,'utf8')):null;
  const result=command==='catalog'?catalog():command==='import'?importBuild(payload):command==='prepare'?prepare(payload):(()=>{throw Error('Expected catalog, import or prepare');})();
  fs.writeFileSync(output,JSON.stringify({ok:true,value:result}));
 }catch(error){if(process.argv[4])fs.writeFileSync(process.argv[4],JSON.stringify({ok:false,error:error.message}));else throw error; process.exitCode=1;}
}
