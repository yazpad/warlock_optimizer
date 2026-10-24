local M={}
local function evenRound(n)
 local base=math.floor(n);local remainder=n-base
 if remainder>0.5 or (remainder==0.5 and base%2==1) then return base+1 end
 return base
end
-- IEEE 754 binary32, round to nearest/even. No FFI or graphics context needed.
function M.floatBits(value)
 assert(type(value)=='number' and value==value and math.abs(value)~=math.huge,'Expected finite float')
 local sign=(value<0 or (value==0 and 1/value==-math.huge)) and 2147483648 or 0
 value=math.abs(value)
 if value==0 then return sign end
 local mantissa,exponent=math.frexp(value);exponent=exponent-1
 if exponent < -126 then return sign+evenRound(value*2^149) end
 local significand=evenRound(mantissa*2^24)
 if significand==2^24 then significand=2^23;exponent=exponent+1 end
 assert(exponent<=127,'Float exceeds binary32 range')
 return sign+(exponent+127)*2^23+significand-2^23
end
return M
