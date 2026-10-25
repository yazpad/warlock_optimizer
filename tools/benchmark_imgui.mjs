// Paired native-worker presentation test; rendering-paused and visible modes stay distinct.
import fs from 'node:fs';
import path from 'node:path';
import {run,readJson,save,equal,check,cli} from './tooling.mjs';
cli(()=>{
 const args=process.argv.slice(2),opts={'--pairs':'5','--total-fights':'1000000','--batch-size':'65536','--warmup-batches':'2'};
 const forwarded=[];let directory,visible=false,software=false;
 while(args.length){
  const k=args.shift();
  if(k==='--live-ui'){visible=true;continue;}
  if(k==='--allow-software'){software=true;forwarded.push(k);continue;}
  if(k==='--no-subgroup-size'||k==='--generic'||k==='--no-histogram'){forwarded.push(k);continue;}
  const v=args.shift();check(v,'Missing '+k);
  if(k==='--candidate'){forwarded.push(k,path.resolve(v));continue;}
  if(k==='--output-dir')directory=path.resolve(v);else{check(k in opts,'Unknown option: '+k);opts[k]=v;}
 }
 const pairs=Number(opts['--pairs']);check(Number.isInteger(pairs)&&pairs>=1&&pairs<=10,'pairs must be 1..10');
 if(directory){check(!fs.existsSync(directory),'Use a fresh output directory');fs.mkdirSync(directory,{recursive:true});}
 else{fs.mkdirSync('artifacts/imgui-paired',{recursive:true});directory=fs.mkdtempSync(path.resolve('artifacts/imgui-paired/run-'));}
 const summary={schemaVersion:1,status:'running',presentation:visible?'visible-10hz':'rendering-paused',softwareDiagnostic:software,pairs,measurements:[],replay:null,thresholds:{gpuTimeRatio:1.02,steadyTimeRatio:1.05},note:'Exact raw replay is measured separately from aggregate-only timing pairs. GPU and C aggregation are the same native backend; this checks presentation overhead, not independent engine accuracy.'};
 const summaryPath=path.join(directory,'summary.json');
 let baseline,frontendIdentity;
 const nativeArgs=['--runs','1'];
 for(const k of ['--total-fights','--batch-size','--warmup-batches'])nativeArgs.push(k,opts[k]);
 nativeArgs.push(...forwarded);
 function evaluate(name,mode,raw=false){
  const out=path.join(directory,name),cmd=[mode==='headless'?'--headless':'--benchmark',...nativeArgs,'--output-dir',out];
  if(mode!=='headless'&&visible)cmd.push('--live-ui');if(raw)cmd.push('--raw-reports');
  console.log(name+': '+mode+(raw?' (raw replay only)':' (aggregate-only timing)'));
  run('./tools/run_imgui.sh',cmd,{stdio:'inherit'});
  const result=readJson(path.join(out,'summary.json'));check(result.status==='complete'&&result.runs.length===1,'Incomplete native worker evaluation');
  const native=result.runs[0].native;
  if(!baseline)baseline=native;
  else{
   for(const k of ['schemaVersion','classId','suiteHash','suiteId','kernelHashes','inputSnapshotHash','mode','iterations','seed','optimization','results'])check(equal(baseline[k],native[k]),'Paired identity/results mismatch: '+k);
   check(baseline.runtime.engineBuildKey===native.runtime.engineBuildKey&&baseline.runtime.nativeRunnerHash===native.runtime.nativeRunnerHash&&baseline.probe.device===native.probe.device,'Paired engine/runner/device mismatch');
  }
  const observations=result.uiObservations;
  check(observations&&observations.rendererInitialized===(mode!=='headless'),'Presentation initialization label mismatch');
  check(typeof observations.frontendSourceHash==='string'&&observations.frontendSourceHash.length===64,'Missing frontend source identity');
  const identity={source:observations.frontendSourceHash,build:observations.frontendBuildType,imgui:observations.imguiRevision,sdl:observations.sdlVersion};
  if(frontendIdentity)check(equal(frontendIdentity,identity),'Frontend source/build/dependency identity changed between pairs');else frontendIdentity=identity;
  if(mode!=='headless'&&!visible)check(observations.framesDuringNativeJobs===0,'Compute-priority UI rendered during native work');
  if(!software)check(native.probe.deviceType!==4&&native.probe.deviceType!==0,'Hardware performance acceptance rejects software/unknown devices');
  return result;
 }
 try{
  evaluate('replay-headless','headless',true);evaluate('replay-gui','gui',true);
  run('node',['tools/compare_imgui.mjs',path.join(directory,'replay-headless/summary.json'),path.join(directory,'replay-gui/summary.json'),'--output',path.join(directory,'replay.json')],{stdio:'inherit'});
  summary.replay=readJson(path.join(directory,'replay.json'));
  for(const mode of ['headless','gui'])fs.unlinkSync(path.join(directory,'replay-'+mode,'native-1.bin'));
  summary.replayRawFilesRetained=false;
  for(let pair=0;pair<pairs;pair++)for(const mode of pair%2?['gui','headless']:['headless','gui']){
   const name=`pair-${pair+1}-${mode}`,result=evaluate(name,mode);
   summary.measurements.push({pair:pair+1,mode,file:path.join(name,'summary.json'),timing:result.runs[0].timing,uiObservations:result.uiObservations});save(summaryPath,summary);
  }
  summary.medians={};
  for(const mode of ['headless','gui']){
   summary.medians[mode]={};
   for(const metric of ['gpuComputeMs','steadyMs','gpuSimsPerSecond','nativeSimsPerSecond','clientWallMs']){
    const values=summary.measurements.filter(m=>m.mode===mode).map(m=>metric==='steadyMs'?m.timing.executeReadbackMs+m.timing.cDecodeMs:m.timing[metric]).sort((a,b)=>a-b);
    check(values.every(v=>Number.isFinite(v)&&v>0),'Invalid paired timing: '+metric);const n=values.length;summary.medians[mode][metric]=(values[Math.floor((n-1)/2)]+values[Math.floor(n/2)])/2;
   }
  }
  summary.gpuTimeRatio=summary.medians.gui.gpuComputeMs/summary.medians.headless.gpuComputeMs;
  summary.steadyTimeRatio=summary.medians.gui.steadyMs/summary.medians.headless.steadyMs;
  summary.withinThresholds=summary.gpuTimeRatio<=1.02&&summary.steadyTimeRatio<=1.05;
  summary.historicalNativeGpuTarget=1560000;
  summary.status=software||pairs<5?'diagnostic-only':summary.withinThresholds?'passed-relative-overhead':'regressed';
  console.log(`Paired GPU medians: native/headless ${Math.round(summary.medians.headless.gpuSimsPerSecond)} sims/sec, GUI-attached ${Math.round(summary.medians.gui.gpuSimsPerSecond)} sims/sec; historical native target ~1.56M sims/sec`);
  console.log(`${summary.status}: GPU time ratio ${summary.gpuTimeRatio.toFixed(4)}, steady time ratio ${summary.steadyTimeRatio.toFixed(4)}; lower is faster`);
  if(summary.status==='regressed')process.exitCode=1;
 }catch(e){summary.status='failed';summary.error=e.message;delete summary.medians;throw e;}
 finally{save(summaryPath,summary);console.log('Saved '+summaryPath);}
});
