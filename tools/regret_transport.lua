-- Diagnostic-only persistent adapter generated from the unchanged native probe.
-- Protocol: lane count (u32), fixed-size config buffer, then raw reports on stdout.
-- One device/pipeline/buffer allocation serves an entire search job.
local iohelp=require('tools.native_support')
local M={}
local function once(s,a,b)
 local at=assert(s:find(a,1,true),'Regret transport anchor missing: '..a)
 assert(not s:find(a,at+#a,true),'Ambiguous regret transport anchor: '..a)
 return s:sub(1,at-1)..b..s:sub(at+#a)
end
function M.source()
 local source=iohelp.read('tools/vulkan_probe.c')
 source=once(source,'  double warmupStart = seconds(), warmupMs = 0, gpuMs = 0, decodeMs = 0;',[[
  output=stdout;
  uint32_t capacity=total;
  fprintf(stderr,"Regret worker ready for requests\n");
  while(fread(&total,sizeof(total),1,stdin)==1) {
    if(!total || total>capacity)fail("Invalid regret lane count");
    if(fread(buffers[0].mapped,1,configSize,stdin)!=configSize)fail("Incomplete regret configs");
    params[1]=total;params[4]=total;
  double warmupStart = seconds(), warmupMs = 0, gpuMs = 0, decodeMs = 0;]])
 local first=assert(source:find('  if (output && fclose(output))',1,true))
 local last=assert(source:find('  if (queryPool)\n    vkDestroyQueryPool',first,true))
 source=source:sub(1,first-1)..'  if(fflush(stdout))fail("Regret output flush failed");\n  }\n'..source:sub(last)
 return source
end
function M.build(work)
 local source=M.source()
 local key=iohelp.hash(source..iohelp.read('tools/dps_histogram.h')..'regret-persistent-v1')
 local probe='artifacts/.cache/regret-probe-'..key
 if not iohelp.exists(probe) then
  iohelp.write(work..'/regret-probe.c',source)
  iohelp.run('cc -O2 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -I tools '..iohelp.quote(work..'/regret-probe.c')..' -lvulkan -o '..iohelp.quote(work..'/regret-probe'))
  iohelp.run('mv '..iohelp.quote(work..'/regret-probe')..' '..iohelp.quote(probe))
 end
 return probe
end
function M.open(probe,shader,work,words,capacity,initialConfigs,params)
 iohelp.write(work..'/server-configs.bin',initialConfigs);iohelp.write(work..'/server-params.bin',params)
 local fifo=work..'/reply.fifo'
 iohelp.run('mkfifo '..iohelp.quote(fifo))
 local command={probe,shader,work..'/server-configs.bin',work..'/server-params.bin','-',words,capacity}
 for i,v in ipairs(command) do command[i]=iohelp.quote(v) end
 local input=assert(io.popen(table.concat(command,' ')..' > '..iohelp.quote(fifo),'w'))
 local output=assert(io.open(fifo,'rb'))
 local server={}
 function server:dispatch(countBytes,configs,n)
  assert(input:write(countBytes,configs));assert(input:flush())
  local bytes=assert(output:read(n*words*4),'Regret worker closed its output')
  assert(#bytes==n*words*4,'Incomplete persistent regret response')
  return bytes
 end
 function server:close()
  local ok=input:close();output:close();os.remove(fifo)
  assert(ok,'Persistent regret worker failed')
 end
 return server
end
return M
