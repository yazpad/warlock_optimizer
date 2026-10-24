import fs from 'node:fs';
import {temporary,compileShader,run,check,cli} from './tooling.mjs';
cli(()=>temporary(dir=>{
 run('cc',['-O2','-Wall','-Wextra','-Werror','tools/vulkan_probe.c','-lvulkan','-o',dir+'/probe']);
 compileShader(fs.readFileSync('validation/headless_transport.glsl','utf8'),dir+'/kernel.spv');
 const config=Buffer.alloc(4);config.writeUInt32LE(1000);fs.writeFileSync(dir+'/configs.bin',config);
 const params=Buffer.alloc(24);[5,2,101,0,10,100].forEach((v,i)=>params.writeUInt32LE(v,i*4));fs.writeFileSync(dir+'/params.bin',params);
 const meta=JSON.parse(run(dir+'/probe',[dir+'/kernel.spv',dir+'/configs.bin',dir+'/params.bin',dir+'/output.bin','1','3']));
 const actual=fs.readFileSync(dir+'/output.bin');check(actual.length===40,'Output size mismatch');
 for(let i=0;i<10;i++)check(actual.readUInt32LE(i*4)===1101+i,'Transport lane mismatch');
 check(meta.draws===4&&meta.totalFights===10,'Dispatch count mismatch');
 check(meta.requiredSubgroupSize===Number(process.env.FASTSIM_SUBGROUP_SIZE||0),'Subgroup mismatch');
 check(meta.warmupBatches===Number(process.env.FASTSIM_WARMUP_BATCHES||0),'Warmup mismatch');
 check(meta.gpuComputeMs===null||meta.gpuComputeMs>=0,'Invalid GPU timing');
 console.log('PASS: actual Vulkan offsets, partial batches and readback on '+meta.device);
}));
