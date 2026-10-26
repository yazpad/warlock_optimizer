// End-to-end correctness check for complete online-controller fights; not a throughput benchmark.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {catalog} from '../tools/editor_config.mjs';
const core=['sim/classes/warlock/generated/fast.glsl','tools/run_native.lua','tools/vulkan_probe.c','tools/dps_histogram.h'];
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const before=core.map(hash);
const neural=process.argv.includes('--neuroevolution');
const fights=neural?2048:2;
const dir=fs.mkdtempSync(path.resolve('artifacts/policy-validation-'));
const candidate=structuredClone(catalog().presets[0].candidate);
candidate.encounter.duration=5;
const candidatePath=path.join(dir,'candidate.json');
fs.writeFileSync(candidatePath,JSON.stringify(candidate));
const output=path.join(dir,'complete');
const run=spawnSync('artifacts/imgui-build/fastsim-imgui',[
  '--headless','--policy-benchmark',...(neural?[]:['--policy-fights','2']),'--policy-puct','1.5','--policy-seed-start','128',
  ...(neural ? ['--policy-algorithm','neuroevolution','--evolution-generations','2','--evolution-population','3'] : []),
  '--candidate',candidatePath,'--allow-software','--output-dir',output
],{encoding:'utf8',timeout:360000});
assert.equal(run.status,0,run.stdout+run.stderr);
const summary=JSON.parse(fs.readFileSync(path.join(output,'summary.json')));
assert.equal(summary.status,'complete');
assert.equal(summary.mode,'policy_benchmark');
assert.equal(summary.totalFights,2*fights,'Only complete evaluated fights belong in the job count');
assert(!summary.medians && summary.runs.length===0,'Online search must not publish fast-runner throughput');
const a=summary.analysis;
assert.equal(a.schemaVersion,2);
assert.equal(a.evaluationScope,'complete_fights');
assert.equal(a.policyType,neural ? 'neuroevolution_mlp' : 'receding_horizon_mcts');
if(neural) {
  assert.equal(a.network.weights.length,1250);
  assert.equal(a.trainingCurve.length,3);
  assert.equal(a.totalSearches,0);
  assert.equal(a.inferenceBackend,'batched_gpu_full_fights');
  assert.equal(a.timing.neuralDispatches,7,'Batched training/evaluation plus one displayed trace');
  assert.equal(a.timing.trainingPolicyFights,5000,'Elite fitness must be cached');
  assert.equal(a.timing.dispatches,7,'Held-out APL checks are batched');
  assert.equal(a.timing.neuralFullFights,6000+2*fights+1);
  assert(a.timing.neuralFightsPerSecond>0);
  assert.equal(a.timing.neuralBatchCapacity,32768);
  assert(a.timing.neuralMeasuredGpuMs>=0);
  assert.equal(a.timing.neuralMeasuredFights,5000+2*fights+1);
  assert(a.timing.setupMs>=a.timing.neuralStartupWallMs);
  assert.equal(a.trainingSeeds.length,1000);
  assert.equal(new Set(a.trainingSeeds).size,1000);
  assert.equal(a.trainingSeeds[0],65536);
  assert.equal(a.trainingSeeds[999],66535);
  for(const [i,p] of a.trainingCurve.entries()) {
    assert.equal(p.generation,i);
    assert.equal(p.trainingFights,1000);
    assert(p.generationWorstDps<=p.generationMeanDps && p.generationMeanDps<=p.generationBestDps);
    assert.equal(p.candidateDps.length,i===0?1:2);
    assert(p.policyDps>=p.generationBestDps);
    assert(p.baselineDps>0 && Number.isFinite(p.policyDps));
    if(i) assert(p.policyDps>=a.trainingCurve[i-1].policyDps);
  }
}
assert.equal(a.optimalityGuaranteed,false);
assert.equal(a.reportReplay,'bit-identical');
assert.equal(a.completedFightPairs,fights);
assert.equal(a.settings.cPuct,1.5);
assert.equal(a.settings.evaluationStart,128);
assert.equal(a.fightIndex,128);
assert.equal(a.results.length,fights);
assert(!('curves' in a) && !('analyzedDecisions' in a),'Local estimates must not masquerade as policy scores');
for(const [i,row] of a.results.entries()) {
  assert.equal(row.fightIndex,128+i);
  assert.equal(row.terminalVerified,true);
  assert(row.decisions>0 && row.searches<=row.decisions);
  assert(Math.abs(row.dpsDelta-(row.policyDps-row.baselineDps))<1e-8);
}
const mean=key=>a.results.reduce((n,r)=>n+r[key]/a.results.length,0);
assert(Math.abs(mean('baselineDps')-a.meanBaselineDps)<1e-8);
assert(Math.abs(mean('policyDps')-a.meanAlgoDps)<1e-8);
assert(Math.abs(mean('dpsDelta')-a.dpsDelta)<1e-8);
assert(a.ciLow<=a.dpsDelta && a.ciHigh>=a.dpsDelta && a.standardError>=0);
assert.equal(a.winRatePct,a.results.filter(r=>r.dpsDelta>0).length*100/fights);
assert.equal(a.totalDecisions,a.results.reduce((n,r)=>n+r.decisions,0));
assert.equal(a.timeline.length,a.results[0].decisions);
for(const [i,move] of a.timeline.entries()) {
  assert.equal(move.ordinal,i);
  assert(move.time>=0 && move.time<=5 && move.mana>=0 && move.totalDamage>=0);
  assert.equal(move.signature.length,2);
  if(i)assert(move.time>=a.timeline[i-1].time);
}
assert((neural ? a.timing.searchRolloutFights===0 : a.timing.searchRolloutFights>0) && a.timing.dispatches>0 && a.timing.totalWallMs>=a.timing.evaluationWallMs);
assert(!fs.existsSync(path.join(output,'work-1')),'Completed policy work must be cleaned up');
assert(core.every((p,i)=>hash(p)===before[i]),'Production compute sources changed');
console.log('PASS: full policy UI worker path, complete fight pairs, exact APL replay, paired statistics, actual timeline, search cost and unchanged core');
console.log('Evidence: '+output);
