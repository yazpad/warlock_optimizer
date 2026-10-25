// Paired measurement of the actual production aggregation loop; optional Vulkan
// pairs stay separately scoped and software timings never establish GPU throughput.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const args=process.argv.slice(2);let vulkan=false,software=false,pairs=9,total=10;
while(args.length){const arg=args.shift();if(arg==='--vulkan')vulkan=true;else if(arg==='--allow-software')software=true;else if(arg==='--pairs')pairs=Number(args.shift());else if(arg==='--total-fights')total=Number(args.shift());else throw Error('Unknown argument: '+arg);}
if(!Number.isInteger(pairs)||pairs<3||pairs>30)throw Error('Expected 3..30 pairs');
const root=path.resolve(import.meta.dirname,'..');process.chdir(root);
fs.mkdirSync('artifacts/histogram-performance',{recursive:true});
const directory=fs.mkdtempSync(path.resolve('artifacts/histogram-performance/run-'));
const run=(command,args)=>execFileSync(command,args,{encoding:'utf8',stdio:['ignore','pipe','pipe'],maxBuffer:16*1024*1024});
const median=a=>{a=[...a].sort((a,b)=>a-b);return(a[Math.floor((a.length-1)/2)]+a[Math.floor(a.length/2)])/2;};
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const executable=path.join(directory,'aggregation');
run('cc',['-O2','-Wall','-Wextra','-Werror','validation/benchmark_histogram.c','-lvulkan','-o',executable]);
// Select one CPU from the actual container affinity mask.
const mask=run('taskset',['-pc',String(process.pid)]).trim().split(':').at(-1).trim();
const cpu=Number(mask.split(/[,-]/)[0]);
const report={schemaVersion:1,cpu:{model:os.cpus()[0]?.model,affinityCpu:cpu,scope:'Actual production C aggregation of 1,000,000 synthetic valid fast reports; excludes GPU execution and process startup',warmupPassesPerProcess:2,timedPassesPerProcess:9,pairs,measurements:[]},compiler:run('cc',['--version']).split('\n')[0],compilerFlags:'-O2 -Wall -Wextra -Werror -lvulkan',sourceHashes:Object.fromEntries(['tools/vulkan_probe.c','tools/dps_histogram.h','validation/benchmark_histogram.c'].map(p=>[p,hash(p)])),vulkan:null};
for(let pair=1;pair<=pairs;pair++)for(const enabled of pair%2?[false,true]:[true,false]){
 const result=JSON.parse(run('taskset',['-c',String(cpu),executable,enabled?'1':'0']));report.cpu.measurements.push({pair,...result});
}
report.cpu.mediansMs=Object.fromEntries([false,true].map(enabled=>[enabled?'enabled':'disabled',median(report.cpu.measurements.filter(r=>r.collection===enabled).map(r=>median(r.milliseconds)))]));
report.cpu.addedMs=report.cpu.mediansMs.enabled-report.cpu.mediansMs.disabled;
report.cpu.ratio=report.cpu.mediansMs.enabled/report.cpu.mediansMs.disabled;
console.log('CPU million-report aggregation:',JSON.stringify({mediansMs:report.cpu.mediansMs,addedMs:report.cpu.addedMs,ratio:report.cpu.ratio}));
const save=()=>fs.writeFileSync(path.join(directory,'summary.json'),JSON.stringify(report,null,2)+'\n');save();
if(vulkan){
 report.vulkan={scope:'Full native runs; software diagnostics do not establish hardware performance',softwareDiagnostic:software,totalFights:total,pairs,measurements:[]};let identity;
 const evaluate=(enabled,name,raw=false)=>{
  const output=path.join(directory,name+'.json'),cmd=['tools/run_native.lua','--total-fights',String(total),'--warmup-batches','2','--output',output];
  if(raw)cmd.push('--raw-output',path.join(directory,name+'.bin'));
  if(!enabled)cmd.push('--no-histogram');if(software)cmd.push('--allow-software','--no-subgroup-size');
  console.log(name);run('luajit',cmd);const value=JSON.parse(fs.readFileSync(output));
  const rows=value.results.map(row=>{const clean=structuredClone(row);delete clean.histogram;return clean;});
  const current=JSON.stringify([value.suiteHash,value.inputSnapshotHash,value.kernelHashes,value.optimization,value.runtime,value.probe.device,rows]);
  if(identity&&identity!==current)throw Error('A/B workload/results/source identity changed');identity=current;
  if(value.results.some(row=>enabled!==Boolean(row.histogram)))throw Error('Wrong histogram collection mode');
  if(!software&&![1,2,3].includes(value.probe.deviceType))throw Error('Hardware run returned software device');
  return{gpuMs:value.probe.gpuComputeMs,aggregationMs:value.probe.cDecodeMs,nativeWallMs:value.timing.wallMs,steadyMs:value.probe.executeReadbackMs+value.probe.cDecodeMs,device:value.probe.device};
 };
 evaluate(false,'replay-disabled',true);evaluate(true,'replay-enabled',true);
 const left=fs.readFileSync(path.join(directory,'replay-disabled.bin')),right=fs.readFileSync(path.join(directory,'replay-enabled.bin'));
 if(!left.equals(right)||left.length!==total*64)throw Error('Histogram collection changed simulation reports');
 const collected=JSON.parse(fs.readFileSync(path.join(directory,'replay-enabled.json')));
 const suite=JSON.parse(fs.readFileSync('data/validation/forever_baseline.json'));
 for(let c=0;c<collected.results.length;c++){
  const row=collected.results[c],h=row.histogram,expected=Array(256).fill(0);
  for(let i=0;i<row.sampleCount;i++)expected[Math.floor(left.readFloatLE((c*row.sampleCount+i)*64)/suite.scenarios[c].duration/h.high*256)]++;
  if(JSON.stringify(expected)!==JSON.stringify(h.bins))throw Error('Histogram counts differ from observed DPS');
 }
 report.vulkan.replay={status:'passed',fights:total,reportWords:total*16,allHistogramCountsMatch:true};
 for(const name of ['replay-disabled','replay-enabled'])fs.unlinkSync(path.join(directory,name+'.bin'));
 evaluate(false,'warmup-disabled');evaluate(true,'warmup-enabled');
 for(let pair=1;pair<=pairs;pair++)for(const enabled of pair%2?[false,true]:[true,false]){report.vulkan.measurements.push({pair,enabled,...evaluate(enabled,`pair-${pair}-${enabled?'enabled':'disabled'}`)});save();}
 report.vulkan.medians={};
 for(const mode of [false,true])report.vulkan.medians[mode?'enabled':'disabled']=Object.fromEntries(['gpuMs','aggregationMs','nativeWallMs','steadyMs'].map(key=>[key,median(report.vulkan.measurements.filter(r=>r.enabled===mode).map(r=>r[key]))]));
 save();console.log('Vulkan:',JSON.stringify(report.vulkan.medians));
}
console.log('Saved',path.join(directory,'summary.json'));
