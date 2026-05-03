/*
** $Id: lparser.c $
** Lua Parser
** See Copyright Notice in lua.h
*/

#define lparser_c
#define LUA_CORE

#include "lprefix.h"


#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "mos.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"



/* maximum number of variable declarations per function (must be
   smaller than 250, due to the bytecode format) */
#define MAXVARS		200


#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)


/* because all strings are unified by the scanner, the parser
   can use pointer equality for string equality */
#define eqstr(a,b)	((a) == (b))


/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* chain */
  int firstlabel;  /* index of first label in this block */
  int firstgoto;  /* index of first pending goto in this block */
  short nactvar;  /* number of active declarations at block entry */
  lu_byte upval;  /* true if some variable in the block is an upvalue */
  lu_byte isloop;  /* 1 if 'block' is a loop; 2 if it has pending breaks */
  lu_byte insidetbc;  /* true if inside the scope of a to-be-closed var. */
} BlockCnt;



/*
** prototypes for recursive non-terminal functions
*/
static void statement (LexState *ls);
static void expr (LexState *ls, expdesc *v);


static l_noret error_expected (LexState *ls, int token) {
  luaX_syntaxerror(ls,
      luaO_pushfstring(ls->L, "%s expected", luaX_token2str(ls, token)));
}


static l_noret errorlimit (FuncState *fs, int limit, const char *what) {
  lua_State *L = fs->ls->L;
  const char *msg;
  int line = fs->f->linedefined;
  const char *where = (line == 0)
                      ? "main function"
                      : luaO_pushfstring(L, "function at line %d", line);
  msg = luaO_pushfstring(L, "too many %s (limit is %d) in %s",
                             what, limit, where);
  luaX_syntaxerror(fs->ls, msg);
}


void luaY_checklimit (FuncState *fs, int v, int l, const char *what) {
  if (l_unlikely(v > l)) errorlimit(fs, l, what);
}


/*
** Test whether next token is 'c'; if so, skip it.
*/
static int testnext (LexState *ls, int c) {
  if (ls->t.token == c) {
    luaX_next(ls);
    return 1;
  }
  else return 0;
}


/*
** Check that next token is 'c'.
*/
static void check (LexState *ls, int c) {
  if (ls->t.token != c)
    error_expected(ls, c);
}


/*
** Check that next token is 'c' and skip it.
*/
static void checknext (LexState *ls, int c) {
  check(ls, c);
  luaX_next(ls);
}


#define check_condition(ls,c,msg)	{ if (!(c)) luaX_syntaxerror(ls, msg); }


/*
** Check that next token is 'what' and skip it. In case of error,
** raise an error that the expected 'what' should match a 'who'
** in line 'where' (if that is not the current line).
*/
static void check_match (LexState *ls, int what, int who, int where) {
  if (l_unlikely(!testnext(ls, what))) {
    if (where == ls->linenumber)  /* all in the same line? */
      error_expected(ls, what);  /* do not need a complex message */
    else {
      luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
             "%s expected (to close %s at line %d)",
              luaX_token2str(ls, what), luaX_token2str(ls, who), where));
    }
  }
}


static TString *str_checkname (LexState *ls) {
  TString *ts;
  check(ls, TK_NAME);
  ts = ls->t.seminfo.ts;
  luaX_next(ls);
  return ts;
}


static void init_exp (expdesc *e, expkind k, int i) {
  e->f = e->t = NO_JUMP;
  e->k = k;
  e->u.info = i;
  e->etype = MOS_TYPE_UNKNOWN;  /* Mos: Initialize expression type */
}


static void codestring (expdesc *e, TString *s) {
  e->f = e->t = NO_JUMP;
  e->k = VKSTR;
  e->u.strval = s;
}


static void codename (LexState *ls, expdesc *e) {
  codestring(e, str_checkname(ls));
}


/*
** Register a new local variable in the active 'Proto' (for debug
** information).
*/
static short registerlocalvar (LexState *ls, FuncState *fs,
                               TString *varname) {
  Proto *f = fs->f;
  int oldsize = f->sizelocvars;
  luaM_growvector(ls->L, f->locvars, fs->ndebugvars, f->sizelocvars,
                  LocVar, SHRT_MAX, "local variables");
  while (oldsize < f->sizelocvars)
    f->locvars[oldsize++].varname = NULL;
  f->locvars[fs->ndebugvars].varname = varname;
  f->locvars[fs->ndebugvars].startpc = fs->pc;
  luaC_objbarrier(ls->L, f, varname);
  return fs->ndebugvars++;
}


/*
** Create a new variable with the given 'name' and given 'kind'.
** Return its index in the function.
*/
static int new_varkind (LexState *ls, TString *name, lu_byte kind) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  Vardesc *var;
  luaM_growvector(L, dyd->actvar.arr, dyd->actvar.n + 1,
             dyd->actvar.size, Vardesc, SHRT_MAX, "variable declarations");
  var = &dyd->actvar.arr[dyd->actvar.n++];
  var->vd.kind = kind;  /* default */
  var->vd.name = name;
  var->vd.vtype = MOS_TYPE_UNKNOWN;  /* Mos: no default type - must be declared */
  return dyd->actvar.n - 1 - fs->firstlocal;
}


/*
** Create a new local variable with the given 'name' and regular kind.
*/
static int new_localvar (LexState *ls, TString *name) {
  return new_varkind(ls, name, VDKREG);
}

/* Mos: Create a new local variable with explicit type */
static int new_localvar_typed (LexState *ls, TString *name, int type);

/* Mos: Convert token to type code */
static int token_to_type (int token) {
  switch (token) {
    case TK_TYPE_INT: return MOS_TYPE_INT;
    case TK_TYPE_FLOAT: return MOS_TYPE_FLOAT;
    case TK_TYPE_BOOL: return MOS_TYPE_BOOL;
    case TK_TYPE_STRING: return MOS_TYPE_STRING;
    case TK_TYPE_VOID: return MOS_TYPE_VOID;
    default: return MOS_TYPE_UNKNOWN;
  }
}

/* Mos: Get type name string */
static const char* type_to_string (int type) {
  switch (type) {
    case MOS_TYPE_INT: return "int";
    case MOS_TYPE_FLOAT: return "float";
    case MOS_TYPE_BOOL: return "bool";
    case MOS_TYPE_STRING: return "String";
    case MOS_TYPE_VOID: return "void";
    default: return "unknown";
  }
}

/* Mos: Type checking - check if types are compatible */
static int types_compatible (int expected, int actual) {
  if (expected == MOS_TYPE_UNKNOWN || actual == MOS_TYPE_UNKNOWN)
    return 1;  /* unknown types are compatible with anything */
  return expected == actual;
}

/* Mos: Report type error */
static void type_error (LexState *ls, const char *msg, int line) {
  luaO_pushfstring(ls->L, "type error at line %d: %s", line, msg);
  luaX_syntaxerror(ls, luaO_pushfstring(ls->L, "%s", msg));
}

/* Mos: Get the type of an expression */
static int get_expr_type (expdesc *e) {
  /* Mos: First check etype which is set by type propagation */
  if (e->etype != MOS_TYPE_UNKNOWN)
    return e->etype;
  /* Fall back to inferring type from expression kind */
  switch (e->k) {
    case VKINT:
      return MOS_TYPE_INT;
    case VKFLT:
      return MOS_TYPE_FLOAT;
    case VKSTR:
      return MOS_TYPE_STRING;
    case VTRUE:
    case VFALSE:
      return MOS_TYPE_BOOL;
    case VNIL:
      return MOS_TYPE_VOID;
    default:
      return MOS_TYPE_UNKNOWN;
  }
}

/* Mos: Check assignment type compatibility */
static void check_assignment_type (LexState *ls, Vardesc *var, expdesc *e) {
  int var_type = var->vd.vtype;
  int expr_type = get_expr_type(e);
  
  if (var_type != MOS_TYPE_UNKNOWN && expr_type != MOS_TYPE_UNKNOWN) {
    if (!types_compatible(var_type, expr_type)) {
      char msg[256];
      snprintf(msg, sizeof(msg), "cannot assign '%s' to '%s' variable",
               type_to_string(expr_type), type_to_string(var_type));
      type_error(ls, msg, ls->linenumber);
    }
  }
}

/* Mos: Check binary operation type compatibility */
static void check_binop_type (LexState *ls, BinOpr op, expdesc *e1, expdesc *e2, int line) {
  int t1 = e1->etype;
  int t2 = e2->etype;
  
  /* Skip check if types are unknown */
  if (t1 == MOS_TYPE_UNKNOWN || t2 == MOS_TYPE_UNKNOWN)
    return;
  
  /* For arithmetic operations, both operands must be numeric */
  if (op >= OPR_ADD && op <= OPR_DIV) {
    /* String + String/Int/Float is concatenation, allowed */
    if (t1 == MOS_TYPE_STRING || t2 == MOS_TYPE_STRING) {
      /* String concatenation with + operator: "hello" + 42 -> "hello42" */
    }
    else {
      int both_numeric = 0;
      
      if ((t1 == MOS_TYPE_INT || t1 == MOS_TYPE_FLOAT) &&
          (t2 == MOS_TYPE_INT || t2 == MOS_TYPE_FLOAT)) {
        both_numeric = 1;
      }
      
      if (!both_numeric) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cannot perform arithmetic on '%s' and '%s'",
                 type_to_string(t1), type_to_string(t2));
        type_error(ls, msg, line);
      }
    }
  }
  /* For comparison operations */
  else if (op >= OPR_EQ && op <= OPR_LE) {
    /* Comparisons are allowed between same types only */
    if (t1 != t2) {
      char msg[256];
      snprintf(msg, sizeof(msg), "cannot compare '%s' with '%s'",
               type_to_string(t1), type_to_string(t2));
      type_error(ls, msg, line);
    }
  }
  /* For string concatenation */
  else if (op == OPR_CONCAT) {
    if (t1 != MOS_TYPE_STRING || t2 != MOS_TYPE_STRING) {
      char msg[256];
      snprintf(msg, sizeof(msg), "cannot concatenate '%s' with '%s'",
               type_to_string(t1), type_to_string(t2));
      type_error(ls, msg, line);
    }
  }
  /* For bitwise operations, both must be integers */
  else if (op >= OPR_BAND && op <= OPR_SHR) {
    if (t1 != MOS_TYPE_INT || t2 != MOS_TYPE_INT) {
      char msg[256];
      snprintf(msg, sizeof(msg), "bitwise operations require integers, got '%s' and '%s'",
               type_to_string(t1), type_to_string(t2));
      type_error(ls, msg, line);
    }
  }
}

#define new_localvarliteral(ls,v) \
    new_localvar(ls,  \
      luaX_newstring(ls, "" v, (sizeof(v)/sizeof(char)) - 1));



/*
** Return the "variable description" (Vardesc) of a given variable.
** (Unless noted otherwise, all variables are referred to by their
** compiler indices.)
*/
static Vardesc *getlocalvardesc (FuncState *fs, int vidx) {
  return &fs->ls->dyd->actvar.arr[fs->firstlocal + vidx];
}

/* Mos: Create a new local variable with explicit type */
static int new_localvar_typed (LexState *ls, TString *name, int type) {
  int vidx = new_varkind(ls, name, VDKREG);
  /* Set the variable type */
  Vardesc *vd = getlocalvardesc(ls->fs, vidx);
  vd->vd.vtype = cast_byte(type);
  return vidx;
}


/*
** Convert 'nvar', a compiler index level, to its corresponding
** register. For that, search for the highest variable below that level
** that is in a register and uses its register index ('ridx') plus one.
*/
static lu_byte reglevel (FuncState *fs, int nvar) {
  while (nvar-- > 0) {
    Vardesc *vd = getlocalvardesc(fs, nvar);  /* get previous variable */
    if (varinreg(vd))  /* is in a register? */
      return cast_byte(vd->vd.ridx + 1);
  }
  return 0;  /* no variables in registers */
}


/*
** Return the number of variables in the register stack for the given
** function.
*/
lu_byte luaY_nvarstack (FuncState *fs) {
  return reglevel(fs, fs->nactvar);
}


/*
** Get the debug-information entry for current variable 'vidx'.
*/
static LocVar *localdebuginfo (FuncState *fs, int vidx) {
  Vardesc *vd = getlocalvardesc(fs,  vidx);
  if (!varinreg(vd))
    return NULL;  /* no debug info. for constants */
  else {
    int idx = vd->vd.pidx;
    lua_assert(idx < fs->ndebugvars);
    return &fs->f->locvars[idx];
  }
}


/*
** Create an expression representing variable 'vidx'
*/
static void init_var (FuncState *fs, expdesc *e, int vidx) {
  Vardesc *vd = getlocalvardesc(fs, vidx);
  e->f = e->t = NO_JUMP;
  e->k = VLOCAL;
  e->u.var.vidx = cast_short(vidx);
  e->u.var.ridx = vd->vd.ridx;
  e->etype = vd->vd.vtype;  /* Mos: Set expression type from variable type */
}


/*
** Raises an error if variable described by 'e' is read only; moreover,
** if 'e' is t[exp] where t is the vararg parameter, change it to index
** a real table. (Virtual vararg tables cannot be changed.)
*/
static void check_readonly (LexState *ls, expdesc *e) {
  FuncState *fs = ls->fs;
  TString *varname = NULL;  /* to be set if variable is const */
  switch (e->k) {
    case VCONST: {
      varname = ls->dyd->actvar.arr[e->u.info].vd.name;
      break;
    }
    case VLOCAL: case VVARGVAR: {
      Vardesc *vardesc = getlocalvardesc(fs, e->u.var.vidx);
      if (vardesc->vd.kind != VDKREG)  /* not a regular variable? */
        varname = vardesc->vd.name;
      break;
    }
    case VUPVAL: {
      Upvaldesc *up = &fs->f->upvalues[e->u.info];
      if (up->kind != VDKREG)
        varname = up->name;
      break;
    }
    case VVARGIND: {
      needvatab(fs->f);  /* function will need a vararg table */
      e->k = VINDEXED;
    }  /* FALLTHROUGH */
    case VINDEXUP: case VINDEXSTR: case VINDEXED: {  /* global variable */
      if (e->u.ind.ro)  /* read-only? */
        varname = tsvalue(&fs->f->k[e->u.ind.keystr]);
      break;
    }
    default:
      lua_assert(e->k == VINDEXI);  /* this one doesn't need any check */
      return;  /* integer index cannot be read-only */
  }
  if (varname)
    luaK_semerror(ls, "attempt to assign to const variable '%s'",
                      getstr(varname));
}


