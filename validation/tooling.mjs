import assert from 'node:assert/strict';
import fs from 'node:fs';
import {compare as parity} from '../tools/compare_parity.mjs';
import {compare as replay} from '../tools/compare_variant.mjs';
import {temporary,readJson,sha} from '../tools/tooling.mjs';
function fixture(){
 const suite={schemaVersion:1,id:'test',classId:'warlock',kernelHashes:{kernel:'abc'},iterations:10000,seed:101,acceptanceReady:true,thresholdDps:1,scenarios:[{id:'a',reference:{mean:100,sd:1,n:10000}},{id:'b',reference:{mean:200,sd:1,n:10000}}]};
 const result={schemaVersion:1,suiteId:'test',suiteHash:'hash',classId:'warlock',kernelHashes:{kernel:'abc'},iterations:10000,seed:101,status:'complete',runtime:{backend:'Vulkan'},results:[{candidateId:'b',status:'complete',meanDps:200.2,sd:1,sampleCount:10000},{candidateId:'a',status:'complete',meanDps:100.4,sd:1,sampleCount:10000}]};return {suite,result};
}
{
 const {suite,result}=fixture(),report=parity(suite,result,'hash');assert.equal(report.decision,'pass');assert.ok(Math.abs(report.maeDps-0.3)<1e-12);assert.deepEqual(report.rows.map(r=>r.id),['a','b']);
 result.results.forEach(r=>r.meanDps+=3);assert.equal(parity(suite,result,'hash').decision,'fail');
 result.results.forEach(r=>r.sd=1000);assert.equal(parity(suite,result,'hash').decision,'inconclusive');
 suite.acceptanceReady=false;assert.equal(parity(suite,result,'hash').decision,'diagnostic-only');
 suite.acceptanceReady=true;suite.scenarios[0].adaptations=['unreviewed'];assert.equal(parity(suite,result,'hash').decision,'diagnostic-only');
}
for(const mutate of [r=>r.suiteHash='stale',r=>r.status='failed',r=>r.kernelHashes={},r=>r.seed=42,r=>r.runtime.backend='OpenGL',r=>r.results.pop(),r=>r.results[0].candidateId='a',r=>r.results[0].sampleCount=1,r=>r.results[0].meanDps=NaN,r=>r.results[0].sd=-1]){
 const {suite,result}=fixture();mutate(result);assert.throws(()=>parity(suite,result,'hash'));
}
{
 const suitePath='validation/evidence/headless_fast_frozen_suite.json',bytes=fs.readFileSync(suitePath);
 const report=parity(JSON.parse(bytes),readJson('validation/evidence/headless_fast_baseline.json'),sha(bytes));
 const saved=readJson('validation/evidence/headless_fast_comparison.json');
 assert.equal(report.decision,'diagnostic-only');
 for(const key of ['maeDps','maeApprox95Lower','maeApprox95Upper'])assert.ok(Math.abs(report[key]-saved[key])<1e-7,key);
}
temporary(dir=>{
 const manifest=readJson('sim/classes/warlock/generated/manifest.json');
 for(const mode of ['fast','detailed']){
  const reference={schemaVersion:1,classId:'warlock',suiteHash:'frozen',kernelHashes:{fast:'hash'},inputSnapshotHash:'inputs',mode,iterations:2,seed:101,status:'complete',runtime:{device:'test GPU'},optimization:{variant:'generic'},results:[{candidateId:'a',sampleCount:2,status:'complete'}]};
  const candidate=structuredClone(reference);candidate.optimization.variant='shared-features';
  const words=mode==='fast'?manifest.fastWords:manifest.detailedWords;
  const raw=Buffer.alloc(words*4*2);raw.writeUInt32LE(1,4);raw.writeUInt32LE(1,words*4+4);
  const a=dir+'/a.bin',b=dir+'/b.bin';fs.writeFileSync(a,raw);fs.writeFileSync(b,raw);
  assert.equal(replay(reference,a,candidate,b).comparedFights,2);
  const damaged=Buffer.from(raw);damaged[20]=1;fs.writeFileSync(b,damaged);assert.throws(()=>replay(reference,a,candidate,b));fs.writeFileSync(b,raw);
  candidate.seed=102;assert.throws(()=>replay(reference,a,candidate,b));candidate.seed=101;
  candidate.inputSnapshotHash='changed';assert.throws(()=>replay(reference,a,candidate,b));candidate.inputSnapshotHash='inputs';
  const invalid=Buffer.from(raw);invalid.writeUInt32LE(0,4);fs.writeFileSync(a,invalid);fs.writeFileSync(b,invalid);assert.throws(()=>replay(reference,a,candidate,b));
  invalid.writeUInt32LE(1,4);invalid.writeFloatLE(NaN,0);fs.writeFileSync(a,invalid);fs.writeFileSync(b,invalid);assert.throws(()=>replay(reference,a,candidate,b));
 }
});
console.log('PASS: parity decisions, uncertainty, audit blockers, historical numerics, strict fast/detailed variant replay and corrupt-report rejection');

// Controlled orchestration must reject corrupt results before publishing medians.
const {experiment}=await import('../tools/benchmark_specialization.mjs');
for(const corrupt of [false,true])temporary(dir=>{
 const order=[];
 function execute(file,args){
  assert.equal(file,'luajit');
  const variant=args.includes('--subgroup-size')?'shared-features-wave32':args.includes('--specialize-features')?'shared-features':'generic';order.push(variant);
  const output=args[args.indexOf('--output')+1],rawPath=args[args.indexOf('--raw-output')+1];
  const result={schemaVersion:1,classId:'warlock',suiteHash:'frozen',kernelHashes:{fast:'hash'},inputSnapshotHash:'inputs',mode:'fast',iterations:2,seed:101,status:'complete',probe:{device:'synthetic'},runtime:{backend:'Vulkan'},optimization:{variant},timing:{gpuComputeMs:variant==='generic'?100:50,executeReadbackMs:120,wallMs:200},results:[{candidateId:'a',sampleCount:2,status:'complete'}]};
  const raw=Buffer.alloc(16*4*2);raw.writeUInt32LE(1,4);raw.writeUInt32LE(1,16*4+4);if(corrupt&&variant!=='generic')raw[20]=1;
  fs.writeFileSync(output,JSON.stringify(result));fs.writeFileSync(rawPath,raw);
 }
 const args=['--total-fights','10','--subgroup-size','32','--output-dir',dir];
 if(corrupt)assert.throws(()=>experiment(args,execute));else experiment(args,execute);
 const summary=readJson(dir+'/summary.json');
 if(corrupt){assert.equal(summary.status,'failed');assert.equal(summary.medians,undefined);}
 else{assert.equal(summary.status,'passed-replay');assert.equal(summary.replayGates.length,5);assert.deepEqual(order,['generic','shared-features','shared-features-wave32','shared-features-wave32','shared-features','generic']);assert.equal(summary.medians['shared-features'].gpuComputeMs.median,50);}
});
console.log('PASS: controlled forward/reverse order and fail-closed benchmark summaries');
