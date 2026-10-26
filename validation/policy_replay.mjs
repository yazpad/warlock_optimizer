// GPU/software Vulkan integration regression. Run: node validation/policy_replay.mjs
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const dir=fs.mkdtempSync(path.resolve('artifacts/policy-replay-'));
const original=JSON.parse(fs.readFileSync('data/validation/forever_baseline.json'));
const floatBits=value=>{const b=Buffer.alloc(4);b.writeFloatLE(value);return b.readUInt32LE();};
for(const variant of ['amplify','explicit-cooldowns','unreachable-cooldowns']) {
  const baseline=original.scenarios[0];
  const suite={seed:original.seed,kernelHashes:original.kernelHashes,
    scenarios:[{id:baseline.id,duration:baseline.duration,packed:[...baseline.packed]}]};
  const scenario=suite.scenarios[0], p=scenario.packed;
  scenario.duration=45; p[16]=45000000;
  p[100]=1;
  if(variant!=='amplify') {
    p[7]=0; p[4]=1; p[11]=1; p[82]=floatBits(200);
    const rows=[];
    for(let r=0;r<32 && p[101+r];r++)
      rows.push([p[101+r],p[133+r],p[165+r],p[197+r]]);
    // Disabled and false-condition rows must remain inert. The trailing
    // off-GCD rows are unreachable when the unconditional Life Tap executes.
    const prefix=[[24,0,0,0],[0x1000000+24+22*256,0,0,0]];
    if(variant==='unreachable-cooldowns') prefix.push([0x1000000+1,0,0,0]);
    prefix.push(...[26,21,24].map(a=>[0x1000000+a,0,0,0]));
    const combined=[...prefix,...rows];
    assert(combined.length<=32);
    for(let r=0;r<32;r++)for(let k=0;k<4;k++)p[101+k*32+r]=combined[r]?.[k]??0;
  }
  const suitePath=path.join(dir,variant+'.json');
  fs.writeFileSync(suitePath,JSON.stringify(suite));
  const run=spawnSync('luajit',['validation/policy_replay.lua','--suite',suitePath,
    '--fights','2','--evaluation-start','128','--allow-software',
    '--work-dir',path.join(dir,variant+'-work'),'--output',path.join(dir,variant+'-result.json')],
    {encoding:'utf8',timeout:180000});
  assert.equal(run.status,0,run.stdout+run.stderr);
  console.log('PASS: '+variant+' forced APL full-report parity');
}
console.log('Evidence: '+dir);
