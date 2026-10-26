// Small correctness checks only. No benchmarks or throughput assertions.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync,spawn} from 'node:child_process';
import {catalog} from '../tools/editor_config.mjs';
import {sha,check,readJson} from '../tools/tooling.mjs';
const files=['sim/classes/warlock/generated/fast.glsl','sim/classes/warlock/generated/detailed.glsl','tools/run_native.lua','tools/vulkan_probe.c','tools/dps_histogram.h','sim/classes/warlock/specialize.lua'];
const before=files.map(p=>sha(fs.readFileSync(p)));
const dir=fs.mkdtempSync(path.resolve('artifacts/regret-validation-'));
const candidate=structuredClone(catalog().presets[0].candidate);candidate.encounter.duration=5;
const file=path.join(dir,'candidate.json');fs.writeFileSync(file,JSON.stringify(candidate));
const args=['--headless','--regret','--candidate',file,'--allow-software','--no-subgroup-size','--regret-decisions','2','--regret-rounds','12','--regret-depth','2','--regret-samples','4','--regret-validation','8'];
const output=path.join(dir,'complete');
const result=spawnSync('artifacts/imgui-build/fastsim-imgui',[...args,'--output-dir',output],{encoding:'utf8',timeout:180000});
check(result.status===0,'Regret job failed: '+result.stdout+result.stderr);
const summary=readJson(path.join(output,'summary.json')),a=summary.analysis;
check(summary.status==='complete' && summary.mode==='regret' && !summary.medians && summary.runs.length===0,'Diagnostic must not publish benchmark medians');
check(a.reportReplay==='bit-identical' && a.fightIndex===0 && a.seed===101 && a.duration===5,'Wrong fight or replay');
check(a.analyzedDecisions>0 && a.analyzedDecisions<=2 && a.rows.length===a.analyzedDecisions,'Missing actual decisions');
for(const r of a.rows){
 check(r.samples===8 && r.time>=0 && r.time<=5 && r.dpsDelta>=0 && r.standardError>=0,'Invalid regret row');
 check(Math.abs((r.ciHigh-r.ciLow)-3.92*r.standardError)<1e-6,'Paired interval mismatch');
 check(r.alternatives.length>1 && r.alternatives.some(v=>v.action===r.takenAction),'Taken action must be compared');
}
check(!fs.existsSync(path.join(output,'work-1')),'Completed work directory leaked');
// The default one-action mode batches different roots/actions and follows APL to terminal.
const flatDir=path.join(dir,'flat');
const flatRun=spawnSync('artifacts/imgui-build/fastsim-imgui',[...args,'--regret-depth','1','--regret-validation','1000','--output-dir',flatDir],{encoding:'utf8',timeout:180000});
check(flatRun.status===0,'Flat rollout failed: '+flatRun.stdout+flatRun.stderr);
const flat=readJson(path.join(flatDir,'summary.json')).analysis;
check(flat.terminalVerified && flat.settings.validationSamples===1000 && flat.continuation==='Current APL to terminal after one forced action','Flat rollout semantics mismatch');
check(flat.rolloutFights===flat.rows.reduce((n,r)=>n+r.alternatives.length*1000,0),'Missing root/action samples');
check(flat.rolloutDispatches===Math.ceil(flat.rolloutFights/65536),'Alternatives must be batched together');
for(const r of flat.rows){check(r.takenPlan.split(' → ').length===2 && r.samples===1000,'Taken action must return directly to APL');}
// Cancellation must also terminate the persistent diagnostic subprocess.
const cancelled=path.join(dir,'cancelled');
const child=spawn('artifacts/imgui-build/fastsim-imgui',[...args,'--regret-rounds','256','--regret-samples','256','--output-dir',cancelled],{stdio:['ignore','pipe','pipe']});
let killed=false,log='';
const observe=bytes=>{log+=bytes.toString();if(!killed && log.includes('Regret worker ready')){killed=true;child.kill('SIGTERM');}};
child.stdout.on('data',observe);child.stderr.on('data',observe);
// Wait until the persistent adapter is an actual descendant before cancellation.
const hasAdapter=pid=>{
 try {
  if(fs.readlinkSync(`/proc/${pid}/exe`).includes('/regret-probe-'))return true;
  return fs.readdirSync(`/proc/${pid}/task`).flatMap(tid=>fs.readFileSync(`/proc/${pid}/task/${tid}/children`,'utf8').trim().split(/\s+/).filter(Boolean)).some(hasAdapter);
 } catch { return false; }
};
const poll=setInterval(()=>{if(!killed && hasAdapter(child.pid)){killed=true;child.kill('SIGTERM');}},10);
const timer=setTimeout(()=>child.kill('SIGKILL'),30000);
await new Promise(resolve=>child.on('exit',resolve));clearInterval(poll);clearTimeout(timer);
const stopped=readJson(path.join(cancelled,'summary.json'));
check(killed && stopped.status==='cancelled' && !stopped.medians,'Cancellation must reject accepted results');
check(!fs.existsSync(path.join(cancelled,'work-1')) && !fs.existsSync(path.join(cancelled,'native-1.json')),'Cancelled diagnostics leaked partial work');
// Check unchanged replay and later root contexts across all five frozen builds.
for(let build=0;build<5;build++){
 const frozen=path.join(dir,`frozen-${build}`);
 const run=spawnSync('artifacts/imgui-build/fastsim-imgui',['--headless','--regret','--allow-software','--no-subgroup-size',
  '--regret-candidate',String(build),'--regret-decisions','2','--regret-rounds','8','--regret-depth','2',
  '--regret-samples','4','--regret-validation','8','--regret-depth','1','--output-dir',frozen],{encoding:'utf8',timeout:180000});
 check(run.status===0,'Frozen regret replay failed: '+run.stdout+run.stderr);
 const analysis=readJson(path.join(frozen,'summary.json')).analysis;
 check(analysis.terminalVerified && analysis.reportReplay==='bit-identical' && analysis.duration===180 && analysis.rows.length===2,'Missing frozen replay or decision coverage');
 if(build===0){const doom=analysis.rows.flatMap(r=>r.alternatives).find(v=>v.action==='Bane of Doom');check(doom && doom.doomDamage>0,'Doom must tick in full-length continuations');}
}
check(files.every((p,i)=>sha(fs.readFileSync(p))===before[i]),'Baseline compute sources changed');
console.log('PASS: actual GPU regret job, exact replay, paired estimates, artifact routing, cancellation, unchanged baseline compute sources');
console.log('Saved',dir);
