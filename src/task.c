/// 
// Process submodule.
//
// A process is a lightweight thread of execution with its own
// independent state.
//
// @module process
// @author Tiago Salmito
// @license MIT
// @copyright Tiago Salmito - 2014


#include "task.h"
#include "marshal.h"
#include "scheduler.h"

#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>


#define DEFAULT_I_IDLE_CAPACITY 10
#define DEFAULT_QUEUE_CAPACITY -1
#define CLP_TASK_CACHE "clp-process-cache"


static void get_taskmetatale (lua_State * L);
static int task_instantiate (lua_State * L);

extern pool_t clp_defaultpool;

task_t clp_totask (lua_State * L, int i) {
	task_t *s = luaL_checkudata (L, i, CLP_TASK_METATABLE);
	luaL_argcheck (L, s != NULL, i, "Process expected");
	return *s;
}

///
// Creates a new process.
//
// If called without parameters, creates a new empty process 
// without environment. It can be set later with 
// the @{process:wrap} function.
//
// If `f` is a function then it will be used as the new process environment.
// Any upvalues will be copied to the state of the new process.
//
// If `e` is a function then it will be used as the error 
// function of the new process. It will be called whenever the process 
// raise a error. The process is destroyed after it returns.
//
// All spawned processes share the same input channel.
//
// @tparam[opt] function f environment of the process.
// @tparam[optchain] function e error function of the process.
// @tparam[opt=1] number n number of processes to create.
// @return the new process
// @function new

///
// Tests whether the userdata is a process
// @tparam process p value to be tested
// @return true if it has the process type
//
// @function isprocess


///
// Our class. Any function or table in this section belongs to `process`
// @type process

///
// get the input channel of the process.
//
// @return the input channel
//
// @function input
static int task_input (lua_State * L) {
	task_t s = clp_totask (L, 1);
	clp_pushchannel(L,s->input);
	return 1;
}

///
// Changes the input channel of the process.
//
// @tparam channel c the new input channel
//
// @function setinput
static int task_setinput (lua_State * L) {
	task_t s = clp_totask (L, 1);
	channel_t c=clp_tochannel(L, 2);
	s->input=c;
	lua_pushvalue(L,1);
	return 1;
}

///
// Get the number of spawned processes.
//
// @return the number of process instances
//
// @function size
static int get_max_instances (lua_State * L) {
	task_t s = clp_totask (L, 1);
	lua_pushnumber (L, s->instances);
	return 1;
}

///
// Get the environment function of a process
//
// @treturn function the environment function
//
// @function env
static int task_getenv (lua_State * L) {
	lua_getfield(L, LUA_REGISTRYINDEX, CLP_HANDLER_KEY);
	if(lua_type(L,-1)!=LUA_TNIL) {
		return 1;
	}
	lua_pop(L,1);
	task_t s = clp_totask (L, 1);
	if (s->env == NULL){
		lua_pushnil (L);
	} else {
		lua_pushcfunction(L,mar_decode);
		lua_pushlstring (L, s->env, s->env_len);
		lua_call(L,1,1);
	}
	return 1;
}

static int task_eq (lua_State * L) {
	task_t s1 = clp_totask (L, 1);
	task_t s2 = clp_totask (L, 2);
	lua_pushboolean (L, s1 == s2);
	return 1;
}

///
// Wraps an empty process with an environment and error function.
// 
// A process can be wrapped only once.
//
// Has the same parameters of the @{new} function.
// 
// @tparam[opt] function f environment of the process.
// @tparam[optchain] function e error function of the process.
// @tparam[opt=1] number n number of processes to create.
//
// @function wrap
// @treturn process itself
// @see new
static int task_wrap (lua_State * L) {
	int top=lua_gettop(L);
	task_t s = clp_totask (L, 1);
	if (s->env != NULL)
		luaL_error (L, "Process already have a environment");

	luaL_checktype (L, 2, LUA_TFUNCTION);
	lua_pushcfunction (L, mar_encode);
	lua_newtable(L);
	lua_pushvalue (L, 2);
	lua_setfield(L,-2,"f");
	if(lua_type(L,3) == LUA_TFUNCTION && top>=3) {   
		lua_pushvalue (L, 3);
		lua_setfield(L,-2,"e");
	}
	lua_call (L, 1, 1);

	const char *env = NULL;
	size_t len = 0;
	env = lua_tolstring (L, -1, &len);
	char *envcp = malloc (len + 1);
	envcp[len] = '\0';
	memcpy (envcp, env, len + 1);
	s->env = envcp;
	s->env_len = len;
	lua_pop (L, 1);

	lua_pushcfunction (L, task_instantiate);
	lua_pushvalue (L, 1);
	lua_pushnumber (L, 1);
	lua_call (L, 2, 0);

	lua_pushvalue (L, 1);
	return 1;
}

