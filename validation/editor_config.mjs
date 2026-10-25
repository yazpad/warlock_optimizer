import assert from 'node:assert/strict';
import fs from 'node:fs';
import {createHash} from 'node:crypto';
import {catalog,prepare,importBuild,resolveCandidate,validateTalents} from '../tools/editor_config.mjs';
const presets=catalog().presets;
assert.equal(presets.length,5);
for(const p of presets){
 const first=prepare(p.candidate), second=prepare(importBuild(JSON.parse(JSON.stringify(p.candidate))));
 assert.equal(first.suiteSource,second.suiteSource,'Save/import must preserve executable input');
 assert.equal(first.suiteHash,createHash('sha256').update(first.suiteSource).digest('hex'));
 const suite=JSON.parse(first.suiteSource);assert.equal(suite.scenarios[0].packed.length,229);
 assert.ok(suite.scenarios[0].packed.every(w=>Number.isInteger(w)&&w>=0&&w<=0xffffffff));
}
const original=presets[0].candidate, originalSource=prepare(original).suiteSource;
const changed=structuredClone(original);changed.stats.spellPower+=100;
assert.notEqual(prepare(changed).suiteSource,originalSource);
assert.equal(prepare(original).suiteSource,originalSource,'Editing a copy must not mutate the preset');
const simple=structuredClone(original);simple.apl.text='tap if mana_pct <= 25\nbolt';
assert.equal(prepare(simple).rows.length,2);
const invalid=structuredClone(simple);invalid.apl.text='tap\ninvalid_spell';
assert.throws(()=>prepare(invalid),/Line 2/);
invalid.apl.text=Array(33).fill('bolt').join('\n');assert.throws(()=>prepare(invalid),/32/);
invalid.apl.text='drain';assert.throws(()=>prepare(invalid),/not implemented/);
invalid.apl.text='wrack';assert.throws(()=>prepare(invalid),/requires a talent/);
assert.throws(()=>validateTalents({affliction:{nightfall:3}}),/Invalid talent rank/);
assert.throws(()=>validateTalents({affliction:{wrack:1}}),/prerequisites/);
assert.throws(()=>validateTalents({affliction:{made_up:1}}),/Invalid talent rank/);
const invalidBuff=structuredClone(simple);invalidBuff.buffs.bloodlust=true;assert.throws(()=>prepare(invalidBuff),/buff/);
const invalidStat=structuredClone(simple);invalidStat.stats.spellPower=Infinity;assert.throws(()=>prepare(invalidStat),/Invalid stats/);
// A supported direct-stat JSON export in the local Forever schema.
const input={apiVersion:2,player:{apiVersion:2,name:'Fixture',class:'ClassWarlock',race:'RaceHuman',
 talentsString:'2540002013520105--0550305003',bonusStats:{stats:[],pseudoStats:[]},equipment:{items:[]},
 warlock:{options:{classOptions:{summon:'Succubus',armor:'NoArmor',sacrificedDemon:'NoSummon',curseOptions:'NoCurse'}}},
 rotation:{type:'TypeAPL',priorityList:[{action:{castSpell:{spellId:{spellId:11689}},condition:{cmp:{op:'OpLe',lhs:{currentManaPercent:{}},rhs:{const:{val:'25%'}}}}}},{action:{castSpell:{spellId:{spellId:25307}}}}]}},encounter:{duration:180,targets:[{level:63}]}};
const imported=importBuild(input);assert.equal(imported.classOptions.race,'HUMAN');assert.ok(prepare(imported).rows.length>=2);
const withGear=structuredClone(input);withGear.player.equipment.items=[{id:19379},{id:19682,enchant:2504}];
const importedWithGear=importBuild(withGear);
assert.equal(importedWithGear.stats.spellPower, 44 + 27 + 30);
assert.equal(importedWithGear.stats.hit, 4.0);
const bad=structuredClone(input);bad.player.class='ClassMage';assert.throws(()=>importBuild(bad),/Warlock/);
bad.player.class='ClassWarlock';bad.raidBuffs={bloodlust:true};assert.throws(()=>importBuild(bad),/raid buff/);
bad.raidBuffs={};bad.player.rotation.priorityList[0].action={castSpell:{spellId:{spellId:12345}}};assert.throws(()=>importBuild(bad),/unsupported APL/i);
// Reordering complete JSON representations has no effect on the packed model.
const reversed=Object.fromEntries(Object.entries(simple).reverse());assert.deepEqual(resolveCandidate(reversed).packed,resolveCandidate(simple).packed);
// Runtime code and vendored configuration model must remain mechanically identical.
const provenance=JSON.parse(fs.readFileSync('policies/warlock/source/provenance.json'));
for(const [name,identity] of Object.entries(provenance))assert.equal(createHash('sha256').update(fs.readFileSync('policies/warlock/source/'+name)).digest('hex'),identity.snapshotSha256);
console.log('PASS: editor packing, immutable copies, save/import round trips, talents, line errors and unsupported imports');
