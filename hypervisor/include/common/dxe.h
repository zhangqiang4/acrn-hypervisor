/*-
* Copyright (c) 2023 Intel Corporation.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*
* $FreeBSD$
*/

#ifndef DXE_H_
#define DXE_H_

#include <asm/cpu.h>
#include <asm/io.h>
#include <types.h>

extern uint64_t get_mmcfg_base(uint16_t bdf);
extern int32_t register_diagnostics_on_msi(uint16_t bdf,
		int32_t (*cb)(void *data), void *data);
extern int32_t unregister_diagnostics_on_msi(uint16_t bdf);
extern int32_t register_diagnostics_on_msix(uint16_t bdf, uint32_t vector,
		int32_t (*cb)(void *data), void *data);
extern int32_t unregister_diagnostics_on_msix(uint16_t bdf, uint32_t vector);


/* external diagnostics provided APIs */

#ifndef DIAGNOSTICS_INIT_DATA_SIZE
#define DIAGNOSTICS_INIT_DATA_SIZE 0
#endif

#ifndef DIAGNOSTICS_DIAG_DATA_SIZE
#define DIAGNOSTICS_DIAG_DATA_SIZE 0
#endif

extern int32_t initialize_diagnostics(uint64_t routine_mask, void *data);
extern int32_t run_diagnostics(uint64_t routine_mask, void *data);

#endif /* DXE_H_ */
