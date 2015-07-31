#ifndef SYSCALL_REPL_H
#define SYSCALL_REPL_H

#include "pin.H"
#include <stdio.h>

void syscallRepl_main();

typedef void (*replFunc_t)(IMG);
void syscallRepl_registerReplFunc(replFunc_t func);

void syscallRepl_replInNewProcess();

// Utility macro to count number of macro variadic arguments
#define VA_NUM_ARGS(...) VA_NUM_ARGS_(__VA_ARGS__,6,5,4,3,2,1)
#define VA_NUM_ARGS_(_1,_2,_3,_4,_5,_6,N,...) N

// Utility macro overloading
#define OVR_MACRO(M, ...)      OVR_MACRO_(M, VA_NUM_ARGS(__VA_ARGS__))
#define OVR_MACRO_(M, nargs)   OVR_MACRO__(M, nargs)
#define OVR_MACRO__(M, nargs)  M ## nargs

// Utility macro to expand list of arguments
#define EXPAND1(M,a1) M(a1)
#define EXPAND2(M,a1,a2) \
  EXPAND1(M,a1), M(a2)
#define EXPAND3(M,a1,a2,a3) \
  EXPAND2(M,a1,a2), M(a3)
#define EXPAND4(M,a1,a2,a3,a4) \
  EXPAND3(M,a1,a2,a3), M(a4)
#define EXPAND5(M,a1,a2,a3,a4,a5) \
  EXPAND4(M,a1,a2,a3,a4), M(a5)
#define EXPAND6(M,a1,a2,a3,a4,a5,a6) \
  EXPAND5(M,a1,a2,a3,a4,a5), M(a6)
  
// Utility macro to expand a number into list of zero to that number
#define ZERO_TO_n(n) OVR_MACRO_(ZERO_TO_, n)
#define ZERO_TO_1 0, 1
#define ZERO_TO_2 ZERO_TO_1, 2
#define ZERO_TO_3 ZERO_TO_2, 3
#define ZERO_TO_4 ZERO_TO_3, 4
#define ZERO_TO_5 ZERO_TO_4, 5
#define ZERO_TO_6 ZERO_TO_5, 6

// Macro to call the provided original function pointer
#define CALL_APP_FUN(origFnc, ctxt, res, ...) \
  PIN_CallApplicationFunction(ctxt, PIN_ThreadId(),         \
                              CALLINGSTD_DEFAULT,           \
                              (AFUNPTR)origFnc, NULL,       \
                              PIN_PARG(typeof(res)), &res,  \
                              EXPAND_ARGS(__VA_ARGS__),     \
                              PIN_PARG_END());

#define EXPAND_ARG(arg) PIN_PARG(typeof(arg)), arg
#define EXPAND_ARGS(...) OVR_MACRO(EXPAND, __VA_ARGS__) (EXPAND_ARG, __VA_ARGS__)

#define EXPAND_PARG(arg) PIN_PARG(arg)
#define EXPAND_PARGS(...) OVR_MACRO(EXPAND, __VA_ARGS__) (EXPAND_PARG, __VA_ARGS__)

#define FUNCARG_VALUE(n) IARG_FUNCARG_ENTRYPOINT_VALUE, n
#define N_FUNCARG_VALUES(n)     N_FUNCARG_VALUES_(ZERO_TO_n(n))
#define N_FUNCARG_VALUES_(...)  OVR_MACRO(EXPAND, __VA_ARGS__) (FUNCARG_VALUE, __VA_ARGS__)

// Macro used to replace a libc syscall with a new function
#define REPLACE_SYSCALL(fncName, newFunc, resT, ...)                          \
void doRepl_ ## fncName (IMG img) {                                           \
  RTN rtn = RTN_FindByName(img, #fncName);                                    \
  if (RTN_Valid(rtn)) {                                                       \
    PROTO proto = PROTO_Allocate(PIN_PARG(resT), CALLINGSTD_DEFAULT,          \
                                 #fncName,                                    \
                                 EXPAND_PARGS(__VA_ARGS__),                   \
                                 PIN_PARG_END());                             \
    RTN_ReplaceSignature(rtn, (AFUNPTR)newFunc,                               \
                         IARG_PROTOTYPE, proto,                               \
                         IARG_CONST_CONTEXT,                                  \
                         IARG_ORIG_FUNCPTR,                                   \
                         N_FUNCARG_VALUES(2),                                 \
                         IARG_END);                                           \
    PROTO_Free(proto);                                                        \
  } else {                                                                    \
    fprintf(stderr, "[%d] Didn't find '%s' in libc", PIN_GetPid(), #fncName); \
    PIN_ExitApplication(2);                                                   \
  }                                                                           \
}                                                                             \
                                                                              \
__attribute__((constructor))                                                  \
static void initRepl_ ## fncName() {                                          \
  syscallRepl_registerReplFunc(doRepl_ ## fncName);                           \
}

#endif
