import fs from 'node:fs';
import {temporary,compileShader,run,cli} from './tooling.mjs';
cli(()=>temporary(dir=>{
 compileShader(fs.readFileSync('validation/subgroup.glsl','utf8'),dir+'/subgroup.spv','vulkan1.2');console.log('PASS: subgroup canary Vulkan compilation / SPIR-V validation');
 for(const file of ['sim/gpu/smoke.glsl','validation/headless_transport.glsl','sim/classes/warlock/generated/fast.glsl','sim/classes/warlock/generated/detailed.glsl']){
  compileShader(fs.readFileSync(file,'utf8'),dir+'/kernel.spv');console.log('PASS: Vulkan compilation / SPIR-V validation: '+file);
 }
 for(const mode of ['fast','detailed']){
  const source=run('luajit',['-e',`local source=require('tools.native_support').read('sim/classes/warlock/generated/${mode}.glsl');local fixture=require('sim.classes.warlock.generated.example');io.write((require('sim.classes.warlock.specialize').specialize(source,{{packed=fixture.packed}})))`]);
  compileShader(source,dir+'/specialized.spv');console.log('PASS: specialized LÖVE smoke fixture Vulkan compilation / SPIR-V validation: '+mode);
 }
}));