static int task_tostring (lua_State * L) {
	task_t *s = luaL_checkudata (L, 1, CLP_TASK_METATABLE);
	lua_pushfstring (L, "Process (%p)", *s);
	return 1;
}


static int task_destroyinstances (lua_State * L) {
	task_t s = clp_totask (L, 1);
	int n = luaL_optint(L, 2, 0);
	if (n < 0)
		luaL_error (L, "Argument must be positive");
	if (n == 0) {
		lua_pushvalue(L,1);
		return 1;
	}

	MUTEX_LOCK(&s->intances_mutex);
	s->instances -= n;
	MUTEX_UNLOCK(&s->intances_mutex);

	lua_pushvalue(L,1);
	return 1;
}

///
// Create a new process from an existing one.
//
// All spawned processes share the same input channel.
// @int n number of new processes
// @treturn process itself
// @function spawn
static int task_instantiate (lua_State * L) {
	task_t s = clp_totask (L, 1);
	if (s->pool == NULL)
		luaL_error (L, "Process must be associated to a pool");
	if (s->env == NULL)
		luaL_error (L, "Process must have an environment");
	int n = lua_tointeger (L, 2);
	int i;
	if (n < 0)
		luaL_error (L, "Argument must be positive");
	if (n == 0) {
		lua_pushvalue(L,1);
		return 1;
	}

	MUTEX_LOCK(&s->intances_mutex);
	s->instances += n;
	for (i = 0; i < n; i++) {
		(void) clp_newinstance (s);
	}
	MUTEX_UNLOCK(&s->intances_mutex);


	/*unlock mutex */
	lua_pushvalue (L, 1);
	return 1;
}

static int task_ptr (lua_State * L) {
	task_t *s = luaL_checkudata (L, 1, CLP_TASK_METATABLE);
	lua_pushlightuserdata (L, *s);
	return 1;
}

static void task_getcached(lua_State * L, task_t t) {
	lua_pushliteral(L,CLP_TASK_CACHE);
	lua_gettable(L,LUA_REGISTRYINDEX);
	if(!lua_istable(L,-1)) {
		lua_pop(L,1);
		lua_newtable(L);
		lua_newtable(L);
		lua_pushliteral(L,"v");
		lua_setfield(L,-2,"__mode");
		lua_setmetatable(L,-2);
		lua_pushliteral(L,CLP_TASK_CACHE);
		lua_pushvalue(L,-2);
		lua_settable(L,LUA_REGISTRYINDEX);
	}
	lua_pushlightuserdata(L,t);
	lua_gettable(L,-2);
	lua_remove(L,-2);
}

static void task_putcache(lua_State * L, task_t t) {
	lua_pushliteral(L,CLP_TASK_CACHE);
	lua_gettable(L,LUA_REGISTRYINDEX);
	if(!lua_isuserdata(L,-1)) {
		lua_pop(L,1);
		lua_newtable(L);
		lua_newtable(L);
		lua_pushliteral(L,"v");
		lua_setfield(L,-2,"__mode");
		lua_setmetatable(L,-2);
		lua_pushliteral(L,CLP_TASK_CACHE);
		lua_pushvalue(L,-2);
		lua_settable(L,LUA_REGISTRYINDEX);
	}
	lua_pushlightuserdata(L,t);
	lua_pushvalue(L,-3);
	lua_settable(L,-3);
	lua_pop(L,1);
}

void clp_buildtask (lua_State * L, task_t t) {
	_DEBUG("Building task %p\n",t);
	task_getcached(L,t);
	if(lua_type(L,-1)==LUA_TUSERDATA) 
		return;
	lua_pop(L,1);
	task_t *s = lua_newuserdata (L, sizeof (task_t *));
	*s = t;
	get_taskmetatale (L);
	lua_setmetatable (L, -2);
	task_putcache(L,t);
	_DEBUG("Created userdata %p\n",t);

}

///
// Get the current pool associated to the process
//
// @treturn pool the pool of the process
// @function pool
static int task_getpool (lua_State * L) {
	if(lua_gettop(L)>1) {
		luaL_error(L,"To many arguments");
	}

	task_t s = clp_totask (L, 1);
	if (s->pool)
		clp_buildpool (L, s->pool);
	else
		lua_pushnil (L);
	return 1;
}
///
// Associate a pool to the process
//
// @tparam pool the pool to be associated
// @treturn process itself
// @function pool
static int task_setpool (lua_State * L) {
	task_t s = clp_totask (L, 1);
	pool_t p = clp_topool (L, 2);
	s->pool = p;
	lua_pop(L,1);
	return 1;
}