/*
** Start the scope for the last 'nvars' created variables.
*/
static void adjustlocalvars (LexState *ls, int nvars) {
  FuncState *fs = ls->fs;
  int reglevel = luaY_nvarstack(fs);
  int i;
  for (i = 0; i < nvars; i++) {
    int vidx = fs->nactvar++;
    Vardesc *var = getlocalvardesc(fs, vidx);
    var->vd.ridx = cast_byte(reglevel++);
    var->vd.pidx = registerlocalvar(ls, fs, var->vd.name);
    luaY_checklimit(fs, reglevel, MAXVARS, "local variables");
  }
}


/*
** Close the scope for all variables up to level 'tolevel'.
** (debug info.)
*/
static void removevars (FuncState *fs, int tolevel) {
  fs->ls->dyd->actvar.n -= (fs->nactvar - tolevel);
  while (fs->nactvar > tolevel) {
    LocVar *var = localdebuginfo(fs, --fs->nactvar);
    if (var)  /* does it have debug information? */
      var->endpc = fs->pc;
  }
}


/*
** Search the upvalues of the function 'fs' for one
** with the given 'name'.
*/
static int searchupvalue (FuncState *fs, TString *name) {
  int i;
  Upvaldesc *up = fs->f->upvalues;
  for (i = 0; i < fs->nups; i++) {
    if (eqstr(up[i].name, name)) return i;
  }
  return -1;  /* not found */
}


static Upvaldesc *allocupvalue (FuncState *fs) {
  Proto *f = fs->f;
  int oldsize = f->sizeupvalues;
  luaY_checklimit(fs, fs->nups + 1, MAXUPVAL, "upvalues");
  luaM_growvector(fs->ls->L, f->upvalues, fs->nups, f->sizeupvalues,
                  Upvaldesc, MAXUPVAL, "upvalues");
  while (oldsize < f->sizeupvalues)
    f->upvalues[oldsize++].name = NULL;
  return &f->upvalues[fs->nups++];
}


static int newupvalue (FuncState *fs, TString *name, expdesc *v) {
  Upvaldesc *up = allocupvalue(fs);
  FuncState *prev = fs->prev;
  if (v->k == VLOCAL) {
    up->instack = 1;
    up->idx = v->u.var.ridx;
    up->kind = getlocalvardesc(prev, v->u.var.vidx)->vd.kind;
    lua_assert(eqstr(name, getlocalvardesc(prev, v->u.var.vidx)->vd.name));
  }
  else {
    up->instack = 0;
    up->idx = cast_byte(v->u.info);
    up->kind = prev->f->upvalues[v->u.info].kind;
    lua_assert(eqstr(name, prev->f->upvalues[v->u.info].name));
  }
  up->name = name;
  luaC_objbarrier(fs->ls->L, fs->f, name);
  return fs->nups - 1;
}


/*
** Look for an active variable with the name 'n' in the
** function 'fs'. If found, initialize 'var' with it and return
** its expression kind; otherwise return -1. While searching,
** var->u.info==-1 means that the preambular global declaration is
** active (the default while there is no other global declaration);
** var->u.info==-2 means there is no active collective declaration
** (some previous global declaration but no collective declaration);
** and var->u.info>=0 points to the inner-most (the first one found)
** collective declaration, if there is one.
*/
static int searchvar (FuncState *fs, TString *n, expdesc *var) {
  int i;
  for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
    Vardesc *vd = getlocalvardesc(fs, i);
    if (varglobal(vd)) {  /* global declaration? */
      if (vd->vd.name == NULL) {  /* collective declaration? */
        if (var->u.info < 0)  /* no previous collective declaration? */
          var->u.info = fs->firstlocal + i;  /* this is the first one */
      }
      else {  /* global name */
        if (eqstr(n, vd->vd.name)) {  /* found? */
          init_exp(var, VGLOBAL, fs->firstlocal + i);
          return VGLOBAL;
        }
        else if (var->u.info == -1)  /* active preambular declaration? */
          var->u.info = -2;  /* invalidate preambular declaration */
      }
    }
    else if (eqstr(n, vd->vd.name)) {  /* found? */
      if (vd->vd.kind == RDKCTC)  /* compile-time constant? */
        init_exp(var, VCONST, fs->firstlocal + i);
      else {  /* local variable */
        init_var(fs, var, i);
        if (vd->vd.kind == RDKVAVAR)  /* vararg parameter? */
          var->k = VVARGVAR;
      }
      return cast_int(var->k);
    }
  }
  return -1;  /* not found */
}


/*
** Mark block where variable at given level was defined
** (to emit close instructions later).
*/
static void markupval (FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl->nactvar > level)
    bl = bl->previous;
  bl->upval = 1;
  fs->needclose = 1;
}


/*
** Mark that current block has a to-be-closed variable.
*/
static void marktobeclosed (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  bl->upval = 1;
  bl->insidetbc = 1;
  fs->needclose = 1;
}


/*
** Find a variable with the given name 'n'. If it is an upvalue, add
** this upvalue into all intermediate functions. If it is a global, set
** 'var' as 'void' as a flag.
*/
static void singlevaraux (FuncState *fs, TString *n, expdesc *var, int base) {
  int v = searchvar(fs, n, var);  /* look up variables at current level */
  if (v >= 0) {  /* found? */
    /* Mos: Set expression type from variable type */
    if (var->k == VLOCAL || var->k == VVARGVAR) {
      Vardesc *vd = getlocalvardesc(fs, var->u.var.vidx);
      var->etype = vd->vd.vtype;
    }
    if (!base) {
      if (var->k == VVARGVAR)  /* vararg parameter? */
        luaK_vapar2local(fs, var);  /* change it to a regular local */
      if (var->k == VLOCAL)
        markupval(fs, var->u.var.vidx);  /* will be used as an upvalue */
    }
    /* else nothing else to be done */
  }
  else {  /* not found at current level; try upvalues */
    int idx = searchupvalue(fs, n);  /* try existing upvalues */
    if (idx < 0) {  /* not found? */
      if (fs->prev != NULL)  /* more levels? */
        singlevaraux(fs->prev, n, var, 0);  /* try upper levels */
      if (var->k == VLOCAL || var->k == VUPVAL)  /* local or upvalue? */
        idx  = newupvalue(fs, n, var);  /* will be a new upvalue */
      else  /* it is a global or a constant */
        return;  /* don't need to do anything at this level */
    }
    init_exp(var, VUPVAL, idx);  /* new or old upvalue */
  }
}


static void buildglobal (LexState *ls, TString *varname, expdesc *var) {
  FuncState *fs = ls->fs;
  expdesc key;
  init_exp(var, VGLOBAL, -1);  /* global by default */
  singlevaraux(fs, ls->envn, var, 1);  /* get environment variable */
  if (var->k == VGLOBAL)
    luaK_semerror(ls, "%s is global when accessing variable '%s'",
                      LUA_ENV, getstr(varname));
  luaK_exp2anyregup(fs, var);  /* _ENV could be a constant */
  codestring(&key, varname);  /* key is variable name */
  luaK_indexed(fs, var, &key);  /* 'var' represents _ENV[varname] */
}


/*
** Find a variable with the given name 'n', handling global variables
** too.
*/
static void buildvar (LexState *ls, TString *varname, expdesc *var) {
  FuncState *fs = ls->fs;
  init_exp(var, VGLOBAL, -1);  /* global by default */
  singlevaraux(fs, varname, var, 1);
  if (var->k == VGLOBAL) {  /* global name? */
    int info = var->u.info;
    /* global by default in the scope of a global declaration? */
    if (info == -2)
      luaK_semerror(ls, "variable '%s' not declared", getstr(varname));
    buildglobal(ls, varname, var);
    if (info != -1 && ls->dyd->actvar.arr[info].vd.kind == GDKCONST)
      var->u.ind.ro = 1;  /* mark variable as read-only */
    else  /* anyway must be a global */
      lua_assert(info == -1 || ls->dyd->actvar.arr[info].vd.kind == GDKREG);
  }
}


static void singlevar (LexState *ls, expdesc *var) {
  buildvar(ls, str_checkname(ls), var);
}


/*
** Adjust the number of results from an expression list 'e' with 'nexps'
** expressions to 'nvars' values.
*/
static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) {
  FuncState *fs = ls->fs;
  int needed = nvars - nexps;  /* extra values needed */
  luaK_checkstack(fs, needed);
  if (hasmultret(e->k)) {  /* last expression has multiple returns? */
    int extra = needed + 1;  /* discount last expression itself */
    if (extra < 0)
      extra = 0;
    luaK_setreturns(fs, e, extra);  /* last exp. provides the difference */
  }
  else {
    if (e->k != VVOID)  /* at least one expression? */
      luaK_exp2nextreg(fs, e);  /* close last expression */
    if (needed > 0)  /* missing values? */
      luaK_nil(fs, fs->freereg, needed);  /* complete with nils */
  }
  if (needed > 0)
    luaK_reserveregs(fs, needed);  /* registers for extra values */
  else  /* adding 'needed' is actually a subtraction */
    fs->freereg = cast_byte(fs->freereg + needed);  /* remove extra values */
}


#define enterlevel(ls)	luaE_incCstack(ls->L)


#define leavelevel(ls) ((ls)->L->nCcalls--)


/*
** Generates an error that a goto jumps into the scope of some
** variable declaration.
*/
static l_noret jumpscopeerror (LexState *ls, Labeldesc *gt) {
  TString *tsname = getlocalvardesc(ls->fs, gt->nactvar)->vd.name;
  const char *varname = (tsname != NULL) ? getstr(tsname) : "*";
  luaK_semerror(ls,
     "<goto %s> at line %d jumps into the scope of '%s'",
      getstr(gt->name), gt->line, varname);  /* raise the error */
}


/*
** Closes the goto at index 'g' to given 'label' and removes it
** from the list of pending gotos.
** If it jumps into the scope of some variable, raises an error.
** The goto needs a CLOSE if it jumps out of a block with upvalues,
** or out of the scope of some variable and the block has upvalues
** (signaled by parameter 'bup').
*/
static void closegoto (LexState *ls, int g, Labeldesc *label, int bup) {
  int i;
  FuncState *fs = ls->fs;
  Labellist *gl = &ls->dyd->gt;  /* list of gotos */
  Labeldesc *gt = &gl->arr[g];  /* goto to be resolved */
  lua_assert(eqstr(gt->name, label->name));
  if (l_unlikely(gt->nactvar < label->nactvar))  /* enter some scope? */
    jumpscopeerror(ls, gt);
  if (gt->close ||
      (label->nactvar < gt->nactvar && bup)) {  /* needs close? */
    lu_byte stklevel = reglevel(fs, label->nactvar);
    /* move jump to CLOSE position */
    fs->f->code[gt->pc + 1] = fs->f->code[gt->pc];
    /* put CLOSE instruction at original position */
    fs->f->code[gt->pc] = CREATE_ABCk(OP_CLOSE, stklevel, 0, 0, 0);
    gt->pc++;  /* must point to jump instruction */
  }
  luaK_patchlist(ls->fs, gt->pc, label->pc);  /* goto jumps to label */
  for (i = g; i < gl->n - 1; i++)  /* remove goto from pending list */
    gl->arr[i] = gl->arr[i + 1];
  gl->n--;
}


/*
** Search for an active label with the given name, starting at
** index 'ilb' (so that it can search for all labels in current block
** or all labels in current function).
*/
static Labeldesc *findlabel (LexState *ls, TString *name, int ilb) {
  Dyndata *dyd = ls->dyd;
  for (; ilb < dyd->label.n; ilb++) {
    Labeldesc *lb = &dyd->label.arr[ilb];
    if (eqstr(lb->name, name))  /* correct label? */
      return lb;
  }
  return NULL;  /* label not found */
}


/*
** Adds a new label/goto in the corresponding list.
*/
static int newlabelentry (LexState *ls, Labellist *l, TString *name,
                          int line, int pc) {
  int n = l->n;
  luaM_growvector(ls->L, l->arr, n, l->size,
                  Labeldesc, SHRT_MAX, "labels/gotos");
  l->arr[n].name = name;
  l->arr[n].line = line;
  l->arr[n].nactvar = ls->fs->nactvar;
  l->arr[n].close = 0;
  l->arr[n].pc = pc;
  l->n = n + 1;
  return n;
}


/*
** Create an entry for the goto and the code for it. As it is not known
** at this point whether the goto may need a CLOSE, the code has a jump
** followed by an CLOSE. (As the CLOSE comes after the jump, it is a
** dead instruction; it works as a placeholder.) When the goto is closed
** against a label, if it needs a CLOSE, the two instructions swap
** positions, so that the CLOSE comes before the jump.
*/
static int newgotoentry (LexState *ls, TString *name, int line) {
  FuncState *fs = ls->fs;
  int pc = luaK_jump(fs);  /* create jump */
  luaK_codeABC(fs, OP_CLOSE, 0, 1, 0);  /* spaceholder, marked as dead */
  return newlabelentry(ls, &ls->dyd->gt, name, line, pc);
}


/*
** Create a new label with the given 'name' at the given 'line'.
** 'last' tells whether label is the last non-op statement in its
** block. Solves all pending gotos to this new label and adds
** a close instruction if necessary.
** Returns true iff it added a close instruction.
*/
static void createlabel (LexState *ls, TString *name, int line, int last) {
  FuncState *fs = ls->fs;
  Labellist *ll = &ls->dyd->label;
  int l = newlabelentry(ls, ll, name, line, luaK_getlabel(fs));
  if (last) {  /* label is last no-op statement in the block? */
    /* assume that locals are already out of scope */
    ll->arr[l].nactvar = fs->bl->nactvar;
  }
}


