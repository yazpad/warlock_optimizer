-- Output only: saved build input parsing will use a separate validated decoder.
local M={}
local escapes={['"']='\\"',['\\']='\\\\',['\b']='\\b',['\f']='\\f',['\n']='\\n',['\r']='\\r',['\t']='\\t'}
local function quote(value)
 return '"'..value:gsub('[%z\1-\31\\"]',function(c) return escapes[c] or ('\\u%04x'):format(c:byte()) end)..'"'
end
function M.encode(value,seen)
 local kind=type(value)
 if kind=='nil' then return 'null' elseif kind=='boolean' then return value and 'true' or 'false'
 elseif kind=='string' then return quote(value)
 elseif kind=='number' then assert(value==value and math.abs(value)~=math.huge,'Cannot serialize a nonfinite number');return ('%.17g'):format(value) end
 assert(kind=='table','Unsupported JSON value: '..kind)
 seen=seen or {};assert(not seen[value],'Cyclic JSON object');seen[value]=true
 local count,array=0,true
 for key in pairs(value) do count=count+1;if type(key)~='number' or key%1~=0 or key<1 then array=false end end
 array=array and count>0 and count==#value
 local result={}
 if array then for _,item in ipairs(value) do result[#result+1]=M.encode(item,seen) end
 else
  local keys={};for key in pairs(value) do assert(type(key)=='string','JSON objects require string keys');keys[#keys+1]=key end;table.sort(keys)
  for _,key in ipairs(keys) do result[#result+1]=quote(key)..':'..M.encode(value[key],seen) end
 end
 seen[value]=nil
 return (array and '[' or '{')..table.concat(result,',')..(array and ']' or '}')
end
return M