///
// Get the parent of the current process
//
// @treturn[1] process the parent process
// @treturn[2] nil if the process has no parent.
// @function parent
static int task_getparent (lua_State * L) {
	task_t s = clp_totask (L, 1);
	if (s->parent)
		clp_buildtask (L, s->parent);
	else
		lua_pushnil (L);
	return 1;
}

static int task_gc (lua_State * L) {
	//	task_t s = clp_totask (L, 1);
	//	_DEBUG("Destroying task %p\n",s);
	return 0;
}


static const struct luaL_Reg TaskMetaFunctions[] = {
	{"__eq", task_eq},
	{"__gc", task_gc},
	{"__tostring", task_tostring},
	//	{"__call", task_push},
	{"__id", task_ptr},
	{"size", get_max_instances},
	{"env", task_getenv},
	{"wrap", task_wrap},
	{"input", task_input},
	{"setinput", task_setinput},
	{"spawn", task_instantiate},
	{"remove", task_destroyinstances},
	{"parent", task_getparent},
	{"pool", task_getpool},
	{"setpool", task_setpool},
	{NULL, NULL}
};

static void get_taskmetatale (lua_State * L) {
	luaL_getmetatable (L, CLP_TASK_METATABLE);
	if (lua_isnil (L, -1))
	{
		lua_pop (L, 1);
		luaL_newmetatable (L, CLP_TASK_METATABLE);
		LUA_REGISTER (L, TaskMetaFunctions);
		lua_pushvalue (L, -1);
		lua_setfield (L, -2, "__index");
		luaL_loadstring (L,
				"local t=(...) assert(t:input():put(select(2,...))) return t");
		lua_setfield (L, -2, "__call");				   
		luaL_loadstring (L,
				"local ptr=(...):__id() return function() return require'clp.process'.get(ptr) end");
		lua_setfield (L, -2, "__wrap");
	}
}


static int task_istask (lua_State * L) {
	lua_getmetatable (L, 1);
	get_taskmetatale (L);
	int has = 0;
#if LUA_VERSION_NUM > 501
	if (lua_compare (L, -1, -2, LUA_OPEQ))
		has = 1;
#else
	if (lua_equal (L, -1, -2))
		has = 1;
#endif
	lua_pop (L, 2);
	lua_pushboolean (L, has);
	return 1;
}

static int clp_newtask (lua_State * L) {
	int idle = 0;
	task_t *task_ = NULL;
	int top=lua_gettop(L);
	if (top==0) {
		task_ = lua_newuserdata (L, sizeof (task_t *));
		(*task_) = malloc (sizeof (struct clp_Task));
		(*task_)->env = NULL;
		(*task_)->env_len = 0;
	}
	else {
		luaL_checktype (L, 1, LUA_TFUNCTION);
		if(lua_type(L,2) == LUA_TNUMBER) { 
			idle = luaL_optint (L, 2, 1);
		} else if(lua_type(L,3) == LUA_TNUMBER) {
			idle = luaL_optint (L, 3, 1);
		} else {
			idle=1;
		}
		lua_pushcfunction (L, mar_encode);
		lua_newtable(L);
		lua_pushvalue (L, 1);
		lua_setfield(L,-2,"f");
		if(lua_type(L,2) == LUA_TFUNCTION && top>=2) {
			lua_pushvalue (L, 2);
			lua_setfield(L,-2,"e");
		}
		lua_call (L, 1, 1);
		const char *env = NULL;
		size_t len = 0;
		env = lua_tolstring (L, -1, &len);
		task_ = lua_newuserdata (L, sizeof (task_t *));
		(*task_) = calloc(1, sizeof (struct clp_Task));
		char *envcp = malloc ((sizeof(char))*len);
		memcpy (envcp, env, len);
		(*task_)->env = envcp;
		(*task_)->env_len = len; 
		lua_remove (L, -2);
	}
	task_t task = *task_;
	//instance queue initialization
	task->instances = 0;
	MUTEX_INIT(&task->intances_mutex);
	//create input channel
	lua_pushcfunction(L,clp_channelnew);
	lua_call(L,0,1);
	lua_getfield(L,-1,"__id");
	lua_insert(L,-2);
	lua_call(L,1,1);
	task->input=(channel_t)lua_touserdata(L,-1);
	lua_pop(L,1);
	//initialize thread pool
	task->pool = clp_defaultpool;
	//assign metatable
	get_taskmetatale (L);
	lua_setmetatable (L, -2);
	//initialize intances
	if (idle > 0) {
		lua_pushcfunction (L, task_instantiate);
		lua_pushvalue (L, -2);
		lua_pushnumber (L, idle);
		lua_call (L, 2, 0);
	}
	//initialize parent
	task->parent = NULL;
	lua_pushliteral (L, CLP_INSTANCE_KEY);
	lua_gettable (L, LUA_REGISTRYINDEX);
	if (lua_type (L, -1) == LUA_TLIGHTUSERDATA)
	{
		instance_t i = lua_touserdata (L, -1);
		task->parent = i->task;
	}
	lua_pop (L, 1);

	return 1;
}