/*
** Traverse the pending gotos of the finishing block checking whether
** each match some label of that block. Those that do not match are
** "exported" to the outer block, to be solved there. In particular,
** its 'nactvar' is updated with the level of the inner block,
** as the variables of the inner block are now out of scope.
*/
static void solvegotos (FuncState *fs, BlockCnt *bl) {
  LexState *ls = fs->ls;
  Labellist *gl = &ls->dyd->gt;
  int outlevel = reglevel(fs, bl->nactvar);  /* level outside the block */
  int igt = bl->firstgoto;  /* first goto in the finishing block */
  while (igt < gl->n) {   /* for each pending goto */
    Labeldesc *gt = &gl->arr[igt];
    /* search for a matching label in the current block */
    Labeldesc *lb = findlabel(ls, gt->name, bl->firstlabel);
    if (lb != NULL)  /* found a match? */
      closegoto(ls, igt, lb, bl->upval);  /* close and remove goto */
    else {  /* adjust 'goto' for outer block */
      /* block has variables to be closed and goto escapes the scope of
         some variable? */
      if (bl->upval && reglevel(fs, gt->nactvar) > outlevel)
        gt->close = 1;  /* jump may need a close */
      gt->nactvar = bl->nactvar;  /* correct level for outer block */
      igt++;  /* go to next goto */
    }
  }
  ls->dyd->label.n = bl->firstlabel;  /* remove local labels */
}


static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isloop) {
  bl->isloop = isloop;
  bl->nactvar = fs->nactvar;
  bl->firstlabel = fs->ls->dyd->label.n;
  bl->firstgoto = fs->ls->dyd->gt.n;
  bl->upval = 0;
  /* inherit 'insidetbc' from enclosing block */
  bl->insidetbc = (fs->bl != NULL && fs->bl->insidetbc);
  bl->previous = fs->bl;  /* link block in function's block list */
  fs->bl = bl;
  lua_assert(fs->freereg == luaY_nvarstack(fs));
}


/*
** generates an error for an undefined 'goto'.
*/
static l_noret undefgoto (LexState *ls, Labeldesc *gt) {
  /* breaks are checked when created, cannot be undefined */
  lua_assert(!eqstr(gt->name, ls->brkn));
  luaK_semerror(ls, "no visible label '%s' for <goto> at line %d",
                    getstr(gt->name), gt->line);
}


static void leaveblock (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  LexState *ls = fs->ls;
  lu_byte stklevel = reglevel(fs, bl->nactvar);  /* level outside block */
  if (bl->previous && bl->upval)  /* need a 'close'? */
    luaK_codeABC(fs, OP_CLOSE, stklevel, 0, 0);
  fs->freereg = stklevel;  /* free registers */
  removevars(fs, bl->nactvar);  /* remove block locals */
  lua_assert(bl->nactvar == fs->nactvar);  /* back to level on entry */
  if (bl->isloop == 2)  /* has to fix pending breaks? */
    createlabel(ls, ls->brkn, 0, 0);
  solvegotos(fs, bl);
  if (bl->previous == NULL) {  /* was it the last block? */
    if (bl->firstgoto < ls->dyd->gt.n)  /* still pending gotos? */
      undefgoto(ls, &ls->dyd->gt.arr[bl->firstgoto]);  /* error */
  }
  fs->bl = bl->previous;  /* current block now is previous one */
}


/*
** adds a new prototype into list of prototypes
*/
static Proto *addprototype (LexState *ls) {
  Proto *clp;
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;  /* prototype of current function */
  if (fs->np >= f->sizep) {
    int oldsize = f->sizep;
    luaM_growvector(L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
    while (oldsize < f->sizep)
      f->p[oldsize++] = NULL;
  }
  f->p[fs->np++] = clp = luaF_newproto(L);
  luaC_objbarrier(L, f, clp);
  return clp;
}


/*
** codes instruction to create new closure in parent function.
** The OP_CLOSURE instruction uses the last available register,
** so that, if it invokes the GC, the GC knows which registers
** are in use at that time.

*/
static void codeclosure (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs->prev;
  init_exp(v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
  luaK_exp2nextreg(fs, v);  /* fix it at the last register */
}


static void open_func (LexState *ls, FuncState *fs, BlockCnt *bl) {
  lua_State *L = ls->L;
  Proto *f = fs->f;
  fs->prev = ls->fs;  /* linked list of funcstates */
  fs->ls = ls;
  ls->fs = fs;
  fs->pc = 0;
  fs->previousline = f->linedefined;
  fs->iwthabs = 0;
  fs->lasttarget = 0;
  fs->freereg = 0;
  fs->nk = 0;
  fs->nabslineinfo = 0;
  fs->np = 0;
  fs->nups = 0;
  fs->ndebugvars = 0;
  fs->nactvar = 0;
  fs->needclose = 0;
  fs->firstlocal = ls->dyd->actvar.n;
  fs->firstlabel = ls->dyd->label.n;
  fs->bl = NULL;
  f->source = ls->source;
  luaC_objbarrier(L, f, f->source);
  f->maxstacksize = 2;  /* registers 0/1 are always valid */
  fs->kcache = luaH_new(L);  /* create table for function */
  sethvalue2s(L, L->top.p, fs->kcache);  /* anchor it */
  luaD_inctop(L);
  enterblock(fs, bl, 0);
}


static void close_func (LexState *ls) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  luaK_ret(fs, luaY_nvarstack(fs), 0);  /* final return */
  leaveblock(fs);
  lua_assert(fs->bl == NULL);
  luaK_finish(fs);
  luaM_shrinkvector(L, f->code, f->sizecode, fs->pc, Instruction);
  luaM_shrinkvector(L, f->lineinfo, f->sizelineinfo, fs->pc, ls_byte);
  luaM_shrinkvector(L, f->abslineinfo, f->sizeabslineinfo,
                       fs->nabslineinfo, AbsLineInfo);
  luaM_shrinkvector(L, f->k, f->sizek, fs->nk, TValue);
  luaM_shrinkvector(L, f->p, f->sizep, fs->np, Proto *);
  luaM_shrinkvector(L, f->locvars, f->sizelocvars, fs->ndebugvars, LocVar);
  luaM_shrinkvector(L, f->upvalues, f->sizeupvalues, fs->nups, Upvaldesc);
  ls->fs = fs->prev;
  L->top.p--;  /* pop kcache table */
  luaC_checkGC(L);
}


/*
** {======================================================================
** GRAMMAR RULES
** =======================================================================
*/


/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it is handled in separate.
*/
static int block_follow (LexState *ls, int withuntil) {
  switch (ls->t.token) {
    case TK_ELSE: case TK_ELSEIF:
    case TK_END:  /* anonymous function ending */
    case TK_EOS:
    case '}':  /* Mos: C-style block ending */
      return 1;
    case TK_UNTIL: return withuntil;
    default: return 0;
  }
}


/*
** Parse statements within a switch case body
** Stops at case, default, or }
*/
static void casestatlist (LexState *ls) {
  /* statlist -> { stat [';'] } */
  while (ls->t.token != TK_CASE && ls->t.token != TK_DEFAULT && 
         ls->t.token != '}' && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_RETURN) {
      statement(ls);
      return;  /* 'return' must be last statement */
    }
    statement(ls);
  }
}

static void statlist (LexState *ls) {
  /* statlist -> { stat [';'] } */
  while (!block_follow(ls, 1)) {
    if (ls->t.token == TK_RETURN) {
      statement(ls);
      return;  /* 'return' must be last statement */
    }
    statement(ls);
  }
}


static void fieldsel (LexState *ls, expdesc *v) {
  /* fieldsel -> ['.' | ':'] NAME */
  FuncState *fs = ls->fs;
  expdesc key;
  luaK_exp2anyregup(fs, v);
  luaX_next(ls);  /* skip the dot or colon */
  codename(ls, &key);
  luaK_indexed(fs, v, &key);
}


static void yindex (LexState *ls, expdesc *v) {
  /* index -> '[' expr ']' */
  luaX_next(ls);  /* skip the '[' */
  expr(ls, v);
  luaK_exp2val(ls->fs, v);
  checknext(ls, ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/

typedef struct ConsControl {
  expdesc v;  /* last list item read */
  expdesc *t;  /* table descriptor */
  int nh;  /* total number of 'record' elements */
  int na;  /* number of array elements already stored */
  int tostore;  /* number of array elements pending to be stored */
  int maxtostore;  /* maximum number of pending elements */
} ConsControl;


/*
** Maximum number of elements in a constructor, to control the following:
** * counter overflows;
** * overflows in 'extra' for OP_NEWTABLE and OP_SETLIST;
** * overflows when adding multiple returns in OP_SETLIST.
*/
#define MAX_CNST	(INT_MAX/2)
#if MAX_CNST/(MAXARG_vC + 1) > MAXARG_Ax
#undef MAX_CNST
#define MAX_CNST	(MAXARG_Ax * (MAXARG_vC + 1))
#endif


static void recfield (LexState *ls, ConsControl *cc) {
  /* recfield -> (NAME | '['exp']') = exp */
  FuncState *fs = ls->fs;
  lu_byte reg = ls->fs->freereg;
  expdesc tab, key, val;
  if (ls->t.token == TK_NAME)
    codename(ls, &key);
  else  /* ls->t.token == '[' */
    yindex(ls, &key);
  cc->nh++;
  checknext(ls, '=');
  tab = *cc->t;
  luaK_indexed(fs, &tab, &key);
  expr(ls, &val);
  luaK_storevar(fs, &tab, &val);
  fs->freereg = reg;  /* free registers */
}


static void closelistfield (FuncState *fs, ConsControl *cc) {
  lua_assert(cc->tostore > 0);
  luaK_exp2nextreg(fs, &cc->v);
  cc->v.k = VVOID;
  if (cc->tostore >= cc->maxtostore) {
    luaK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);  /* flush */
    cc->na += cc->tostore;
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *fs, ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.k)) {
    luaK_setmultret(fs, &cc->v);
    luaK_setlist(fs, cc->t->u.info, cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.k != VVOID)
      luaK_exp2nextreg(fs, &cc->v);
    luaK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);
  }
  cc->na += cc->tostore;
}


static void listfield (LexState *ls, ConsControl *cc) {
  /* listfield -> exp */
  expr(ls, &cc->v);
  cc->tostore++;
}


static void field (LexState *ls, ConsControl *cc) {
  /* field -> listfield | recfield */
  switch(ls->t.token) {
    case TK_NAME: {  /* may be 'listfield' or 'recfield' */
      if (luaX_lookahead(ls) != '=')  /* expression? */
        listfield(ls, cc);
      else
        recfield(ls, cc);
      break;
    }
    case '[': {
      recfield(ls, cc);
      break;
    }
    default: {
      listfield(ls, cc);
      break;
    }
  }
}


/*
** Compute a limit for how many registers a constructor can use before
** emitting a 'SETLIST' instruction, based on how many registers are
** available.
*/
static int maxtostore (FuncState *fs) {
  int numfreeregs = MAX_FSTACK - fs->freereg;
  if (numfreeregs >= 160)  /* "lots" of registers? */
    return numfreeregs / 5;  /* use up to 1/5 of them */
  else if (numfreeregs >= 80)  /* still "enough" registers? */
    return 10;  /* one 'SETLIST' instruction for each 10 values */
  else  /* save registers for potential more nesting */
    return 1;
}


static void constructor (LexState *ls, expdesc *t) {
  /* constructor -> '{' [ field { sep field } [sep] ] '}'
     sep -> ',' | ';' */
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  int pc = luaK_codevABCk(fs, OP_NEWTABLE, 0, 0, 0, 0);
  ConsControl cc;
  luaK_code(fs, 0);  /* space for extra arg. */
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  init_exp(t, VNONRELOC, fs->freereg);  /* table will be at stack top */
  luaK_reserveregs(fs, 1);
  init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
  checknext(ls, '{' /*}*/);
  cc.maxtostore = maxtostore(fs);
  do {
    if (ls->t.token == /*{*/ '}') break;
    if (cc.v.k != VVOID)  /* is there a previous list item? */
      closelistfield(fs, &cc);  /* close it */
    field(ls, &cc);
    luaY_checklimit(fs, cc.tostore + cc.na + cc.nh, MAX_CNST,
                    "items in a constructor");
  } while (testnext(ls, ',') || testnext(ls, ';'));
  check_match(ls, /*{*/ '}', '{' /*}*/, line);
  lastlistfield(fs, &cc);
  luaK_settablesize(fs, pc, t->u.info, cc.na, cc.nh);
}

/* }====================================================================== */


static void setvararg (FuncState *fs) {
  fs->f->flag |= PF_VAHID;  /* by default, use hidden vararg arguments */
  luaK_codeABC(fs, OP_VARARGPREP, 0, 0, 0);
}


static void parlist (LexState *ls) {
  /* parlist -> [ {NAME ','} (NAME | '...') ] */
  /* Mos: parlist -> [ {type NAME ','} (type NAME | '...') ] */
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  int nparams = 0;
  int varargk = 0;
  if (ls->t.token != ')') {  /* is 'parlist' not empty? */
    do {
      switch (ls->t.token) {
        case TK_TYPE_INT:
        case TK_TYPE_FLOAT:
        case TK_TYPE_BOOL:
        case TK_TYPE_STRING:
        case TK_TYPE_VOID: {
          /* Mos: Get type keyword, then get parameter name */
          int type = token_to_type(ls->t.token);
          luaX_next(ls);  /* skip type */
          if (ls->t.token == TK_NAME) {
            new_localvar_typed(ls, str_checkname(ls), type);
            nparams++;
          } else {
            luaX_syntaxerror(ls, "<name> expected after type");
          }
          break;
        }
        case TK_NAME: {
          /* Mos: Parameter must have type declaration */
          luaX_syntaxerror(ls, "parameter must have type declaration (e.g., 'int name')");
          break;
        }
        case TK_DOTS: {
          varargk = 1;
          luaX_next(ls);  /* skip '...' */
          if (ls->t.token == TK_NAME)
            new_varkind(ls, str_checkname(ls), RDKVAVAR);
          else
            new_localvarliteral(ls, "(vararg table)");
          break;
        }
        default: luaX_syntaxerror(ls, "<name> or '...' expected");
      }
    } while (!varargk && testnext(ls, ','));
  }
  adjustlocalvars(ls, nparams);
  f->numparams = cast_byte(fs->nactvar);
  if (varargk) {
    setvararg(fs);  /* declared vararg */
    adjustlocalvars(ls, 1);  /* vararg parameter */
  }
  /* reserve registers for parameters (plus vararg parameter, if present) */
  luaK_reserveregs(fs, fs->nactvar);
}


