///
// Thread pool submodule.
//
// Processes share OS threads from a dynamic thread pool cooperatively.
// Threads can be created and destoyed at any time. 
// Threads are scheduled to be destroyed the as soon as they
// become IDLE.
//
// @module pool
// @author Tiago Salmito
// @license MIT
// @copyright Tiago Salmito - 2014

#include <stdlib.h>
#include "process.h"
#include "pool.h"
#include "scheduler.h"

///
// Creates a new pool
//
// @int[opt=1] size initial size of the pool. 
// @treturn pool the new pool
// @function new

#define DEFAULT_QUEUE_CAPACITY -1

///
// Pool type.
//
// Any function in this section belongs to `pool` type methods.
// @type pool

static void get_metatable(lua_State * L);

pool_t clp_topool(lua_State *L, int i) {
	pool_t * p = luaL_checkudata (L, i, CLP_POOL_METATABLE);
	luaL_argcheck (L, p != NULL, i, "Pool expected");
	return *p;
}

void clp_buildpool(lua_State * L,pool_t t) {
	pool_t *s=lua_newuserdata(L,sizeof(pool_t *));
	*s=t;
	get_metatable(L);
	lua_setmetatable(L,-2);
}

static int pool_tostring (lua_State *L) {
	pool_t * s = luaL_checkudata (L, 1, CLP_POOL_METATABLE);
	lua_pushfstring (L, "Pool (%p)", *s);
	return 1;
}

static int pool_ptr(lua_State * L) {
	pool_t * s = luaL_checkudata (L, 1, CLP_POOL_METATABLE);
	lua_pushlightuserdata(L,*s);
	return 1;
}

///
// Schedule the addition on threads to the pool.
//
// @tparam int number of new threads
// @treturn pool a pool object
// @function add
static int pool_addthread(lua_State * L) {
	pool_t s=clp_topool(L, 1);
	int size=luaL_optint(L, 2, 1);
	CHANNEL_LOCK(s);
	if(size<0) {
		luaL_error(L,"argument must be positive or zero");
	}
	int i;
	for(i=0;i<size;i++) {
		clp_newthread(L,s);
		_DEBUG("pool_addthread pool:%p\n",s);
	}
	s->size+=size;
	CHANNEL_UNLOCK(s);
	return size;
}

///
// Return the current size of the pool
// @treturn int the current size
// @function size
static int pool_size(lua_State * L) {
	pool_t s = clp_topool(L, 1);
	CHANNEL_LOCK(s);
	lua_pushinteger(L,s->size);
	CHANNEL_UNLOCK(s);
	return 1;
}

///
// Schedule the destruction of single thread to the pool.
//
// Threads are scheduled to be destroyed the as soon as they
// become IDLE.
//
// @function kill
static int pool_killthread(lua_State * L) {
	pool_t pool=clp_topool(L, 1);
	void *a=NULL;
	clp_lfqueue_push(pool->ready,(void**)&a);
	return 0;
}


static int pool_eq(lua_State * L) {
	pool_t p1=clp_topool(L, 1);
	pool_t p2=clp_topool(L, 2);
	lua_pushboolean(L,p1==p2);
	return 1;
}


static void get_metatable(lua_State * L) {
	luaL_getmetatable(L,CLP_POOL_METATABLE);
	if(lua_isnil(L,-1)) {
		lua_pop(L,1);
		luaL_newmetatable(L,CLP_POOL_METATABLE);
		lua_pushvalue(L,-1);
		lua_setfield(L,-2,"__index");
		lua_pushcfunction (L, pool_tostring);
		lua_setfield (L, -2,"__tostring");
		luaL_loadstring(L,"local ptr=(...):ptr() return function() return require'clp.pool'.get(ptr) end");
		lua_setfield (L, -2,"__wrap");
		lua_pushcfunction (L, pool_eq);
		lua_setfield (L, -2,"__eq");
		lua_pushcfunction(L,pool_ptr);
		lua_setfield(L,-2,"ptr");
		lua_pushcfunction(L,pool_size);
		lua_setfield(L,-2,"size");
		lua_pushcfunction(L,pool_addthread);
		lua_setfield(L,-2,"add");
		lua_pushcfunction(L,pool_killthread);
		lua_setfield(L,-2,"kill");
	}
}

static int pool_new(lua_State *L) {
	int size=lua_tointeger(L,1);
	_DEBUG("pool_new %d\n",size);
	if(size<0) luaL_error(L,"Initial pool size must be greater than zero");
	pool_t p=malloc(sizeof(struct pool_s));
	p->size=0;
	p->lock=0;
	p->ready=clp_lfqueue_new();
	clp_lfqueue_setcapacity(p->ready,-1);
	clp_buildpool(L,p);
	lua_pushcfunction(L,pool_addthread);
	lua_pushvalue(L,-2);
	lua_pushnumber(L,size);
	lua_call(L,2,0);
	return 1;
}

static int pool_get(lua_State * L) {
	pool_t p=lua_touserdata(L,1);
	if(p) {
		_DEBUG("pool_get %p\n",p);
		clp_buildpool(L,p);
		return 1;
	}
	lua_pushnil(L);
	lua_pushliteral(L,"Pool is null");
	return 2;
}

static const struct luaL_Reg LuaExportFunctions[] = {
	{"new",pool_new},
	{"get",pool_get},
	{NULL,NULL}
};

CLP_EXPORTAPI int luaopen_clp_pool(lua_State *L) {
	lua_newtable(L);
	lua_newtable(L);
	luaL_loadstring(L,"return function() return require'clp.pool' end");
	lua_setfield (L, -2,"__persist");
	lua_setmetatable(L,-2);
#if LUA_VERSION_NUM < 502
	luaL_register(L, NULL, LuaExportFunctions);
#else
	luaL_setfuncs(L, LuaExportFunctions, 0);
#endif        
	return 1;
};
