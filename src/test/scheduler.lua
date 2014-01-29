local s=require'leda.stage'
local sched=require'leda.scheduler'

local stage3=s.new(function(...) 
	print('third',...)
end)
stage3:instantiate(10)
local stage=s.new(function(...) 
	print('aew',self,stage3,...)
	
	local stage2=s.new(function(s1,...) 
		print('stage2',s1,...)
		s1:push('OK')
	end)
	stage2:push(stage3,'yeah')
	stage2:instantiate(1)
	print(stage2:instances())
end,0,true)
stage:push(24,"event",math.pi)
stage:instantiate(1)
local t=sched:new_thread()
print(t)
t:join(1)
print("timed out")
print(stage,"instances:",stage:instances())
print(stage3,"instances:",stage3:instances())
local n=stage3:free(9)
print(stage3,"destroyed:",n,stage3:instances())
t:rawkill()