static int clp_destroytask (lua_State * L) {
	task_t *s_ptr = luaL_checkudata (L, 1, CLP_TASK_METATABLE);
	if (!s_ptr)
		return 0;
	if (!(*s_ptr))
		return 0;
	task_t s = *s_ptr;
	if (s->env != NULL)
		free (s->env);
	/*	while (ltask_lfqueue_try_pop (s->instances, &i))
		clp_destroyinstance (i);
		ltask_lfqueue_free (s->instances);*/
	s->input=NULL;
	*s_ptr = 0;
	return 0;
}

static int clp_gettask (lua_State * L) {
	task_t s = lua_touserdata (L, 1);
	if (s)
	{
		clp_buildtask (L, s);
		return 1;
	}
	lua_pushnil (L);
	lua_pushliteral (L, "Process not found");
	return 2;
}

static const struct luaL_Reg LuaExportFunctions[] = {
	{"new", clp_newtask},
	{"get", clp_gettask},
	{"destroy", clp_destroytask},
	{"isprocess", task_istask},
	{NULL, NULL}
};

CLP_EXPORTAPI int luaopen_clp_process (lua_State * L) {
	lua_newtable (L);
	lua_newtable (L);
	luaL_loadstring (L,
			"return function() return require'clp.process' end");
	lua_setfield (L, -2, "__persist");
	lua_setmetatable (L, -2);
#if LUA_VERSION_NUM < 502
	luaL_register (L, NULL, LuaExportFunctions);
#else
	luaL_setfuncs (L, LuaExportFunctions, 0);
#endif
	return 1;
};


static const struct luaL_Reg InstanceLibs[] = {
	{"io",luaopen_io},
	{"os",luaopen_os},
	{"table",luaopen_table},
	{"string",luaopen_string},
	{"math",luaopen_math},
	{"debug",luaopen_debug},
#if LUA_VERSION_NUM > 501
	{"coroutine",luaopen_coroutine},
#endif
	{NULL,NULL}
};

void clp_initinstance(instance_t i) {
	_DEBUG("Initiating instance %p\n",i);
	lua_State *L=i->L;
	lua_pushliteral(L,CLP_INSTANCE_KEY);
	lua_pushlightuserdata(L,i);
	lua_settable(L, LUA_REGISTRYINDEX);	
	lua_pushcfunction(L,luaopen_base);
	lua_pcall(L,0,0,0);
	lua_pushcfunction(L,luaopen_package);
	lua_pcall(L,0,1,0);
	lua_getfield(L,-1,"preload");
	LUA_REGISTER(L,InstanceLibs);
	lua_pop(L,2);
	//	luaL_openlibs(L);
	lua_pushliteral(L,TASK_HANDLER_KEY);
	const char buff[]="local a={...} "
		"local h=a[1].f "
		"local s=a[2] "
		"a[1].e = a[1].e or function(e) return e end "
		"return require'coroutine'.wrap(function() while true do h(s:input():get()) end end)\n";
	luaL_loadbuffer (L,buff, strlen(buff), "Process");
	lua_pushcfunction(L,mar_decode);
	lua_pushlstring(L,i->task->env,i->task->env_len);
	lua_call(L,1,1);
	lua_pushvalue(L,-1);
	lua_setfield(L, LUA_REGISTRYINDEX,CLP_ENV_KEY);
	clp_buildtask(L,i->task);
	lua_call(L,2,1);
	lua_settable(L, LUA_REGISTRYINDEX);
	lua_getfield(L, LUA_REGISTRYINDEX,CLP_ENV_KEY);
	lua_getfield(L,-1,"e");
	lua_setfield(L,LUA_REGISTRYINDEX,CLP_ERRORFUNCTION_KEY);
	lua_pop(L,1);
	i->state=I_READY;
}

instance_t clp_newinstance(task_t s) {
	lua_State * L = luaL_newstate();
	instance_t i=malloc(sizeof(struct instance_s));
	i->L=L;
	i->task=s;
	i->state=I_CREATED;
	i->chan=NULL;
	i->ev=NULL;
	clp_pushinstance(i);
	return i;
}

void clp_destroyinstance(instance_t i) {
	lua_close(i->L);
	if(i->ev) clp_destroyevent(i->ev);
	free(i);
}

