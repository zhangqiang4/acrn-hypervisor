.. _sca:

Static Code Analysis
####################

Static code analysis verifies the ACRN hypervisor code against the enabled coding rules.

.. contents::
   :local:

Justifications for Static Code Analysis Violations
**************************************************

This section summarizes the justifications for static code analysis violations.

The existing violations are classified into three categories.

 - Justified Deviation
 - False Positives
 - Overly Aggressive Alarms

The following sections provide justifications for each category.


Justifications to Deviations
============================

JD_001: Violations due to the usage of foreach_vcpu
---------------------------------------------------

The macro foreach_vcpu, defined as follows, maintains all the logic to iterate over active vCPUs of a given VM
in its macro definition, in order to make such iteration concise.

.. code-block:: c

	#define foreach_vcpu(idx, vm, vcpu)					\
		for ((idx) = 0U, (vcpu) = &((vm)->hw.vcpu_array[(idx)]);	\
			(idx) < (vm)->hw.created_vcpus;				\
			(idx)++, (vcpu) = &((vm)->hw.vcpu_array[(idx)])) 	\
			if ((vcpu)->state != VCPU_OFFLINE)

The macro is designed to be used in the following way.

.. code-block:: c

	uint16_t i;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;
	foreach_vcpu(i, vm, vcpu) {
		/* Operation on vcpu */
	}

While helpful, the macro causes Coverity to complain due to the following rules.

 - Directive 4.9 A function should be used in preference to a function-like macro where they are interchangeable
 - Rule 12.3	The comma operator should not be used
 - Rule 13.3	A full expression containing an increment (++) or decrement (--) operator should have no other
   potential side effects other than that caused by the increment or decrement operator
 - Rule 13.4	The result of an assignment operator should not be used
 - Rule 14.2	A for loop shall be well-formed
 - Rule 15.6	The body of an iteration-statement or a selection-statement shall be a compound statement

In the version under test foreach_vcpu is used 15 times in the code. As a result, there are 30 violations to Rule 12.3,
15 violations to Rule 13.3, 30 violations to Rule 13.4, 15 violations to Rule 14.2, 15 violations to Rule 15.6,
15 violations to Directive 4.9.

The logic encoded in the macro foreach_vcpu is commonly used throughout the code. Getting rid of the violations will
cause the same piece of code to be duplicated, making it harder to maintain the iterations.

The macro definition has been reviewed not to rely on any non-deterministic or implementation-specific behavior in C99.
There are no potential risks observed due to this specific deviation in the foreach_vcpu macro.


JD_002: Violations due to the usage of list_for_each_safe
---------------------------------------------------------

The macro list_for_each_safe, defined as follows, maintains all the logic to iterate linked list safely in its
macro definition, in order to make such iteration concise.

.. code-block:: c

	#define list_for_each_safe(pos, n, head) 				\
		for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); 	\
			(pos) = (n), (n) = (pos)->next)

The macro is designed to be used in the following way.

.. code-block:: c

	const struct list_head *pos, *n;
	struct list_head *head;
	list_for_each_safe(pos, n, head) {
		/* Operation on pos */
	}

While helpful, the macro causes Coverity to complain due to the following rules.

 - Directive 4.9 A function should be used in preference to a function-like macro where they are interchangeable
 - Rule 12.3	The comma operator should not be used
 - Rule 13.4	The result of an assignment operator should not be used
 - Rule 14.2	A for loop shall be well-formed

In the version under test list_for_each_safe is used once in the code. As a result, there are
2 violations to Rule 12.3, 3 violations to Rule 13.4, 1 violations to Rule 14.2, 1 violations to Directive 4.9.

The logic encoded in the macro list_for_each_safe is a common library-like use case. Though it is only used once
throughout the code under test. But it would be very helpful for future feature extension. So, this macro is kept
to benefit the code scalability in the future.

The macro definition has been reviewed not to rely on any non-deterministic or implementation-specific behavior in C99.
There are no potential risks observed due to this specific deviation in the list_for_each_safe macro.

JD_003: Use of # operator
-------------------------

The # operator is used to convert a user-given token into a string. It is directly used only by the STRINGIFY macro,
which is further used by CPU_SEG_READ, CPU_CR_READ, CPU_CR_WRITE, CPU_XMM_READ, and CPU_XMM_WRITE macros.
Without using # operator, the implementation needs to add a series of functions having similar code for
the same functionality. It has been reviewed that the ## operator is not used together with the # operator.
Thus the behavior is well defined in C99.

JD_004: Use of ## operator
--------------------------

