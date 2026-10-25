import fs from 'node:fs';
import {temporary,compileShader,run,cli} from './tooling.mjs';
cli(()=>temporary(dir=>{
 for(const file of ['validation/headless_transport.glsl','sim/classes/warlock/generated/fast.glsl','sim/classes/warlock/generated/detailed.glsl']){
  compileShader(fs.readFileSync(file,'utf8'),dir+'/kernel.spv');console.log('PASS: Vulkan compilation / SPIR-V validation: '+file);
 }
 const trace=run('luajit',['-e',`io.write(require('tools.detailed_trace').instrument(require('tools.native_support').read('sim/classes/warlock/generated/detailed.glsl')))`]);
 compileShader(trace,dir+'/trace.spv');console.log('PASS: detailed-only trace Vulkan compilation / SPIR-V validation');
 for(const mode of ['fast','detailed']){
  const source=run('luajit',['-e',`local source=require('tools.native_support').read('sim/classes/warlock/generated/${mode}.glsl');local fixture=require('sim.classes.warlock.generated.example');io.write((require('sim.classes.warlock.specialize').specialize(source,{{packed=fixture.packed}})))`]);
  compileShader(source,dir+'/specialized.spv');console.log('PASS: specialized native fixture Vulkan compilation / SPIR-V validation: '+mode);
 }
}));
