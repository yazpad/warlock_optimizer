-- Data-only JSON decoder for immutable native job snapshots. Never executes input.
local M={}
function M.decode(text)
 assert(type(text)=='string' and #text<=1048576,'JSON input too large')
 local at,depth=1,0
 local function ws() local _,last=text:find('^[ \t\r\n]*',at);at=(last or at-1)+1 end
 local parse
 local function str()
  assert(text:sub(at,at)=='"','Expected JSON string');at=at+1;local parts={}
  while at<=#text do
   local c=text:sub(at,at);at=at+1
   if c=='"' then return table.concat(parts) end
   if c=='\\' then
    local e=text:sub(at,at);at=at+1
    local escapes={['"']='"',['\\']='\\',['/']='/',b='\b',f='\f',n='\n',r='\r',t='\t'}
    if e=='u' then
     local h=text:sub(at,at+3);assert(h:match('^%x%x%x%x$'),'Invalid Unicode escape');at=at+4
     local n=tonumber(h,16)
     if n>=0xD800 and n<=0xDBFF then
      assert(text:sub(at,at+1)=='\\u','Missing Unicode surrogate');at=at+2
      local l=tonumber(text:sub(at,at+3),16);assert(l and l>=0xDC00 and l<=0xDFFF,'Invalid Unicode surrogate');at=at+4;n=0x10000+(n-0xD800)*1024+l-0xDC00
     else assert(n<0xDC00 or n>0xDFFF,'Unexpected Unicode surrogate') end
     if n<128 then c=string.char(n)
     elseif n<2048 then c=string.char(192+math.floor(n/64),128+n%64)
     elseif n<65536 then c=string.char(224+math.floor(n/4096),128+math.floor(n/64)%64,128+n%64)
     else c=string.char(240+math.floor(n/262144),128+math.floor(n/4096)%64,128+math.floor(n/64)%64,128+n%64) end
    else c=assert(escapes[e],'Invalid JSON escape') end
   else assert(c:byte()>=32,'Control character in JSON string') end
   parts[#parts+1]=c
  end
  error('Unterminated JSON string')
 end
 function parse()
  ws();depth=depth+1;assert(depth<=64,'JSON nesting too deep')
  local c=text:sub(at,at);local value
  if c=='"' then value=str()
  elseif c=='{' or c=='[' then
   local object=c=='{';local close=object and '}' or ']';at=at+1;value={};ws()
   if text:sub(at,at)~=close then
    repeat
     local key
     if object then key=str();ws();assert(text:sub(at,at)==':','Expected colon');at=at+1;assert(value[key]==nil,'Duplicate JSON key') else key=#value+1 end
     value[key]=parse();ws();c=text:sub(at,at);at=at+1
     if c==close then break end
     assert(c==',','Expected comma');ws()
    until false
   else at=at+1 end
  elseif text:sub(at,at+3)=='true' then value=true;at=at+4
  elseif text:sub(at,at+4)=='false' then value=false;at=at+5
  elseif text:sub(at,at+3)=='null' then error('Null is not supported in job snapshots')
  else
   local number=text:match('^([^,%]%}%s]+)',at)
   assert(number,'Invalid JSON number')
   local mantissa,exponent=number:match('^(.-)[eE](.*)$')
   if exponent then assert(exponent:match('^[%+%-]?%d+$'),'Invalid JSON exponent') else mantissa=number end
   local unsigned=mantissa:gsub('^%-','')
   assert(unsigned:match('^%d+$') or unsigned:match('^%d+%.%d+$'),'Invalid JSON number')
   assert(not unsigned:match('^0%d'),'Invalid JSON leading zero')
   value=tonumber(number);assert(value and value==value and math.abs(value)~=math.huge,'Invalid JSON number');at=at+#number
  end
  depth=depth-1;return value
 end
 local result=parse();ws();assert(at>#text,'Trailing JSON input');return result
end
return M