All the reported violations are classified and justified into below categories:

 - Due to save_segment/load_segment.
   The token concatenation operator (##) is used by save_segment/load_segment which saves/loads the VMCS fields
   related to a segment register.
 - Due to `CAT__`.
   The token concatenation operator (##) is used by the `CAT__` macro, which is further used by `CAT_` and CTASSERT.
   The macro CTASSERT performs build-time sanity checks to ensure that hard-coded offsets match the actual offsets
   in the data structure.

   The macro is designed to be used in the following way.

   .. code-block:: c

	#define CAT__(A,B) A ## B
	#define CAT_(A,B) CAT__(A,B)
	#define CTASSERT(expr) \
	typedef int32_t CAT_(CTA_DummyType,__LINE__)[(expr) ? 1 : -1]

	CTASSERT(CPU_CONTEXT_OFFSET_RAX    == offsetof(struct acrn_gp_regs, rax));

The operator is used here to simplify the parameters of the macro.
If not using the ## operator, the saving/loading of such VMCS fields, and the build time sanity checks
for the hard-coded offset can be lengthy, tedious and harder to maintain.
It has been reviewed that the ## operator is not used together with the # operator. Also the
operator is used to concatenate at most 2 parts into a token. Thus the behavior is well defined
in C99.

JD_005: Externally linked function and variable used in a single translation unit
---------------------------------------------------------------------------------

All the reported violations are classified and justified into below categories:

 - Due to the potential usages for debug purpose.
   Some symbols are referenced in only one translation unit in the release build.
   However, in debug builds, they potentially being used for debug purpose.
   Defining them as static in one single file to get rid of the violation in the release build would
   negatively impact the code's scalability.
   Those symbols include: get_max_nr_gsi, do_print, gsi_to_ioapic_base, ioapic_get_rte_entry, irq_alloc_bitmap,
   irq_desc_array, mem_loglevel, ptirq_entries, sbuf_next_ptr, version_info.

 - Due to the functions paired or grouped with other functions.
   In the ACRN Hypervisor, certain functions are defined to manipulate registers on physical or virtual platforms.
   These functions are designed as complementary pairs or groups to ensure a consistent and comprehensive register
   access model.
   One of the function within these pairs or groups may be referenced in only one translation unit.
   However, defining them as static in one single file to get rid of the violation could compromise
   the integrity of the comprehensive register access model.

   Those functions include:

    - send_single_init: grouped with send_single_ipi and send_dest_ipi_mask
    - get_io_req_state: paired with set_io_req_state
    - vcpu_set_rsp: paired with vcpu_get_rsp
    - vcpu_get_cr2: paired with vcpu_set_cr2
    - msr_read_pcpu: paired with msr_write_pcpu

 - Due to the files generated from the configuration data (board.xml and scenario.xml).
   Moving the variable definition/initialization/usage from/to the generated file would blur the boundary between the
   configuration data and other hypervisor components.

    - Variable `vhostbridge_ops` is initialized in `dm/vpci/vhostbridge.c` to define the operation callbacks
      for host bridge virtualization.
      It is referenced in the generated `build/configs/scenarios/mbl-rvp-fusa/pci_dev.c`,
      which determines the devices need to be registered with these callbacks.
    - Variable `vpci_ivshmem_ops` is initialized in `dm/vpci/ivshmem.c` to define the operation callbacks
      for IVSHMEM device implementation.
      It is referenced in the generated `build/configs/scenarios/mbl-rvp-fusa/pci_dev.c`,
      which determines the devices need to be registered with these callbacks.
    - Variable `plat_hidden_pdevs` is referenced in `hw/pci.c` to check the list of the hidden devices.
      It is initialized in the generated `build/configs/scenarios/mbl-rvp-fusa/pci_dev.c`
      since it is determined by the configuration data.
    - Variable `platform_l3_clos_array_0`, `rdt_ins_l3`, and `res_cap_info` are referenced in `hw/pci.c`
      for RDT implementation.
      They are initialized in the generated `build/configs/scenarios/mbl-rvp-fusa/pci_dev.c`
      since it is determined by the configuration data.

JD_006: Violations due to pointer arithmetic beyond the array bound
-------------------------------------------------------------------

Pointer arithmetic is applied on pointers in the following case.

.. code-block:: c

        uint16_t *sel = &(vcpu_regs->cs_sel);
        struct segment_sel *seg;

        for (seg = &(ectx->cs); seg <= &(ectx->gs); seg++) {
                seg->base     = 0UL;
                seg->limit    = limit;
                seg->attr     = attr;
                seg->selector = *sel;
                sel++;
        }

In the above codes, Coverity complains that "Dereferencing seg which points past the end of a singleton." and "Using
sel as an array. This might corrupt or misinterpret adjacent memory locations." The violations are classified into the
following rule.

 - Rule 18.1	A pointer resulting from arithmetic on a pointer operand shall address an element of the same array as
   that pointer operand

The codes logic has been reviewed not to point to different elements or corrupt/misinterpret adjacent memory locations.
This is achieved by the same order of segment selectors defined in the struct ext_context and struct acrn_regs as below.

.. code-block:: c

	struct ext_context {
	...
		struct segment_sel cs;   /**< The guest Code Segment Register */
		struct segment_sel ss;   /**< The guest Stack Segment Register */
		struct segment_sel ds;   /**< The guest Data Segment Register */
		struct segment_sel es;   /**< The guest ES Register */
		struct segment_sel fs;   /**< The guest FS Register */
		struct segment_sel gs;   /**< The guest GS Register */
	...
	};

	struct acrn_regs {
	...
		uint16_t cs_sel;            /**< The value in the guest CS selector */
		uint16_t ss_sel;            /**< The value in the guest SS selector */
		uint16_t ds_sel;            /**< The value in the guest DS selector */
		uint16_t es_sel;            /**< The value in the guest ES selector */
		uint16_t fs_sel;            /**< The value in the guest FS selector */
		uint16_t gs_sel;            /**< The value in the guest GS selector */
	...
	};

There are no potential risks observed due to this specific deviation.

JD_007: Macro parameters are safely used without parentheses in macro expansion
-------------------------------------------------------------------------------

All the reported violations are due to below macros:

 - build_bitmap_set
 - build_bitmap_clear
 - build_bitmap_testandset
 - build_bitmap_testandclear

Those macros are designed to generate function definitions with specific types and formats,
to be used like below.

.. code-block:: c

	#define build_bitmap_set(name, op_len, op_type, lock)			\
	static inline void name(uint16_t nr_arg, volatile op_type *addr)	\
	{									\
		uint16_t nr;							\
		nr = nr_arg & ((8U * sizeof(op_type)) - 1U);			\
		asm volatile(lock "or" op_len " %1,%0"				\
				:  "+m" (*addr)					\
				:  "r" ((op_type)(1UL<<nr))			\
				:  "cc", "memory");				\
	}

	build_bitmap_set(bitmap_set_nolock, "q", uint64_t, "")

The parameters passed (e.g., bitmap_set_nolock, "q", uint64_t, and "") are names, constants or type tokens
and not expected to be arithmetic expressions. Wrapping these in parentheses would not provide additional
safety and could actually lead to incorrect syntax when token pasting or stringification is involved.

The macro usages and its expansions have been reviewed to avoid side effects. Therefore, those instances
are considered safe and does not compromise the maintainability or correctness of the code.

Explanations to False Positives
===============================

FP_001: Return value is not tested
----------------------------------

False alarms reported by Coverity.
All the reported violations are classified and justified into below categories:

 - The return value of the function `segment_override` is checked to determine if the segment register is overrided.


FP_002: Function and variable with external linkage only used in one translation unit
-------------------------------------------------------------------------------------

False alarms reported by Coverity.
All the reported violations are classified and justified into below categories:

 - Function `__stack_chk_fail` is the interface to the compiler stack protector.
   It is defined in `lib/stack_protector.c` and referenced in GCC link process.
 - Due to the assembly code, which cannot be precisely analyzed by Coverity.

    - Function `dispatch_exception` is used in `arch/x86/exception.c` and `arch/x86/idt.S`.
    - Function `do_acpi_sx` is used in `arch/x86/pm.c` and `arch/x86/wakeup.S`.
    - Function `handle_nmi` is used in `arch/x86/nmi.c` and `arch/x86/wakeup.S`.
    - Function `init_primary_pcpu` is used in `arch/x86/init.c` and `arch/x86/boot/cpu_primary.S`.
    - Function `init_secondary_pcpu` is used in `arch/x86/init.c` and `arch/x86/boot/trampoline.S`.
    - Function `relocate` is used in `boot/reloc.c` and `arch/x86/boot/cpu_primary.S`.
    - Function `restore_msrs` is used in `arch/x86/pm.c` and `arch/x86/wakeup.S`.
    - Variable `cpu_ctx` is used in `arch/x86/pm.c` and `arch/x86/wakeup.S`.

Explanations to Overly Aggressive Alarms
========================================

OAA_001: Use of allowed language extensions
-------------------------------------------

The ACRN coding guidelines define a limited set of allowed language extensions for the project,
to ensure the limited and documented-only use of compiler-specific features.
Coverity warns all the usage of the language extensions, which is too restrictive in the context of ACRN.

All the reported violations are classified and justified into below categories:

 - Use of `__asm`, refer to ACRN coding guidelines
   `C-LE-01: Use of inline Assembly language in C code is allowed`.
 - Use of `__builtin_va_list`, refer to ACRN coding guidelines
   `C-LE-02: Use of builtin-type __builtin_va_list is allowed`.
 - Use of the attribute `aligned`, `packed`, `section`, and `unused`, refer to ACRN coding guidelines
   `C-LE-03: Use of extended type attributes is allowed`.
 - Use of `__builtin_va_arg`, `__builtin_va_start`, `__builtin_va_end`, and `_builtin_offsetof`,
   refer to ACRN coding guidelines `C-LE-04: Use of extended builtin-function is allowed`.

