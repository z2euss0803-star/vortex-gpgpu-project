/**********************************************************************/
/*   ____  ____                                                       */
/*  /   /\/   /                                                       */
/* /___/  \  /                                                        */
/* \   \   \/                                                         */
/*  \   \        Copyright (c) 2003-2020 Xilinx, Inc.                 */
/*  /   /        All Right Reserved.                                  */
/* /---/   /\                                                         */
/* \   \  /  \                                                        */
/*  \___\/\___\                                                       */
/**********************************************************************/


/* NOTE: DO NOT EDIT. AUTOMATICALLY GENERATED FILE. CHANGES WILL BE LOST. */

#ifndef DPI_H
#define DPI_H
#ifdef __cplusplus
#define DPI_LINKER_DECL  extern "C" 
#else
#define DPI_LINKER_DECL
#endif

#include "svdpi.h"



/* Imported (by SV) task */
DPI_LINKER_DECL DPI_DLLESPEC int host_entry(
	int* result);


/* Exported (from SV) task*/
DPI_LINKER_DECL int host_bus(
	int operation, uint64_t address, unsigned int value, unsigned int* reply
);


#endif