static void body (LexState *ls, expdesc *e, int ismethod, int line, int rettype) {
  /* body ->  '(' parlist ')' block '}' or '(' parlist ')' block 'end' */
  FuncState new_fs;
  BlockCnt bl;
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);
  new_fs.rettype = cast_byte(rettype);  /* Mos: Store return type */
  checknext(ls, '(');
  if (ismethod) {
    new_localvarliteral(ls, "self");  /* create 'self' parameter */
    adjustlocalvars(ls, 1);
  }
  parlist(ls);
  checknext(ls, ')');
  /* Mos: Support both C-style '{' and Lua-style for anonymous functions */
  if (ls->t.token == '{') {
    luaX_next(ls);  /* skip '{' */
    statlist(ls);
    new_fs.f->lastlinedefined = ls->linenumber;
    checknext(ls, '}');
  } else {
    /* Anonymous function: function(params) ... end */
    statlist(ls);
    new_fs.f->lastlinedefined = ls->linenumber;
    checknext(ls, TK_END);
  }
  codeclosure(ls, e);
  close_func(ls);
}


static int explist (LexState *ls, expdesc *v) {
  /* explist -> expr { ',' expr } */
  int n = 1;  /* at least one expression */
  expr(ls, v);
  while (testnext(ls, ',')) {
    luaK_exp2nextreg(ls->fs, v);
    expr(ls, v);
    n++;
  }
  return n;
}


static void funcargs (LexState *ls, expdesc *f) {
  FuncState *fs = ls->fs;
  expdesc args;
  int base, nparams;
  int line = ls->linenumber;
  switch (ls->t.token) {
    case '(': {  /* funcargs -> '(' [ explist ] ')' */
      luaX_next(ls);
      if (ls->t.token == ')')  /* arg list is empty? */
        args.k = VVOID;
      else {
        explist(ls, &args);
        if (hasmultret(args.k))
          luaK_setmultret(fs, &args);
      }
      check_match(ls, ')', '(', line);
      break;
    }
    case '{' /*}*/: {  /* funcargs -> constructor */
      constructor(ls, &args);
      break;
    }
    case TK_STRING: {  /* funcargs -> STRING */
      codestring(&args, ls->t.seminfo.ts);
      luaX_next(ls);  /* must use 'seminfo' before 'next' */
      break;
    }
    default: {
      luaX_syntaxerror(ls, "function arguments expected");
    }
  }
  lua_assert(f->k == VNONRELOC);
  base = f->u.info;  /* base register for call */
  if (hasmultret(args.k))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.k != VVOID)
      luaK_exp2nextreg(fs, &args);  /* close last argument */
    nparams = fs->freereg - (base+1);
  }
  init_exp(f, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
  luaK_fixline(fs, line);
  /* call removes function and arguments and leaves one result (unless
     changed later) */
  fs->freereg = cast_byte(base + 1);
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


static void primaryexp (LexState *ls, expdesc *v) {
  /* primaryexp -> NAME | '(' expr ')' | NEW classname funcargs */
  switch (ls->t.token) {
    case '(': {
      int line = ls->linenumber;
      luaX_next(ls);
      expr(ls, v);
      check_match(ls, ')', '(', line);
      luaK_dischargevars(ls->fs, v);
      return;
    }
    case TK_NAME: {
      singlevar(ls, v);
      return;
    }
    case TK_NEW: {
      /* new ClassName(args) - create new object */
      FuncState *fs = ls->fs;
      expdesc classexp, classkey, initkey;
      int line = ls->linenumber;
      luaX_next(ls);  /* skip 'new' */
      
      /* Get class name */
      TString *classname = str_checkname(ls);
      
      /* Look up class in globals */
      singlevaraux(fs, ls->envn, &classexp, 1);  /* get _ENV */
      codestring(&classkey, classname);
      luaK_indexed(fs, &classexp, &classkey);  /* classexp = _ENV[classname] */
      
      /* Move class to a register and keep it there */
      int classreg = luaK_exp2anyreg(fs, &classexp);
      
      /* Create new table for object */
      int pc = luaK_codevABCk(fs, OP_NEWTABLE, 0, 0, 0, 0);
      luaK_code(fs, 0);  /* space for extra arg */
      init_exp(v, VNONRELOC, fs->freereg);  /* table will be at stack top */
      luaK_reserveregs(fs, 1);
      int objreg = v->u.info;
      luaK_settablesize(fs, pc, objreg, 0, 0);
      
      /* Store class reference in obj.__class */
      {
        expdesc classfield, classref;
        codestring(&classfield, luaS_newliteral(ls->L, "__class"));
        classref = *v;  /* obj */
        luaK_indexed(fs, &classref, &classfield);
        luaK_storevar(fs, &classref, &classexp);
        init_exp(v, VNONRELOC, objreg);
        fs->freereg = objreg + 1;  /* ensure freereg is correct */
      }
      
      /* Set metatable so object can access class methods */
      {
        expdesc setmt, mtname, mttable, mtindex, classref;
        
        /* First, get setmetatable function and put it in a register */
        singlevaraux(fs, ls->envn, &setmt, 1);
        codestring(&mtname, luaS_newliteral(ls->L, "setmetatable"));
        luaK_indexed(fs, &setmt, &mtname);
        luaK_exp2nextreg(fs, &setmt);
        int setmtreg = setmt.u.info;
        
        /* Create metatable: {__index = class} */
        int mtpc = luaK_codevABCk(fs, OP_NEWTABLE, 0, 0, 0, 0);
        luaK_code(fs, 0);
        init_exp(&mttable, VNONRELOC, fs->freereg);
        luaK_reserveregs(fs, 1);
        int mtreg = mttable.u.info;
        luaK_settablesize(fs, mtpc, mtreg, 0, 0);
        
        /* Re-fetch class to a register to ensure it's valid */
        singlevaraux(fs, ls->envn, &classref, 1);
        codestring(&mtname, classname);
        luaK_indexed(fs, &classref, &mtname);
        int newclassreg = luaK_exp2anyreg(fs, &classref);
        
        /* Set metatable.__index = class */
        codestring(&mtname, luaS_newliteral(ls->L, "__index"));
        mtindex = mttable;
        luaK_indexed(fs, &mtindex, &mtname);
        init_exp(&setmt, VNONRELOC, newclassreg);
        luaK_storevar(fs, &mtindex, &setmt);
        
        /* After SETFIELD, ensure freereg is after the highest used register */
        /* newclassreg might be higher than mtreg, so use max */
        int maxreg = (newclassreg > mtreg) ? newclassreg : mtreg;
        fs->freereg = maxreg + 1;
        
        /* Prepare arguments for setmetatable call */
        /* Arguments must be in registers right after the function */
        /* setmetatable is in setmtreg, so args should be in setmtreg+1 and setmtreg+2 */
        /* But we need to be careful not to overwrite existing registers */
        /* First, move object and metatable to safe registers */
        int arg1reg = fs->freereg;
        luaK_codeABC(fs, OP_MOVE, arg1reg, objreg, 0);
        luaK_reserveregs(fs, 1);
        
        int arg2reg = fs->freereg;
        luaK_codeABC(fs, OP_MOVE, arg2reg, mtreg, 0);
        luaK_reserveregs(fs, 1);
        
        /* Now move them to the correct positions for the call */
        /* We need to move them in the right order to avoid overwriting */
        /* If setmtreg+1 or setmtreg+2 overlaps with arg1reg or arg2reg, be careful */
        if (setmtreg + 1 == arg2reg) {
          /* Moving arg2 would overwrite arg1's target position */
          /* Move arg1 first */
          luaK_codeABC(fs, OP_MOVE, setmtreg + 1, arg1reg, 0);
          luaK_codeABC(fs, OP_MOVE, setmtreg + 2, arg2reg, 0);
        } else {
          /* Safe to move in any order */
          luaK_codeABC(fs, OP_MOVE, setmtreg + 2, arg2reg, 0);
          luaK_codeABC(fs, OP_MOVE, setmtreg + 1, arg1reg, 0);
        }
        
        /* Call setmetatable(obj, metatable) */
        /* Function is at setmtreg, args at setmtreg+1 and setmtreg+2 */
        luaK_codeABC(fs, OP_CALL, setmtreg, 3, 2);  /* 3 regs total (func+2 args), 1 result */
        
        /* setmetatable returns the object in setmtreg */
        /* Save it to a new register immediately to protect it */
        int finalobjreg = fs->freereg;
        luaK_codeABC(fs, OP_MOVE, finalobjreg, setmtreg, 0);
        luaK_reserveregs(fs, 1);
        
        /* Update objreg to point to the final object */
        objreg = finalobjreg;
        
        /* Restore object reference */
        init_exp(v, VNONRELOC, objreg);
        /* Don't reset freereg here - keep the register allocated */
      }
      
      /* Check for constructor arguments and call init if present */
      if (ls->t.token == '(') {
        /* Call init method: class.init(self, args...) */
        expdesc initmethod, args;
        int base, nparams;
        int line = ls->linenumber;
        
        /* First, save object to a safe register before any other operations */
        /* This must be done before getting init method to prevent overwriting */
        int objsavereg = fs->freereg;
        luaK_codeABC(fs, OP_MOVE, objsavereg, objreg, 0);
        luaK_reserveregs(fs, 1);
        
        /* Reserve an extra register to prevent GETFIELD from using objsavereg */
        /* GETFIELD uses freereg-1, so we need freereg > objsavereg + 1 */
        luaK_reserveregs(fs, 1);
        
        /* Get init method from class register */
        codestring(&initkey, luaS_newliteral(ls->L, "init"));
        init_exp(&initmethod, VNONRELOC, classreg);
        luaK_indexed(fs, &initmethod, &initkey);
        
        /* Move init method to a register */
        luaK_exp2nextreg(fs, &initmethod);
        base = initmethod.u.info;
        
        /* Move self to next register (first argument) - use saved object */
        luaK_codeABC(fs, OP_MOVE, fs->freereg, objsavereg, 0);
        luaK_reserveregs(fs, 1);
        
        /* Parse arguments */
        luaX_next(ls);  /* skip '(' */
        if (ls->t.token == ')')  /* arg list is empty? */
          args.k = VVOID;
        else {
          explist(ls, &args);
          if (hasmultret(args.k))
            luaK_setmultret(fs, &args);
        }
        check_match(ls, ')', '(', line);
        
        /* Close last argument */
        if (args.k != VVOID)
          luaK_exp2nextreg(fs, &args);
        
        /* Calculate number of parameters (including self) */
        nparams = fs->freereg - (base + 1);
        
        /* Call init(self, args...) - no return values expected */
        luaK_codeABC(fs, OP_CALL, base, nparams + 1, 1);
        
        /* Restore object reference */
        init_exp(v, VNONRELOC, objsavereg);
        fs->freereg = objsavereg + 1;  /* reset freereg to just after object */
      }
      
      return;
    }
    default: {
      luaX_syntaxerror(ls, "unexpected symbol");
    }
  }
}


static void suffixedexp (LexState *ls, expdesc *v) {
  /* suffixedexp ->
       primaryexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs } */
  FuncState *fs = ls->fs;
  primaryexp(ls, v);
  for (;;) {
    switch (ls->t.token) {
      case '.': {  /* fieldsel */
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' */
        expdesc key;
        luaK_exp2anyregup(fs, v);
        yindex(ls, &key);
        luaK_indexed(fs, v, &key);
        break;
      }
      case ':': {  /* ':' NAME funcargs */
        expdesc key;
        luaX_next(ls);
        codename(ls, &key);
        luaK_self(fs, v, &key);
        funcargs(ls, v);
        break;
      }
      case '(': case TK_STRING: case '{' /*}*/: {  /* funcargs */
        /* Regular function call - don't auto-convert to self semantics */
        luaK_exp2nextreg(fs, v);
        funcargs(ls, v);
        break;
      }
      default: return;
    }
  }
}


static void simpleexp (LexState *ls, expdesc *v) {
  /* simpleexp -> FLT | INT | STRING | NIL | TRUE | FALSE | ... |
                  constructor | FUNCTION body | suffixedexp */
  switch (ls->t.token) {
    case TK_FLT: {
      init_exp(v, VKFLT, 0);
      v->u.nval = ls->t.seminfo.r;
      v->etype = MOS_TYPE_FLOAT;  /* Mos: Set expression type */
      break;
    }
    case TK_INT: {
      init_exp(v, VKINT, 0);
      v->u.ival = ls->t.seminfo.i;
      v->etype = MOS_TYPE_INT;  /* Mos: Set expression type */
      break;
    }
    case TK_STRING: {
      codestring(v, ls->t.seminfo.ts);
      v->etype = MOS_TYPE_STRING;  /* Mos: Set expression type */
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      v->etype = MOS_TYPE_VOID;  /* Mos: Set expression type */
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      v->etype = MOS_TYPE_BOOL;  /* Mos: Set expression type */
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      v->etype = MOS_TYPE_BOOL;  /* Mos: Set expression type */
      break;
    }
    case TK_DOTS: {  /* vararg */
      FuncState *fs = ls->fs;
      check_condition(ls, isvararg(fs->f),
                      "cannot use '...' outside a vararg function");
      init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, fs->f->numparams, 1));
      break;
    }
    case '{' /*}*/: {  /* constructor */
      constructor(ls, v);
      return;
    }
    case TK_FUNCTION: {  /* anonymous function: function(params) body end */
      luaX_next(ls);  /* skip 'function' keyword */
      body(ls, v, 0, ls->linenumber, MOS_TYPE_UNKNOWN);
      return;
    }
    default: {
      suffixedexp(ls, v);
      return;
    }
  }
  luaX_next(ls);
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '~': return OPR_BNOT;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case '/': return OPR_DIV;
    case '&': return OPR_BAND;
    case '|': return OPR_BOR;
    case '~': return OPR_BXOR;
    case TK_SHL: return OPR_SHL;
    case TK_SHR: return OPR_SHR;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    default: return OPR_NOBINOPR;
  }
}


/*
** Priority table for binary operators.
*/
static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {10, 10}, {10, 10},           /* '+' '-' */
   {11, 11}, {11, 11},           /* '*' '%' */
   {14, 13},                  /* '^' (right associative) */
   {11, 11}, {11, 11},           /* '/' '//' */
   {6, 6}, {4, 4}, {5, 5},   /* '&' '|' '~' */
   {7, 7}, {7, 7},           /* '<<' '>>' */
   {9, 8},                   /* '..' (right associative) */
   {3, 3}, {3, 3}, {3, 3},   /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},   /* ~=, >, >= */
   {2, 2}, {1, 1}            /* and, or */
};

#define UNARY_PRIORITY	12  /* priority for unary operators */


