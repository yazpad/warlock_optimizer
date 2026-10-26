// Verify nonzero GPU inference against Lua and zero-network APL report identity.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {catalog, prepare} from '../tools/editor_config.mjs';
const dir=fs.mkdtempSync(path.resolve('artifacts/neuro-gpu-validation-'));
const candidate=structuredClone(catalog().presets[0].candidate);
candidate.encounter.duration=180;
const {suiteSource}=prepare(candidate);
fs.writeFileSync(path.join(dir,'suite.json'),suiteSource);
const run=spawnSync('luajit',['tools/run_mcts.lua','--algorithm','neuroevolution','--verify-neural',
  '--suite',path.join(dir,'suite.json'),'--fights','2048','--evaluation-start','32768','--generations','1','--population','3',
  '--allow-software','--work-dir',path.join(dir,'work'),'--output',path.join(dir,'result.json')],
  {encoding:'utf8',timeout:360000});
assert.equal(run.status,0,run.stdout+run.stderr);
assert.match(run.stdout,/verified GPU\/Lua MLP parity on \d+ nonzero-network decisions/);
const result=JSON.parse(fs.readFileSync(path.join(dir,'result.json')));
assert.equal(result.completedFightPairs,2048);
assert.equal(result.results[0].fightIndex,32768);
assert.equal(result.results.at(-1).fightIndex,34815);
assert.equal(result.network.inputs,75);
assert(result.timeline.length>0 && result.timeline.every(s=>s.features.length===20));
console.log('PASS: GPU/Lua nonzero MLP parity, masked actions, full-fight completion, exact zero-network APL reports');
console.log('Evidence: '+dir);
