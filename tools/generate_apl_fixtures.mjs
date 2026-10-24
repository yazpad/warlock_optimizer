// Independent expected VM words from the source JavaScript encoder.
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const upstream=path.resolve(process.argv[2]||'/home/aaron-ma/yazpads_warlock_sim');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'sim/classes/warlock/generated/manifest.json')));
const actualHash=createHash('sha256').update(fs.readFileSync(path.join(upstream,'threejs_webgl_des/src/model.js'))).digest('hex');
if(actualHash!==manifest.source.hashes['model.js']) throw Error('Source model differs from the combat snapshot; regenerate the snapshot deliberately first.');
const {encodeAPLRule,APL_ACTION:A,APL_COND:C}=await import(pathToFileURL(path.join(upstream,'threejs_webgl_des/src/model.js')));
const tests=[
 ['life_tap if mana_pct <= 25',{action:A.LIFE_TAP,cond:C.MANA_LE,param:25}],
 ['corruption if dot.corruption.remains <= 0 && target_ttd >= 12',{action:A.CORRUPTION,cond:C.DOT_REM_LE,targetSpell:1,cond2:C.FIGHT_TIME_GE,param2:12}],
 ['[off] blood_fury if true',{action:A.BLOOD_FURY,enabled:false}],
 ['immolate if dot.immolate.remains < 0.1',{action:A.IMMOLATE,cond:C.DOT_REM_LT,targetSpell:3,param:0.1}],
 ['siphon_life if dot.siphon_life.remains >= 1.25',{action:A.SIPHON_LIFE,cond:C.DOT_REM_GE,targetSpell:15,param:1.25}],
 ['bane_of_doom if dot.doom.remains > 0 && target_hp_pct < 35',{action:A.CURSE_OF_DOOM,cond:C.DOT_REM_GT,targetSpell:22,cond2:C.TARGET_HP_LT,param2:35}],
 ['shadow_bolt if pet_mana >= 300.5',{action:A.SHADOW_BOLT_FILLER,cond:C.PET_MANA_GE,param:300.5}],
 ['amplify_curse if !buff.amplify_curse',{action:A.AMPLIFY_CURSE,cond:C.AMPLIFY_INACTIVE}],
 ['curse_of_elements if !debuff.assigned_curse',{action:A.CURSE_OF_ELEMENTS,cond:C.ASSIGNED_CURSE_INACTIVE}],
 ['nightfall if buff.shadow_trance',{action:A.NIGHTFALL_SHADOW_BOLT,cond:C.SHADOW_TRANCE}],
];
const bits=n=>new Uint32Array(new Float32Array([n]).buffer)[0];
const lines=tests.map(([source,rule])=>{const e=encodeAPLRule(rule);return '{source='+JSON.stringify(source)+',words={'+[e.header0>>>0,bits(e.param0),e.header1>>>0,bits(e.param1)].join(',')+'}}';});
const numbers=[0,-0,0.1,25,1.25,-2.5,2**-149,2**-126,1+2**-24,1+3*2**-24,1e6];
const floats=numbers.map(n=>'{value='+(Object.is(n,-0)?'(-1/math.huge)':String(n))+',bits='+bits(n)+'}');
fs.writeFileSync(path.join(root,'validation/apl_fixtures.lua'),'-- Generated expected values from source encodeAPLRule / Float32Array.\nreturn {rules={'+lines.join(',\n')+'},floats={'+floats.join(',')+'}}\n');
console.log('Generated '+tests.length+' cross-language APL fixtures.');
