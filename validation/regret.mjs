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
// Explicit tracing reruns just the selected pair through the real job boundary.
const traceRow=a.rows[0], traceAlt=traceRow.alternatives.find(v=>v.action!==traceRow.takenAction);
const selection=Object.fromEntries(['suiteHash','kernelHash','detailKernelHash','seed','candidateId','baselineDps'].map(k=>[k,a[k]]));
for(const key of ['decision','time','mana','takenPlan']) selection[key]=traceRow[key];
selection.alternativePlan=traceAlt.plan;
const traceRequest=path.join(dir,'trace-request.json');fs.writeFileSync(traceRequest,JSON.stringify(selection));
const traceOutput=path.join(dir,'trace');
const traced=spawnSync('artifacts/imgui-build/fastsim-imgui',[...args,'--regret-trace-request',traceRequest,'--output-dir',traceOutput],{encoding:'utf8',timeout:180000});
check(traced.status===0,'Trace job failed: '+traced.stdout+traced.stderr);
const trace=readJson(path.join(traceOutput,'summary.json')).analysis.trace;
check(trace.instrumentationVerified && trace.samples===64 && trace.decision===traceRow.decision,'Trace replay/selection mismatch');
check(trace.outcomes.length===64 && trace.schemaVersion===2,'Trace pairs missing');
check(trace.example.taken.timeline.length>0 && trace.example.alternative.timeline.length>0,'Paired timeline missing');
check(Array.isArray(trace.drivers) && trace.drivers.length>0,'Generic damage contributors missing');
check(Math.abs(trace.drivers.reduce((sum,v)=>sum+v.dpsDelta,0)-trace.dpsDelta)<0.01,'Contributors do not reconcile');
for(const driver of trace.drivers) {
 if(driver.actionId) check(driver.gates && driver.windows,'Player contributor lacks timing/gates');
 else check(Object.keys(driver.windows).length===0,'Pet damage invented a player timeline');
}

check(Math.abs(trace.outcomes.reduce((sum,v)=>sum+v.dpsDelta,0)/64-trace.dpsDelta)<1e-6,'Trace statistics mismatch');
check(a.suiteSource && trace.alternativePlan===traceAlt.plan,'Original snapshot/plans missing');
const badRequest=path.join(dir,'trace-wrong-suite.json');fs.writeFileSync(badRequest,JSON.stringify({...selection,suiteHash:'0'.repeat(64)}));
const rejected=spawnSync('artifacts/imgui-build/fastsim-imgui',[...args,'--regret-trace-request',badRequest,'--output-dir',path.join(dir,'trace-rejected')],{encoding:'utf8',timeout:180000});
check(rejected.status!==0,'Trace must reject a different original suite');
function checkDetails(analysis) {
 for (const row of analysis.rows) for (const alternative of row.alternatives) {
  const d=alternative.detail;
  check(d && d.duration===analysis.duration && d.damage.length>0 && d.uptimes.length===12,'Missing detailed alternative report');
  check(Math.abs(d.damage.reduce((sum,s)=>sum+s.damage,0)/d.duration-alternative.meanDps)<0.01,'Detailed damage does not reconcile with paired DPS');
  check(d.damage.every(s=>['casts','hits','crits','misses','ticks'].every(k=>Number.isFinite(s[k]) && s[k]>=0)),'Invalid spell counters');
  check(d.uptimes.every(u=>u.fraction>=0 && u.fraction<=1.000000001 && Math.abs(u.seconds-u.fraction*d.duration)<1e-6),'Invalid uptime decoding');
  check(['spent','gained','remaining','wasted','taps'].every(k=>Number.isFinite(d.mana[k]) && d.mana[k]>=0),'Invalid mana detail');
 }
}
checkDetails(a);
check(a.actions.length===a.storedDecisions,'Missing captured baseline actions');
check(a.actions.every((action,i)=>action.decision===i && Number.isFinite(action.time) && Number.isFinite(action.mana) && typeof action.action==='string'),'Invalid baseline action sequence');
for(const row of a.rows){
 const action=a.actions[row.decision];
 check(action.time===row.time && action.mana===row.mana && action.shadowTrance===row.shadowTrance,'Action context differs from inspected root');
}

check(!fs.existsSync(path.join(output,'work-1')),'Completed work directory leaked');
// The default one-action mode batches different roots/actions and follows APL to terminal.
const flatDir=path.join(dir,'flat');
const flatRun=spawnSync('artifacts/imgui-build/fastsim-imgui',[...args,'--regret-depth','1','--regret-validation','1000','--output-dir',flatDir],{encoding:'utf8',timeout:180000});
check(flatRun.status===0,'Flat rollout failed: '+flatRun.stdout+flatRun.stderr);
const flat=readJson(path.join(flatDir,'summary.json')).analysis;
checkDetails(flat);
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
 checkDetails(analysis);
 if(build===0){const doom=analysis.rows.flatMap(r=>r.alternatives).find(v=>v.action==='Bane of Doom');check(doom && doom.doomDamage>0,'Doom must tick in full-length continuations');}
}
check(files.every((p,i)=>sha(fs.readFileSync(p))===before[i]),'Baseline compute sources changed');
console.log('PASS: actual GPU regret job, exact replay, paired estimates, artifact routing, cancellation, unchanged baseline compute sources');
console.log('Saved',dir);