/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where 'binop' is any binary operator with a priority higher than 'limit'
*/
static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  uop = getunopr(ls->t.token);
  if (uop != OPR_NOUNOPR) {  /* prefix (unary) operator? */
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    subexpr(ls, v, UNARY_PRIORITY);
    luaK_prefix(ls->fs, uop, v, line);
  }
  else simpleexp(ls, v);
  /* expand while operators have priorities higher than 'limit' */
  op = getbinopr(ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    luaK_infix(ls->fs, op, v);
    /* read sub-expression with higher priority */
    nextop = subexpr(ls, &v2, priority[op].right);
    /* Mos: Check binary operation type compatibility */
    check_binop_type(ls, op, v, &v2, line);
    luaK_posfix(ls->fs, op, v, &v2, line);
    op = nextop;
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


static void expr (LexState *ls, expdesc *v) {
  subexpr(ls, v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static void block (LexState *ls) {
  /* block -> statlist */
  FuncState *fs = ls->fs;
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  statlist(ls);
  leaveblock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to an upvalue/local variable, the
** upvalue/local variable is begin used in a previous assignment to a
** table. If so, save original upvalue/local value in a safe place and
** use this safe copy in the previous assignment.
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
  FuncState *fs = ls->fs;
  lu_byte extra = fs->freereg;  /* eventual position to save local variable */
  int conflict = 0;
  for (; lh; lh = lh->prev) {  /* check all previous assignments */
    if (vkisindexed(lh->v.k)) {  /* assignment to table field? */
      if (lh->v.k == VINDEXUP) {  /* is table an upvalue? */
        if (v->k == VUPVAL && lh->v.u.ind.t == v->u.info) {
          conflict = 1;  /* table is the upvalue being assigned now */
          lh->v.k = VINDEXSTR;
          lh->v.u.ind.t = extra;  /* assignment will use safe copy */
        }
      }
      else {  /* table is a register */
        if (v->k == VLOCAL && lh->v.u.ind.t == v->u.var.ridx) {
          conflict = 1;  /* table is the local being assigned now */
          lh->v.u.ind.t = extra;  /* assignment will use safe copy */
        }
        /* is index the local being assigned? */
        if (lh->v.k == VINDEXED && v->k == VLOCAL &&
            lh->v.u.ind.idx == v->u.var.ridx) {
          conflict = 1;
          lh->v.u.ind.idx = extra;  /* previous assignment will use safe copy */
        }
      }
    }
  }
  if (conflict) {
    /* copy upvalue/local value to a temporary (in position 'extra') */
    if (v->k == VLOCAL)
      luaK_codeABC(fs, OP_MOVE, extra, v->u.var.ridx, 0);
    else
      luaK_codeABC(fs, OP_GETUPVAL, extra, v->u.info, 0);
    luaK_reserveregs(fs, 1);
  }
}


/* Create code to store the "top" register in 'var' */
static void storevartop (FuncState *fs, expdesc *var) {
  expdesc e;
  init_exp(&e, VNONRELOC, fs->freereg - 1);
  luaK_storevar(fs, var, &e);  /* will also free the top register */
}


/*
** Parse and compile a multiple assignment. The first "variable"
** (a 'suffixedexp') was already read by the caller.
**
** assignment -> suffixedexp restassign
** restassign -> ',' suffixedexp restassign | '=' explist
*/
static void restassign (LexState *ls, struct LHS_assign *lh, int nvars) {
  expdesc e;
  check_condition(ls, vkisvar(lh->v.k), "syntax error");
  check_readonly(ls, &lh->v);
  if (testnext(ls, ',')) {  /* restassign -> ',' suffixedexp restassign */
    struct LHS_assign nv;
    nv.prev = lh;
    suffixedexp(ls, &nv.v);
    if (!vkisindexed(nv.v.k))
      check_conflict(ls, lh, &nv.v);
    enterlevel(ls);  /* control recursion depth */
    restassign(ls, &nv, nvars+1);
    leavelevel(ls);
  }
  else {  /* restassign -> '=' explist */
    int nexps;
    checknext(ls, '=');
    nexps = explist(ls, &e);
    if (nexps != nvars)
      adjust_assign(ls, nvars, nexps, &e);
    else {
      luaK_setoneret(ls->fs, &e);  /* close last expression */
      /* Mos: Type checking for assignment */
      if (lh->v.k == VLOCAL || lh->v.k == VVARGVAR) {
        Vardesc *var = getlocalvardesc(ls->fs, lh->v.u.var.vidx);
        check_assignment_type(ls, var, &e);
      }
      luaK_storevar(ls->fs, &lh->v, &e);
      return;  /* avoid default */
    }
  }
  /* Mos: Type checking for default assignment */
  if (lh->v.k == VLOCAL || lh->v.k == VVARGVAR) {
    Vardesc *var = getlocalvardesc(ls->fs, lh->v.u.var.vidx);
    check_assignment_type(ls, var, &e);
  }
  storevartop(ls->fs, &lh->v);  /* default assignment */
}


static int cond (LexState *ls) {
  /* cond -> exp */
  expdesc v;
  expr(ls, &v);  /* read condition */
  if (v.k == VNIL) v.k = VFALSE;  /* 'falses' are all equal here */
  luaK_goiftrue(ls->fs, &v);
  return v.f;
}


static void gotostat (LexState *ls, int line) {
  TString *name = str_checkname(ls);  /* label's name */
  newgotoentry(ls, name, line);
}


/*
** Break statement. Semantically equivalent to "goto break".
*/
static void breakstat (LexState *ls, int line) {
  BlockCnt *bl;  /* to look for an enclosing loop */
  for (bl = ls->fs->bl; bl != NULL; bl = bl->previous) {
    if (bl->isloop)  /* found one? */
      goto ok;
  }
  luaX_syntaxerror(ls, "break outside loop");
 ok:
  bl->isloop = 2;  /* signal that block has pending breaks */
  luaX_next(ls);  /* skip break */
  newgotoentry(ls, ls->brkn, line);
}


/*
** Switch statement parsing
** switch (expr) { case expr: block ... default: block }
** Uses if-else chain approach: each case becomes an if check
*/
static void switchstat (LexState *ls, int line) {
  expdesc e, v;
  FuncState *fs = ls->fs;
  BlockCnt bl;
  int endswitch = -1;
  int prevjmp = -1;
  
  luaX_next(ls);  /* skip 'switch' */
  checknext(ls, '(');
  expr(ls, &e);  /* parse switch expression */
  checknext(ls, ')');
  checknext(ls, '{');
  
  enterblock(fs, &bl, 1);  /* create block for switch scope (isloop=1 for break) */
  
  luaK_exp2nextreg(fs, &e);
  int switchreg = e.u.info;
  
  while (ls->t.token != '}' && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_CASE) {
      luaX_next(ls);  /* skip 'case' */
      expr(ls, &v);  /* parse case value */
      checknext(ls, ':');
      
      luaK_exp2nextreg(fs, &v);
      int casereg = v.u.info;
      
      /* If there was a previous case, patch its jump to here */
      if (prevjmp != -1) {
        luaK_patchtohere(fs, prevjmp);
      }
      
      /* Generate comparison: if (switchval != caseval) jump to next */
      luaK_codeABC(fs, OP_EQ, 1, switchreg, casereg);
      prevjmp = luaK_jump(fs);
      
      casestatlist(ls);  /* parse case body */
    }
    else if (ls->t.token == TK_DEFAULT) {
      luaX_next(ls);  /* skip 'default' */
      checknext(ls, ':');
      
      /* If there was a previous case, patch its jump to here */
      if (prevjmp != -1) {
        luaK_patchtohere(fs, prevjmp);
        prevjmp = -1;
      }
      
      casestatlist(ls);  /* parse default body */
    }
    else {
      luaX_syntaxerror(ls, "expected 'case' or 'default'");
    }
  }
  
  /* Patch the last case jump to end */
  if (prevjmp != -1) {
    luaK_patchtohere(fs, prevjmp);
  }
  
  leaveblock(fs);  /* leave switch scope block */
  
  checknext(ls, '}');
}


/*
** Check whether there is already a label with the given 'name' at
** current function.
*/
static void checkrepeated (LexState *ls, TString *name) {
  Labeldesc *lb = findlabel(ls, name, ls->fs->firstlabel);
  if (l_unlikely(lb != NULL))  /* already defined? */
    luaK_semerror(ls, "label '%s' already defined on line %d",
                      getstr(name), lb->line);  /* error */
}


static void labelstat (LexState *ls, TString *name, int line) {
  /* label -> '::' NAME '::' */
  checknext(ls, TK_DBCOLON);  /* skip double colon */
  while (ls->t.token == ';' || ls->t.token == TK_DBCOLON)
    statement(ls);  /* skip other no-op statements */
  checkrepeated(ls, name);  /* check for repeated labels */
  createlabel(ls, name, line, block_follow(ls, 0));
}


static void whilestat (LexState *ls, int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *fs = ls->fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  luaX_next(ls);  /* skip WHILE */
  /* Mos: Support C-style while (cond) { ... } */
  if (ls->t.token == '(') {
    luaX_next(ls);  /* skip '(' */
  }
  whileinit = luaK_getlabel(fs);
  condexit = cond(ls);
  if (ls->t.token == ')') {
    luaX_next(ls);  /* skip ')' */
  }
  enterblock(fs, &bl, 1);
  checknext(ls, '{');
  block(ls);
  luaK_jumpto(fs, whileinit);
  checknext(ls, '}');
  leaveblock(fs);
  luaK_patchtohere(fs, condexit);  /* false conditions finish the loop */
}


static void repeatstat (LexState *ls, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->fs;
  int repeat_init = luaK_getlabel(fs);
  BlockCnt bl1, bl2;
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  luaX_next(ls);  /* skip REPEAT */
  statlist(ls);
  check_match(ls, TK_UNTIL, TK_REPEAT, line);
  condexit = cond(ls);  /* read condition (inside scope block) */
  leaveblock(fs);  /* finish scope */
  if (bl2.upval) {  /* upvalues? */
    int exit = luaK_jump(fs);  /* normal exit must jump over fix */
    luaK_patchtohere(fs, condexit);  /* repetition must close upvalues */
    luaK_codeABC(fs, OP_CLOSE, reglevel(fs, bl2.nactvar), 0, 0);
    condexit = luaK_jump(fs);  /* repeat after closing upvalues */
    luaK_patchtohere(fs, exit);  /* normal exit comes to here */
  }
  luaK_patchlist(fs, condexit, repeat_init);  /* close the loop */
  leaveblock(fs);  /* finish loop */
}


/*
** Read an expression and generate code to put its results in next
** stack slot.
**
*/
static void exp1 (LexState *ls) {
  expdesc e;
  expr(ls, &e);
  luaK_exp2nextreg(ls->fs, &e);
  lua_assert(e.k == VNONRELOC);
}


/*
** Fix for instruction at position 'pc' to jump to 'dest'.
** (Jump addresses are relative in Lua). 'back' true means
** a back jump.
*/
static void fixforjump (FuncState *fs, int pc, int dest, int back) {
  Instruction *jmp = &fs->f->code[pc];
  int offset = dest - (pc + 1);
  if (back)
    offset = -offset;
  if (l_unlikely(offset > MAXARG_Bx))
    luaX_syntaxerror(fs->ls, "control structure too long");
  SETARG_Bx(*jmp, offset);
}


/*
** Generate code for a 'for' loop.
*/
static void forbody (LexState *ls, int base, int line, int nvars, int isgen) {
  /* forbody -> DO block */
  static const OpCode forprep[2] = {OP_FORPREP, OP_TFORPREP};
  static const OpCode forloop[2] = {OP_FORLOOP, OP_TFORLOOP};
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int prep, endfor;
  checknext(ls, TK_DO);
  prep = luaK_codeABx(fs, forprep[isgen], base, 0);
  fs->freereg--;  /* both 'forprep' remove one register from the stack */
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(ls, nvars);
  luaK_reserveregs(fs, nvars);
  block(ls);
  leaveblock(fs);  /* end of scope for declared variables */
  fixforjump(fs, prep, luaK_getlabel(fs), 0);
  if (isgen) {  /* generic for? */
    luaK_codeABC(fs, OP_TFORCALL, base, 0, nvars);
    luaK_fixline(fs, line);
  }
  endfor = luaK_codeABx(fs, forloop[isgen], base, 0);
  fixforjump(fs, endfor, prep + 1, 1);
  luaK_fixline(fs, line);
}


static void fornum (LexState *ls, TString *varname, int line) {
  /* fornum -> NAME = exp,exp[,exp] forbody */
  FuncState *fs = ls->fs;
  int base = fs->freereg;
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_varkind(ls, varname, RDKCONST);  /* control variable */
  checknext(ls, '=');
  exp1(ls);  /* initial value */
  checknext(ls, ',');
  exp1(ls);  /* limit */
  if (testnext(ls, ','))
    exp1(ls);  /* optional step */
  else {  /* default step = 1 */
    luaK_int(fs, fs->freereg, 1);
    luaK_reserveregs(fs, 1);
  }
  adjustlocalvars(ls, 2);  /* start scope for internal variables */
  forbody(ls, base, line, 1, 0);
}


static void forlist (LexState *ls, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist forbody */
  FuncState *fs = ls->fs;
  expdesc e;
  int nvars = 4;  /* function, state, closing, control */
  int line;
  int base = fs->freereg;
  /* create internal variables */
  new_localvarliteral(ls, "(for state)");  /* iterator function */
  new_localvarliteral(ls, "(for state)");  /* state */
  new_localvarliteral(ls, "(for state)");  /* closing var. (after swap) */
  new_varkind(ls, indexname, RDKCONST);  /* control variable */
  /* other declared variables */
  while (testnext(ls, ',')) {
    new_localvar(ls, str_checkname(ls));
    nvars++;
  }
  checknext(ls, TK_IN);
  line = ls->linenumber;
  adjust_assign(ls, 4, explist(ls, &e), &e);
  adjustlocalvars(ls, 3);  /* start scope for internal variables */
  marktobeclosed(fs);  /* last internal var. must be closed */
  luaK_checkstack(fs, 2);  /* extra space to call iterator */
  forbody(ls, base, line, nvars - 3, 1);
}


static void forstat (LexState *ls, int line) {
  /* forstat -> FOR (fornum | forlist | cstyle_for) END|'}' */
  FuncState *fs = ls->fs;
  TString *varname;
  BlockCnt bl;
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  luaX_next(ls);  /* skip 'for' */
  /* Mos: Check for C-style for loop: for(init;cond;step) { body } */
  if (ls->t.token == '(') {
    luaX_next(ls);  /* skip '(' */
    
    /* Mos: Detect standard integer for loop: for(int i=start; i<=limit; i=i+step)
       or for(int i=start; i>=limit; i=i-step)
       Use Lua's efficient FORPREP/FORLOOP when possible. */
    int use_native_for = 0;
    int for_base = fs->freereg;
    int for_prep = -1;
    
    if (ls->t.token == TK_TYPE_INT) {
      luaX_next(ls);  /* skip 'int' */
      if (ls->t.token == TK_NAME) {
        TString *varname = luaS_new(ls->L, getstr(ls->t.seminfo.ts));
        luaX_next(ls);  /* skip variable name */
        if (testnext(ls, '=')) {
          expdesc initval;
          expr(ls, &initval);
          
          if (testnext(ls, ';')) {
            if (ls->t.token == TK_NAME && 
                luaS_eqstr(ls->t.seminfo.ts, varname)) {
              luaX_next(ls);  /* skip variable name */
              
              int is_le = 0, is_lt = 0, is_ge = 0, is_gt = 0;
              if (ls->t.token == TK_LE) is_le = 1;
              else if (ls->t.token == '<') is_lt = 1;
              else if (ls->t.token == TK_GE) is_ge = 1;
              else if (ls->t.token == '>') is_gt = 1;
              
              if (is_le || is_lt || is_ge || is_gt) {
                luaX_next(ls);  /* skip comparison operator */
                
                expdesc limitval;
                expr(ls, &limitval);
                
                if (testnext(ls, ';')) {
                  if (ls->t.token == TK_NAME &&
                      luaS_eqstr(ls->t.seminfo.ts, varname)) {
                    luaX_next(ls);  /* skip variable name */
                    
                    if (testnext(ls, '=')) {
                      if (ls->t.token == TK_NAME &&
                          luaS_eqstr(ls->t.seminfo.ts, varname)) {
                        luaX_next(ls);  /* skip variable name */
                        
                        int step_sign = 0;
                        if (ls->t.token == '+') step_sign = 1;
                        else if (ls->t.token == '-') step_sign = -1;
                        
                        if (step_sign != 0) {
                          luaX_next(ls);  /* skip '+' or '-' */
                          
                          expdesc stepval;
                          expr(ls, &stepval);
                          
                          if (step_sign == -1) {
                            if (stepval.k == VKINT) {
                              stepval.u.ival = -stepval.u.ival;
                            }
                          }
                          
                          if (testnext(ls, ')')) {
                            use_native_for = 1;
                            
                            fs->freereg = for_base;
                            new_localvarliteral(ls, "(for state)");
                            new_localvarliteral(ls, "(for state)");
                            int ctrlvar = new_varkind(ls, varname, RDKCONST);
                            getlocalvardesc(fs, ctrlvar)->vd.vtype = cast_byte(MOS_TYPE_INT);
                            
                            luaK_exp2nextreg(fs, &initval);
                            
                            if (is_lt) {
                              if (limitval.k == VKINT) {
                                limitval.u.ival -= 1;
                              } else {
                                luaK_exp2nextreg(fs, &limitval);
                                luaK_codeABCk(fs, OP_ADDI, 
                                  fs->freereg, limitval.u.info, int2sC(-1), 0);
                                luaK_reserveregs(fs, 1);
                                limitval.k = VNONRELOC;
                                limitval.u.info = fs->freereg - 1;
                              }
                            } else if (is_gt) {
                              if (limitval.k == VKINT) {
                                limitval.u.ival += 1;
                              } else {
                                luaK_exp2nextreg(fs, &limitval);
                                luaK_codeABCk(fs, OP_ADDI, 
                                  fs->freereg, limitval.u.info, int2sC(1), 0);
                                luaK_reserveregs(fs, 1);
                                limitval.k = VNONRELOC;
                                limitval.u.info = fs->freereg - 1;
                              }
                            }
                            luaK_exp2nextreg(fs, &limitval);
                            
                            luaK_exp2nextreg(fs, &stepval);
                            
                            adjustlocalvars(ls, 2);
                            
                            for_prep = luaK_codeABx(fs, OP_FORPREP, for_base, 0);
                            fs->freereg--;
                            
                            if (ls->t.token == '{') {
                              luaX_next(ls);  /* skip '{' */
                            } else {
                              checknext(ls, TK_DO);
                            }
                            
                            {
                              BlockCnt blbody;
                              enterblock(fs, &blbody, 0);
                              adjustlocalvars(ls, 1);
                              luaK_reserveregs(fs, 1);
                              block(ls);
                              leaveblock(fs);
                            }
                            
                            {
                              int endfor = luaK_codeABx(fs, OP_FORLOOP, for_base, 0);
                              fixforjump(fs, for_prep, luaK_getlabel(fs), 0);
                              fixforjump(fs, endfor, for_prep + 1, 1);
                              luaK_fixline(fs, line);
                            }
                            
                            if (ls->t.token == '}') {
                              luaX_next(ls);  /* skip '}' */
                            } else {
                              checknext(ls, '}');
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
      
      if (!use_native_for) {
        luaX_syntaxerror(ls, "C-style int for loop must use simple form: for(int i=start; i<=limit; i=i+step) or for(int i=start; i>=limit; i=i-step)");
      }
    }
    
    if (!use_native_for) {
      /* General C-style for loop for non-int types */
      BlockCnt bl2;
      int steplabel;
      int condpc;
      int falselist = NO_JUMP;
      int jumptobody;
      
      /* Parse init statement (optional) */
      if (ls->t.token != ';') {
        statement(ls);
      }
      checknext(ls, ';');
      /* Parse condition */
      condpc = fs->pc;
      if (ls->t.token != ';') {
        expdesc ce;
        expr(ls, &ce);
        luaK_goiftrue(fs, &ce);
        falselist = ce.f;
      }
      /* Skip step code on first iteration */
      jumptobody = luaK_jump(fs);
      /* Step code */
      steplabel = luaK_getlabel(fs);
      checknext(ls, ';');
      /* Parse step (optional) - can be an expression or assignment */
      if (ls->t.token != ')') {
        struct LHS_assign v;
        suffixedexp(ls, &v.v);
        if (ls->t.token == '=' || ls->t.token == ',') {
          v.prev = NULL;
          restassign(ls, &v, 1);
        }
        else {
          check_condition(ls, v.v.k == VCALL, "syntax error in for step");
          SETARG_C(getinstruction(fs, &v.v), 1);
        }
      }
      checknext(ls, ')');
      /* After step, jump back to condition check */
      luaK_patchlist(fs, luaK_jump(fs), condpc);
      /* Body code - patch the skip-step jump here */
      luaK_patchtohere(fs, jumptobody);
      if (ls->t.token == '{') {
        luaX_next(ls);  /* skip '{' */
      } else {
        checknext(ls, TK_DO);
      }
      enterblock(fs, &bl2, 0);
      block(ls);
      leaveblock(fs);
      /* After body, jump to step */
      luaK_patchlist(fs, luaK_jump(fs), steplabel);
      /* Patch false-jump list to here (L_end) */
      luaK_patchtohere(fs, falselist);
      /* Expect '}' */
      checknext(ls, '}');
    }
  }
  else {
    varname = str_checkname(ls);  /* first variable name */
    switch (ls->t.token) {
      case '=': fornum(ls, varname, line); break;
      case ',': case TK_IN: forlist(ls, varname); break;
      default: luaX_syntaxerror(ls, "'=' or 'in' expected");
    }
    checknext(ls, '}');
  }
  leaveblock(fs);  /* loop scope ('break' jumps to this point) */
}


static void test_then_block (LexState *ls, int *escapelist) {
  /* test_then_block -> [IF | ELSEIF] cond THEN|'{' block */
  FuncState *fs = ls->fs;
  int condtrue;
  luaX_next(ls);  /* skip IF or ELSEIF */
  /* Mos: Support C-style if (cond) { ... } */
  if (ls->t.token == '(') {
    luaX_next(ls);  /* skip '(' */
  }
  condtrue = cond(ls);  /* read condition */
  if (ls->t.token == ')') {
    luaX_next(ls);  /* skip ')' */
  }
  /* Mos: Support both 'then' and '{' */
  if (ls->t.token == '{') {
    luaX_next(ls);  /* skip '{' */
  } else {
    checknext(ls, TK_THEN);
  }
  block(ls);  /* 'then' part */
  if (ls->t.token == TK_ELSE ||
      ls->t.token == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
    luaK_concat(fs, escapelist, luaK_jump(fs));  /* must jump over it */
  luaK_patchtohere(fs, condtrue);
}


static void ifstat (LexState *ls, int line) {
  /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] '}' */
  FuncState *fs = ls->fs;
  int escapelist = NO_JUMP;  /* exit list for finished parts */
  test_then_block(ls, &escapelist);  /* IF cond THEN block */
  while (ls->t.token == TK_ELSEIF)
    test_then_block(ls, &escapelist);  /* ELSEIF cond THEN block */
  /* Mos: Consume '}' before checking for 'else' */
  if (ls->t.token == '}') {
    luaX_next(ls);  /* skip '}' */
  }
  if (testnext(ls, TK_ELSE)) {
    /* Mos: Support '{' after else */
    if (ls->t.token == '{') {
      luaX_next(ls);  /* skip '{' */
    }
    block(ls);  /* 'else' part */
    /* Mos: Consume closing '}' for else block */
    if (ls->t.token == '}') {
      luaX_next(ls);  /* skip '}' */
    }
  }
  luaK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}


static void localfunc (LexState *ls) {
  expdesc b;
  FuncState *fs = ls->fs;
  int fvar = fs->nactvar;  /* function's variable index */
  new_localvar(ls, str_checkname(ls));  /* new local variable */
  adjustlocalvars(ls, 1);  /* enter its scope */
  body(ls, &b, 0, ls->linenumber, MOS_TYPE_UNKNOWN);  /* function created in next register */
  /* debug information will only see the variable after this point! */
  localdebuginfo(fs, fvar)->startpc = fs->pc;
}


static lu_byte getvarattribute (LexState *ls, lu_byte df) {
  /* attrib -> ['<' NAME '>'] */
  if (testnext(ls, '<')) {
    TString *ts = str_checkname(ls);
    const char *attr = getstr(ts);
    checknext(ls, '>');
    if (strcmp(attr, "const") == 0)
      return RDKCONST;  /* read-only variable */
    else if (strcmp(attr, "close") == 0)
      return RDKTOCLOSE;  /* to-be-closed variable */
    else
      luaK_semerror(ls, "unknown attribute '%s'", attr);
  }
  return df;  /* return default value */
}


static void checktoclose (FuncState *fs, int level) {
  if (level != -1) {  /* is there a to-be-closed variable? */
    marktobeclosed(fs);
    luaK_codeABC(fs, OP_TBC, reglevel(fs, level), 0, 0);
  }
}


static void localstat (LexState *ls) {
  /* stat -> LOCAL [type] NAME attrib { ',' [type] NAME attrib } ['=' explist] */
  FuncState *fs = ls->fs;
  int toclose = -1;  /* index of to-be-closed variable (if any) */
  Vardesc *var;  /* last variable */
  int vidx;  /* index of last variable */
  int nvars = 0;
  int nexps;
  expdesc e;
  /* get prefixed attribute (if any); default is regular local variable */
  lu_byte defkind = getvarattribute(ls, VDKREG);
  do {  /* for each variable */
    int vtype = MOS_TYPE_UNKNOWN;  /* no default type - must be declared */
    /* Mos: Check for type keyword */
    switch (ls->t.token) {
      case TK_TYPE_INT:
      case TK_TYPE_FLOAT:
      case TK_TYPE_BOOL:
      case TK_TYPE_STRING:
      case TK_TYPE_VOID:
        vtype = token_to_type(ls->t.token);
        luaX_next(ls);  /* skip type keyword */
        break;
      default:
        luaX_syntaxerror(ls, "variable declaration requires type (e.g., 'int name')");
        break;
    }
    TString *vname = str_checkname(ls);  /* get its name */
    lu_byte kind = getvarattribute(ls, defkind);  /* postfixed attribute */
    vidx = new_varkind(ls, vname, kind);  /* predeclare it */
    /* Mos: Set variable type */
    Vardesc *vd = getlocalvardesc(fs, vidx);
    vd->vd.vtype = cast_byte(vtype);
    if (kind == RDKTOCLOSE) {  /* to-be-closed? */
      if (toclose != -1)  /* one already present? */
        luaK_semerror(ls, "multiple to-be-closed variables in local list");
      toclose = fs->nactvar + nvars;
    }
    nvars++;
  } while (testnext(ls, ','));
  if (testnext(ls, '='))  /* initialization? */
    nexps = explist(ls, &e);
  else {
    e.k = VVOID;
    nexps = 0;
  }
  var = getlocalvardesc(fs, vidx);  /* retrieve last variable */
  if (nvars == nexps &&  /* no adjustments? */
      var->vd.kind == RDKCONST &&  /* last variable is const? */
      luaK_exp2const(fs, &e, &var->k)) {  /* compile-time constant? */
    var->vd.kind = RDKCTC;  /* variable is a compile-time constant */
    adjustlocalvars(ls, nvars - 1);  /* exclude last variable */
    fs->nactvar++;  /* but count it */
  }
  else {
    /* Mos: Type checking for local variable initialization */
    if (nexps > 0 && e.k != VVOID) {
      /* Check the last variable against the expression */
      check_assignment_type(ls, var, &e);
    }
    adjust_assign(ls, nvars, nexps, &e);
    adjustlocalvars(ls, nvars);
  }
  checktoclose(fs, toclose);
}


static lu_byte getglobalattribute (LexState *ls, lu_byte df) {
  lu_byte kind = getvarattribute(ls, df);
  switch (kind) {
    case RDKTOCLOSE:
      luaK_semerror(ls, "global variables cannot be to-be-closed");
      return kind;  /* to avoid warnings */
    case RDKCONST:
      return GDKCONST;  /* adjust kind for global variable */
    default:
      return kind;
  }
}


static void checkglobal (LexState *ls, TString *varname, int line) {
  FuncState *fs = ls->fs;
  expdesc var;
  int k;
  buildglobal(ls, varname, &var);  /* create global variable in 'var' */
  k = var.u.ind.keystr;  /* index of global name in 'k' */
  luaK_codecheckglobal(fs, &var, k, line);
}


/*
** Recursively traverse list of globals to be initalized. When
** going, generate table description for the global. In the end,
** after all indices have been generated, read list of initializing
** expressions. When returning, generate the assignment of the value on
** the stack to the corresponding table description. 'n' is the variable
** being handled, range [0, nvars - 1].
*/
static void initglobal (LexState *ls, int nvars, int firstidx, int n,
                        int line) {
  if (n == nvars) {  /* traversed all variables? */
    expdesc e;
    int nexps = explist(ls, &e);  /* read list of expressions */
    adjust_assign(ls, nvars, nexps, &e);
  }
  else {  /* handle variable 'n' */
    FuncState *fs = ls->fs;
    expdesc var;
    TString *varname = getlocalvardesc(fs, firstidx + n)->vd.name;
    buildglobal(ls, varname, &var);  /* create global variable in 'var' */
    enterlevel(ls);  /* control recursion depth */
    initglobal(ls, nvars, firstidx, n + 1, line);
    leavelevel(ls);
    checkglobal(ls, varname, line);
    storevartop(fs, &var);
  }
}


static void globalnames (LexState *ls, lu_byte defkind) {
  FuncState *fs = ls->fs;
  int nvars = 0;
  int lastidx;  /* index of last registered variable */
  do {  /* for each name */
    TString *vname = str_checkname(ls);
    lu_byte kind = getglobalattribute(ls, defkind);
    lastidx = new_varkind(ls, vname, kind);
    nvars++;
  } while (testnext(ls, ','));
  if (testnext(ls, '='))  /* initialization? */
    initglobal(ls, nvars, lastidx - nvars + 1, 0, ls->linenumber);
  fs->nactvar = cast_short(fs->nactvar + nvars);  /* activate declaration */
}


static void globalstat (LexState *ls) {
  /* globalstat -> (GLOBAL) attrib '*'
     globalstat -> (GLOBAL) attrib NAME attrib {',' NAME attrib} */
  FuncState *fs = ls->fs;
  /* get prefixed attribute (if any); default is regular global variable */
  lu_byte defkind = getglobalattribute(ls, GDKREG);
  if (!testnext(ls, '*'))
    globalnames(ls, defkind);
  else {
    /* use NULL as name to represent '*' entries */
    new_varkind(ls, NULL, defkind);
    fs->nactvar++;  /* activate declaration */
  }
}


static void globalfunc (LexState *ls, int line) {
  /* globalfunc -> (GLOBAL FUNCTION) NAME body */
  expdesc var, b;
  FuncState *fs = ls->fs;
  TString *fname = str_checkname(ls);
  new_varkind(ls, fname, GDKREG);  /* declare global variable */
  fs->nactvar++;  /* enter its scope */
  buildglobal(ls, fname, &var);
  body(ls, &b, 0, ls->linenumber, MOS_TYPE_UNKNOWN);  /* compile and return closure in 'b' */
  checkglobal(ls, fname, line);
  luaK_storevar(fs, &var, &b);
  luaK_fixline(fs, line);  /* definition "happens" in the first line */
}


static void globalstatfunc (LexState *ls, int line) {
  /* stat -> GLOBAL globalfunc | GLOBAL globalstat */
  luaX_next(ls);  /* skip 'global' */
  /* In C-style, just call globalfunc directly */
  globalfunc(ls, line);
}


static int funcname (LexState *ls, expdesc *v) {
  /* funcname -> NAME {fieldsel} [':' NAME] */
  int ismethod = 0;
  singlevar(ls, v);
  while (ls->t.token == '.')
    fieldsel(ls, v);
  if (ls->t.token == ':') {
    ismethod = 1;
    fieldsel(ls, v);
  }
  return ismethod;
}


static void apifuncstat (LexState *ls, int line) {
  /* apifuncstat -> [type] funcname body (marked as @API) */
  int ismethod;
  expdesc v, b;
  int rettype = MOS_TYPE_UNKNOWN;
  
  /* Mos: Parse optional return type */
  if (ls->t.token == TK_TYPE_INT || ls->t.token == TK_TYPE_FLOAT || 
      ls->t.token == TK_TYPE_BOOL || ls->t.token == TK_TYPE_STRING ||
      ls->t.token == TK_TYPE_VOID) {
    rettype = token_to_type(ls->t.token);
    luaX_next(ls);  /* skip type */
  }
  
  /* Get function name */
  ismethod = funcname(ls, &v);
  check_readonly(ls, &v);
  body(ls, &b, ismethod, line, rettype);
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);  /* definition "happens" in the first line */
  /* Mark the function as API */
  ls->fs->f->flag |= PF_API;
}


static void funcstat (LexState *ls, int line) {
  /* funcstat -> [type] funcname body */
  int ismethod;
  expdesc v, b;
  int rettype = MOS_TYPE_UNKNOWN;
  /* In C-style, type is already consumed by caller */
  
  /* Mos: Parse optional return type */
  if (ls->t.token == TK_TYPE_INT || ls->t.token == TK_TYPE_FLOAT || 
      ls->t.token == TK_TYPE_BOOL || ls->t.token == TK_TYPE_STRING ||
      ls->t.token == TK_TYPE_VOID) {
    rettype = token_to_type(ls->t.token);
    luaX_next(ls);  /* skip type */
  }
  
  ismethod = funcname(ls, &v);
  check_readonly(ls, &v);
  body(ls, &b, ismethod, line, rettype);
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);  /* definition "happens" in the first line */
}


static void classstat (LexState *ls, int line) {
  /* classstat -> CLASS NAME '{' { field | method } '}' */
  expdesc v, classvar;
  TString *classname;
  int has_init = 0;
  
  luaX_next(ls);  /* skip CLASS */
  classname = str_checkname(ls);  /* class name */
  
  /* Create class table */
  int pc = luaK_codevABCk(ls->fs, OP_NEWTABLE, 0, 0, 0, 0);
  luaK_code(ls->fs, 0);  /* space for extra arg */
  init_exp(&v, VNONRELOC, ls->fs->freereg);  /* table will be at stack top */
  luaK_reserveregs(ls->fs, 1);
  int classreg = v.u.info;
  luaK_settablesize(ls->fs, pc, classreg, 0, 0);
  
  /* Parse class body (fields and methods) */
  checknext(ls, '{');
  while (ls->t.token != '}' && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_TYPE_INT || ls->t.token == TK_TYPE_FLOAT || 
        ls->t.token == TK_TYPE_BOOL || ls->t.token == TK_TYPE_STRING ||
        ls->t.token == TK_TYPE_VOID) {
      /* Could be field declaration or method - need to peek ahead */
      int rettype = token_to_type(ls->t.token);
      int lookahead = luaX_lookahead(ls);
      
      /* If after type+name we see '(', it's a method */
      if (lookahead == TK_NAME) {
        /* Skip type and name to check what follows */
        luaX_next(ls);  /* skip type */
        TString *nextname = str_checkname(ls);
        
        if (ls->t.token == '(') {
          /* It's a method: type name(...) { ... } */
          expdesc methodkey, methodvar, methodbody;
          FuncState *fs = ls->fs;
          
          /* Put back the name token by re-parsing it */
          codestring(&methodkey, nextname);
          
          /* Check if this is init method */
          if (strcmp(getstr(nextname), "init") == 0) {
            has_init = 1;
          }
          
          /* Create expression for class[methodname] */
          methodvar = v;
          luaK_indexed(fs, &methodvar, &methodkey);
          
          /* Parse method body (ismethod=1 for self) */
          body(ls, &methodbody, 1, ls->linenumber, rettype);
          
          /* Store method in class table */
          luaK_storevar(fs, &methodvar, &methodbody);
          
          /* Refresh class table expression */
          init_exp(&v, VNONRELOC, classreg);
        } else {
          /* It's a field: type name */
          /* Already consumed type and name, nothing more to do */
        }
      } else {
        /* Just skip the type keyword for now */
        luaX_next(ls);
        str_checkname(ls);
      }
    }
    else {
      luaX_syntaxerror(ls, "expected field or method declaration in class");
    }
    testnext(ls, ';');  /* skip optional semicolon */
  }
  
  checknext(ls, '}');
  
  /* Store class table to global variable */
  buildglobal(ls, classname, &classvar);
  luaK_storevar(ls->fs, &classvar, &v);
  luaK_fixline(ls->fs, line);
}


static void exprstat (LexState *ls) {
  /* stat -> func | assignment */
  FuncState *fs = ls->fs;
  struct LHS_assign v;
  suffixedexp(ls, &v.v);
  if (ls->t.token == '=' || ls->t.token == ',') { /* stat -> assignment ? */
    v.prev = NULL;
    restassign(ls, &v, 1);
  }
  else {  /* stat -> func */
    Instruction *inst;
    check_condition(ls, v.v.k == VCALL, "syntax error");
    inst = &getinstruction(fs, &v.v);
    SETARG_C(*inst, 1);  /* call statement uses no results */
  }
}


static void retstat (LexState *ls) {
  /* stat -> RETURN [explist] [';'] */
  FuncState *fs = ls->fs;
  expdesc e;
  int nret;  /* number of values being returned */
  int first = luaY_nvarstack(fs);  /* first slot to be returned */
  if (block_follow(ls, 1) || ls->t.token == ';')
    nret = 0;  /* return no values */
  else {
    nret = explist(ls, &e);  /* optional return values */
    /* Mos: Check return type compatibility */
    if (nret == 1 && fs->rettype != MOS_TYPE_UNKNOWN) {
      int ret_expr_type = e.etype;
      if (ret_expr_type != MOS_TYPE_UNKNOWN) {
        if (!types_compatible(fs->rettype, ret_expr_type)) {
          char msg[256];
          snprintf(msg, sizeof(msg), "cannot return '%s' from function with return type '%s'",
                   type_to_string(ret_expr_type), type_to_string(fs->rettype));
          type_error(ls, msg, ls->linenumber);
        }
      }
    }
    if (hasmultret(e.k)) {
      luaK_setmultret(fs, &e);
      if (e.k == VCALL && nret == 1 && !fs->bl->insidetbc) {  /* tail call? */
        SET_OPCODE(getinstruction(fs,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getinstruction(fs,&e)) == luaY_nvarstack(fs));
      }
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1)  /* only one single value? */
        first = luaK_exp2anyreg(fs, &e);  /* can use original slot */
      else {  /* values must go to the top of the stack */
        luaK_exp2nextreg(fs, &e);
        lua_assert(nret == fs->freereg - first);
      }
    }
  }
  /* Mos: Check void function doesn't return value */
  if (nret > 0 && fs->rettype == MOS_TYPE_VOID) {
    type_error(ls, "void function cannot return a value", ls->linenumber);
  }
  /* Mos: Check non-void function returns a value */
  if (nret == 0 && fs->rettype != MOS_TYPE_VOID && fs->rettype != MOS_TYPE_UNKNOWN) {
    char msg[256];
    snprintf(msg, sizeof(msg), "non-void function must return a value (expected '%s')",
             type_to_string(fs->rettype));
    type_error(ls, msg, ls->linenumber);
  }
  luaK_ret(fs, first, nret);
  testnext(ls, ';');  /* skip optional semicolon */
}


/* Mos: try/catch statement
   try () { body } catch (e) { handler }
   
   Compiled as pcall wrapping:
     local ok, err = pcall(function() body end)
     if not ok then
       local e = err
       handler
     end
*/
static void trycatch (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  FuncState new_fs;
  BlockCnt bl, bl2;
  expdesc tryfunc, pcallfunc, key, result;
  
  luaX_next(ls);  /* skip 'try' */
  
  /* Create a local variable to hold the try body closure */
  int fvar = fs->nactvar;
  new_localvarliteral(ls, "(try func)");
  adjustlocalvars(ls, 1);
  
  /* Parse try body as a closure: function() body end */
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);
  new_fs.rettype = cast_byte(MOS_TYPE_VOID);  /* try block returns void */
  checknext(ls, '{');
  statlist(ls);
  new_fs.f->lastlinedefined = ls->linenumber;
  checknext(ls, '}');
  codeclosure(ls, &tryfunc);
  close_func(ls);
  
  /* Store closure into the local variable */
  fs = ls->fs;
  {
    expdesc var;
    init_var(fs, &var, fvar);
    luaK_storevar(fs, &var, &tryfunc);
  }
  
  /* Generate: pcall(tryfunc) */
  singlevaraux(fs, ls->envn, &pcallfunc, 1);
  codestring(&key, luaS_new(ls->L, "pcall"));
  luaK_indexed(fs, &pcallfunc, &key);
  luaK_exp2nextreg(fs, &pcallfunc);
  {
    expdesc tryref;
    init_var(fs, &tryref, fvar);
    luaK_exp2nextreg(fs, &tryref);
  }
  
  /* Generate function call: pcall(tryfunc) - returns 2 values: ok, err */
  {
    int base = pcallfunc.u.info;
    int nparams = fs->freereg - (base + 1);
    init_exp(&result, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2+1));
    luaK_fixline(fs, line);
    fs->freereg = cast_byte(base + 1);  /* keep only base register */
  }
  
  /* pcall returns ok in base, err in base+1 */
  /* Create local variables for pcall results */
  int ok_reg = pcallfunc.u.info;
  int err_reg = ok_reg + 1;
  
  enterblock(fs, &bl2, 0);
  int okvar = fs->nactvar;
  new_localvarliteral(ls, "(try ok)");
  int errvar_idx = fs->nactvar;
  new_localvarliteral(ls, "(try err)");
  adjustlocalvars(ls, 2);
  /* Override register assignments to match pcall return value positions */
  getlocalvardesc(fs, okvar)->vd.ridx = cast_byte(ok_reg);
  getlocalvardesc(fs, okvar)->vd.vtype = cast_byte(MOS_TYPE_BOOL);
  getlocalvardesc(fs, errvar_idx)->vd.ridx = cast_byte(err_reg);
  getlocalvardesc(fs, errvar_idx)->vd.vtype = cast_byte(MOS_TYPE_STRING);
  fs->freereg = cast_byte(err_reg + 1);
  
  /* Now parse catch block */
  checknext(ls, TK_CATCH);
  
  /* Parse catch variable name */
  TString *errvar = NULL;
  if (testnext(ls, '(')) {
    errvar = str_checkname(ls);
    checknext(ls, ')');
  }
  
  /* Generate: if not ok then ... end */
  {
    expdesc cond_e;
    init_exp(&cond_e, VNONRELOC, ok_reg);
    cond_e.etype = MOS_TYPE_BOOL;
    luaK_goiftrue(fs, &cond_e);
    
    int escapelist = NO_JUMP;
    luaK_concat(fs, &escapelist, luaK_jump(fs));
    luaK_patchtohere(fs, cond_e.f);
    
    /* Create error variable if specified */
    if (errvar) {
      int eidx = new_varkind(ls, errvar, VDKREG);
      adjustlocalvars(ls, 1);
      /* Override register to use err_reg directly */
      getlocalvardesc(fs, eidx)->vd.ridx = cast_byte(err_reg);
      getlocalvardesc(fs, eidx)->vd.vtype = cast_byte(MOS_TYPE_STRING);
    }
    
    /* Parse catch body */
    checknext(ls, '{');
    block(ls);
    
    checknext(ls, '}');
    
    luaK_patchtohere(fs, escapelist);
  }
  
  leaveblock(fs);
}


/* Mos: import statement
   import module_name
   import module_name as alias
   
   Compiled as:
     local module_name = require("module_name")
   or:
     local alias = require("module_name")
*/
static void importstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  TString *modname;
  TString *alias;
  
  luaX_next(ls);  /* skip 'import' */
  
  /* Parse module name - can be a dotted name like a.b.c */
  modname = str_checkname(ls);
  
  /* Build the full module name string */
  TString *fullmodname = modname;
  while (ls->t.token == '.') {
    luaX_next(ls);  /* skip '.' */
    TString *part = str_checkname(ls);
    size_t len1 = tsslen(fullmodname);
    size_t len2 = tsslen(part);
    size_t totlen = len1 + 1 + len2;
    void *buf = luaM_malloc_(ls->L, totlen + 1, 0);
    memcpy(buf, getstr(fullmodname), len1);
    ((char*)buf)[len1] = '.';
    memcpy((char*)buf + len1 + 1, getstr(part), len2);
    ((char*)buf)[totlen] = '\0';
    fullmodname = luaS_newlstr(ls->L, (const char*)buf, totlen);
    luaM_freemem(ls->L, buf, totlen + 1);
  }
  
  /* Check for 'as' alias */
  if (ls->t.token == TK_NAME && 
      tsslen(ls->t.seminfo.ts) == 2 &&
      getstr(ls->t.seminfo.ts)[0] == 'a' &&
      getstr(ls->t.seminfo.ts)[1] == 's') {
    luaX_next(ls);  /* skip 'as' */
    alias = str_checkname(ls);
  } else {
    alias = modname;  /* default alias is the first part of module name */
  }
  
  /* Generate: local alias = require("fullmodname") */
  {
    expdesc requirefunc, key, modarg, result;
    int varidx;
    
    /* Look up 'require' in _ENV */
    singlevaraux(fs, ls->envn, &requirefunc, 1);
    codestring(&key, luaS_new(ls->L, "require"));
    luaK_indexed(fs, &requirefunc, &key);
    luaK_exp2nextreg(fs, &requirefunc);
    
    /* Push module name as argument */
    codestring(&modarg, fullmodname);
    luaK_exp2nextreg(fs, &modarg);
    
    /* Call require("fullmodname") */
    {
      int base = requirefunc.u.info;
      int nparams = fs->freereg - (base + 1);
      init_exp(&result, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
      luaK_fixline(fs, line);
      fs->freereg = cast_byte(base + 1);
    }
    
    /* Create local variable for the result */
    varidx = new_varkind(ls, alias, VDKREG);
    adjustlocalvars(ls, 1);
    {
      expdesc var;
      init_var(fs, &var, varidx);
      luaK_storevar(fs, &var, &result);
    }
  }
  
  testnext(ls, ';');  /* skip optional semicolon */
}


/* Mos: export statement
   export function name() { ... }
   export type name = value
   export name
   
   Makes the declaration available as a global variable.
*/
static void exportstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  
  luaX_next(ls);  /* skip 'export' */
  
  /* export type name = value  or  export type name(params) { ... } */
  if (ls->t.token == TK_TYPE_INT || ls->t.token == TK_TYPE_FLOAT ||
      ls->t.token == TK_TYPE_BOOL || ls->t.token == TK_TYPE_STRING ||
      ls->t.token == TK_TYPE_VOID) {
    int rettype = token_to_type(ls->t.token);
    luaX_next(ls);  /* skip type keyword */
    
    if (ls->t.token == TK_NAME) {
      TString *vname = ls->t.seminfo.ts;
      luaX_next(ls);  /* skip name */
      
      if (ls->t.token == '(') {
        /* export type name(params) { ... } - function */
        expdesc b;
        FuncState *fs = ls->fs;
        int fvar = fs->nactvar;
        int vidx = new_varkind(ls, vname, VDKREG);
        Vardesc *vd = getlocalvardesc(fs, vidx);
        vd->vd.vtype = cast_byte(MOS_TYPE_UNKNOWN);
        adjustlocalvars(ls, 1);
        body(ls, &b, 0, line, rettype);
        localdebuginfo(fs, fvar)->startpc = fs->pc;
        localdebuginfo(fs, fvar)->endpc = fs->pc;
        testnext(ls, ';');
        return;
      }
      else if (testnext(ls, '=')) {
        /* export type name = value */
        expdesc e;
        expr(ls, &e);
        
        expdesc var, key;
        singlevaraux(fs, ls->envn, &var, 1);
        codestring(&key, vname);
        luaK_indexed(fs, &var, &key);
        luaK_storevar(fs, &var, &e);
        testnext(ls, ';');
        return;
      }
    }
  }
  
  /* export function name() { ... } */
  if (ls->t.token == TK_NAME) {
    expdesc v, b;
    int ismethod = funcname(ls, &v);
    body(ls, &b, ismethod, line, MOS_TYPE_UNKNOWN);
    luaK_storevar(fs, &v, &b);
    testnext(ls, ';');
    return;
  }
  
  luaX_syntaxerror(ls, "unexpected symbol after 'export'");
}


static void statement (LexState *ls) {
  int line = ls->linenumber;  /* may be needed for error messages */
  enterlevel(ls);
  switch (ls->t.token) {
    case ';': {  /* stat -> ';' (empty statement) */
      luaX_next(ls);  /* skip ';' */
      break;
    }
    case '@': {  /* stat -> '@API' function */
      luaX_next(ls);  /* skip '@' */
      if (ls->t.token == TK_API) {
        luaX_next(ls);  /* skip 'API' */
        apifuncstat(ls, line);
      } else {
        luaX_syntaxerror(ls, "expected '@API' annotation");
      }
      break;
    }
    case TK_IF: {  /* stat -> ifstat */
      ifstat(ls, line);
      break;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(ls, line);
      break;
    }
    case TK_DO: {  /* stat -> DO block END */
      luaX_next(ls);  /* skip DO */
      checknext(ls, '{');
      block(ls);
      checknext(ls, '}');
      break;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(ls, line);
      break;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(ls, line);
      break;
    }
    case TK_CLASS: {  /* stat -> classstat */
      classstat(ls, line);
      break;
    }
    case TK_GLOBAL: {  /* stat -> globalstatfunc */
      globalstatfunc(ls, line);
      break;
    }
    case TK_DBCOLON: {  /* stat -> label */
      luaX_next(ls);  /* skip double colon */
      labelstat(ls, str_checkname(ls), line);
      break;
    }
    case TK_RETURN: {  /* stat -> retstat */
      luaX_next(ls);  /* skip RETURN */
      retstat(ls);
      break;
    }
    case TK_BREAK: {  /* stat -> breakstat */
      breakstat(ls, line);
      break;
    }
    case TK_SWITCH: {  /* stat -> switchstat */
      switchstat(ls, line);
      break;
    }
    case TK_GOTO: {  /* stat -> 'goto' NAME */
      luaX_next(ls);  /* skip 'goto' */
      gotostat(ls, line);
      break;
    }
    case TK_TRY: {  /* stat -> trycatch */
      trycatch(ls, line);
      break;
    }
    case TK_IMPORT: {  /* stat -> import */
      importstat(ls, line);
      break;
    }
    case TK_EXPORT: {  /* stat -> export */
      exportstat(ls, line);
      break;
    }
    /* Mos: Type keywords at start of statement mean local variable declaration
       or C-style function declaration: type name(params) { body } */
    case TK_TYPE_INT:
    case TK_TYPE_FLOAT:
    case TK_TYPE_BOOL:
    case TK_TYPE_STRING:
    case TK_TYPE_VOID: {
      int rettype = token_to_type(ls->t.token);
      luaX_next(ls);  /* skip type keyword */
      if (ls->t.token == TK_NAME) {
        TString *vname = ls->t.seminfo.ts;
        /* Peek ahead: if next-next token is '(', it's a function */
        luaX_next(ls);  /* consume name, now current token is what follows */
        if (ls->t.token == '(') {
          /* C-style function declaration: type name(params) { body } */
          expdesc b;
          FuncState *fs = ls->fs;
          int fvar = fs->nactvar;
          /* Create a local variable for the function name */
          int vidx = new_varkind(ls, vname, VDKREG);
          Vardesc *vd = getlocalvardesc(fs, vidx);
          vd->vd.vtype = cast_byte(MOS_TYPE_UNKNOWN);  /* function type not yet implemented */
          adjustlocalvars(ls, 1);
          body(ls, &b, 0, line, rettype);
          /* debug info will only see the variable after this point */
          localdebuginfo(fs, fvar)->startpc = fs->pc;
          break;
        }
        /* Not a function declaration - local variable declaration */
        {
          FuncState *fs = ls->fs;
          int toclose = -1;
          Vardesc *var;
          int vidx;
          int nvars = 0;
          int nexps;
          expdesc e;
          lu_byte kind = VDKREG;
          /* Name was already consumed, create variable with it */
          vidx = new_varkind(ls, vname, kind);
          Vardesc *vd2 = getlocalvardesc(fs, vidx);
          vd2->vd.vtype = cast_byte(rettype);
          nvars++;
          while (testnext(ls, ',')) {
            int vtype = MOS_TYPE_UNKNOWN;
            switch (ls->t.token) {
              case TK_TYPE_INT:
              case TK_TYPE_FLOAT:
              case TK_TYPE_BOOL:
              case TK_TYPE_STRING:
              case TK_TYPE_VOID:
                vtype = token_to_type(ls->t.token);
                luaX_next(ls);
                break;
              default:
                luaX_syntaxerror(ls, "variable declaration requires type (e.g., 'int name')");
                break;
            }
            vname = str_checkname(ls);
            kind = getvarattribute(ls, VDKREG);
            vidx = new_varkind(ls, vname, kind);
            vd2 = getlocalvardesc(fs, vidx);
            vd2->vd.vtype = cast_byte(vtype);
            nvars++;
          }
          if (testnext(ls, '='))
            nexps = explist(ls, &e);
          else {
            e.k = VVOID;
            nexps = 0;
          }
          var = getlocalvardesc(fs, vidx);
          if (nvars == nexps &&
              var->vd.kind == RDKCONST &&
              luaK_exp2const(fs, &e, &var->k)) {
            var->vd.kind = RDKCTC;
            adjustlocalvars(ls, nvars - 1);
            fs->nactvar++;
          }
          else {
            if (nexps > 0 && e.k != VVOID) {
              check_assignment_type(ls, var, &e);
            }
            adjust_assign(ls, nvars, nexps, &e);
            adjustlocalvars(ls, nvars);
          }
          checktoclose(fs, toclose);
        }
      }
      break;
    }
#if defined(LUA_COMPAT_GLOBAL)
    case TK_NAME: {
      /* compatibility code to parse global keyword when "global"
         is not reserved */
      if (ls->t.seminfo.ts == ls->glbn) {  /* current = "global"? */
        int lk = luaX_lookahead(ls);
        if (lk == '<' || lk == TK_NAME || lk == '*') {
          /* 'global <attrib>' or 'global name' or 'global *' or
             'global function' */
          globalstatfunc(ls, line);
          break;
        }
      }  /* else... */
    }
#endif
    /* FALLTHROUGH */
    default: {  /* stat -> func | assignment */
      exprstat(ls);
      break;
    }
  }
  lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
             ls->fs->freereg >= luaY_nvarstack(ls->fs));
  ls->fs->freereg = luaY_nvarstack(ls->fs);  /* free registers */
  leavelevel(ls);
}

/* }====================================================================== */

/* }====================================================================== */


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named LUA_ENV
*/
static void mainfunc (LexState *ls, FuncState *fs) {
  BlockCnt bl;
  Upvaldesc *env;
  open_func(ls, fs, &bl);
  setvararg(fs);  /* main function is always vararg */
  env = allocupvalue(fs);  /* ...set environment upvalue */
  env->instack = 0;  /* _ENV is a closed upvalue */
  env->idx = 0;
  env->kind = VDKREG;
  env->name = ls->envn;
  luaC_objbarrier(ls->L, fs->f, env->name);
  luaX_next(ls);  /* read first token */
  statlist(ls);  /* parse main body */
  check(ls, TK_EOS);
  close_func(ls);
}


LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                       Dyndata *dyd, const char *name, int firstchar) {
  LexState lexstate;
  FuncState funcstate;
  LClosure *cl = luaF_newLclosure(L, 1);  /* create main closure */
  setclLvalue2s(L, L->top.p, cl);  /* anchor it (to avoid being collected) */
  luaD_inctop(L);
  lexstate.h = luaH_new(L);  /* create table for scanner */
  sethvalue2s(L, L->top.p, lexstate.h);  /* anchor it */
  luaD_inctop(L);
  funcstate.f = cl->p = luaF_newproto(L);
  luaC_objbarrier(L, cl, cl->p);
  funcstate.f->source = luaS_new(L, name);  /* create and anchor TString */
  luaC_objbarrier(L, funcstate.f, funcstate.f->source);
  lexstate.buff = buff;
  lexstate.dyd = dyd;
  dyd->actvar.n = dyd->gt.n = dyd->label.n = 0;
  luaX_setinput(L, &lexstate, z, funcstate.f->source, firstchar);
  mainfunc(&lexstate, &funcstate);
  lua_assert(!funcstate.prev && funcstate.nups == 1 && !lexstate.fs);
  /* all scopes should be correctly finished */
  lua_assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->label.n == 0);
  L->top.p--;  /* remove scanner's table */
  return cl;  /* closure is on the stack, too */
}

