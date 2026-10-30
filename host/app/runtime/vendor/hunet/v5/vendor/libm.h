/* Minimal adaptation layer for the retained, licensed musl float routines. */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>
#define GET_FLOAT_WORD(i,x) do { float _x=(x); memcpy(&(i),&_x,4); } while(0)
#define SET_FLOAT_WORD(x,i) do { uint32_t _i=(i); memcpy(&(x),&_i,4); } while(0)
#define FORCE_EVAL(x) do { volatile float _force=(x); (void)_force; } while(0)
#if FLT_EVAL_METHOD != 0
#error candidate requires float expression evaluation
#endif
