// Repeat independent training runs; report every held-out result, never select a winner.
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const options={generations:1024,population:32,sigma:0.1,fights:2048,start:32768,seeds:'13,42,73,101,137'};
for(let i=2;i<process.argv.length;i+=2){
  const key=process.argv[i].replace(/^--/,'');
  if(!['suite','output','generations','population','sigma','fights','start','seeds'].includes(key)||!process.argv[i+1])
    throw new Error('Expected --suite/--output/--generations/--population/--sigma/--fights/--start/--seeds VALUE');
  options[key]=process.argv[i+1];
}
const seeds=String(options.seeds).split(',').map(Number);
if(!seeds.length||seeds.some(s=>!Number.isInteger(s)||s<1||s>2147483646)||new Set(seeds).size!==seeds.length)
  throw new Error('Evolution seeds must be unique integers in [1,2147483646]');
const output=options.output?path.resolve(options.output):fs.mkdtempSync(path.resolve('artifacts/neuro-benchmark-'));
if(options.output)fs.mkdirSync(output); // Refuse to overwrite an earlier experiment.
const rows=[];
for(const seed of seeds){
  const result=path.join(output,`seed-${seed}.json`);
  const log=fs.openSync(path.join(output,`seed-${seed}.log`),'w');
  console.log(`Training evolution seed ${seed}; ${options.generations} generations, ${options.population} population`);
  try {
    const run=spawnSync('luajit',['tools/run_mcts.lua','--algorithm','neuroevolution',
      '--generations',String(options.generations),'--population',String(options.population),
      '--mutation-sigma',String(options.sigma),'--evolution-seed',String(seed),
      '--fights',String(options.fights),'--evaluation-start',String(options.start),
      '--work-dir',path.join(output,`work-${seed}`),'--output',result,
      ...(options.suite?['--suite',options.suite]:[])],{stdio:['ignore',log,log]});
    if(run.status!==0)throw new Error(`Seed ${seed} failed; see ${output}/seed-${seed}.log`);
  }finally{fs.closeSync(log);}
  const d=JSON.parse(fs.readFileSync(result));
  const row={seed,trainingGain:d.trainingCurve.at(-1).policyDps-d.trainingCurve[0].policyDps,
    heldoutGain:d.dpsDelta,ciLow:d.ciLow,ciHigh:d.ciHigh,
    trainingSeconds:d.timing.trainingWallMs/1000,
    trainingFightsPerSecond:d.timing.trainingPolicyFights*1000/d.timing.trainingWallMs};
  rows.push(row);console.log(JSON.stringify(row));
  fs.writeFileSync(path.join(output,'summary.json'),JSON.stringify({options,completed:rows.length,rows},null,2));
}
console.log(`Evidence: ${output}`);
