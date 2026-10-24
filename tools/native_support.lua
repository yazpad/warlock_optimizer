-- Shared native CLI helpers; paths are always shell quoted.
local M={}
function M.quote(s) return "'"..tostring(s):gsub("'", "'\\''").."'" end
function M.read(p) local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
function M.write(p,s) local f=assert(io.open(p,'wb'));f:write(s);f:close() end
function M.exists(p) local f=io.open(p,'rb');if f then f:close();return true end;return false end
function M.run(cmd) assert(os.execute(cmd)==0,'Command failed: '..cmd) end
function M.hashFile(p)
 local pipe=assert(io.popen('sha256sum -- '..M.quote(p),'r'))
 local line=pipe:read('*a');pipe:close()
 return assert(line:match('^(%x%x%x%x%x%x%x%x%x+)'),'SHA-256 failed: '..p)
end
function M.hash(s)
 local p=os.tmpname();M.write(p,s)
 local ok,value=pcall(M.hashFile,p);os.remove(p);assert(ok,value);return value
end
function M.wrap(source)
 local n=0
 -- Compatibility for frozen shader snapshots; no external runtime is loaded.
 local s=source
 if source:find('#pragma language glsl4',1,true) then
  s=source:gsub('#pragma language glsl4','#version 450\n#define love_GlobalThreadID gl_GlobalInvocationID',1)
 else
  assert(source:find('#version 450',1,true),'Expected Vulkan GLSL 450 shader source')
 end
 s=s:gsub('layout%(std430%)',function() local b=n;n=n+1;return ('layout(std430, set=0, binding=%d)'):format(b) end)
 return s..'\nvoid main() { computemain(); }\n'
end
return M
