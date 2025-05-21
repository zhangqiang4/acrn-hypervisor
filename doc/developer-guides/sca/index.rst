.. _sca:

Static Code Analysis
####################

Static code analysis verifies the ACRN hypervisor code against the enabled coding rules.

.. contents::
   :local:

Justifications for Static Code Analysis Violations
**************************************************

This section summarizes the justifications for static code analysis violations.

The existing violations are classified into three categories, as shown in
:numref:`sca_violation_categories` below.

.. table:: Static code analysis violation categories
   :align: center
   :widths: 20 40 40
   :name: sca_violation_categories

   +--------------------------+------------------------------------------+------------------------------------------------+
   | Category                 | Explanation                              | Follow-up Actions                              |
   +==========================+==========================================+================================================+
   | Justified Deviation      | A violation is to be deviated if it does | Justify the deviation by explaining the        |
   |                          | violate the coding guidelines, but its   | necessity, consequence, and follow-up actions  |
   |                          | necessity and safety can be justified.   | that avoid safety impacts.                     |
   +--------------------------+------------------------------------------+------------------------------------------------+
   | False Positive           | A violation is a false positive of a     | Justify why the implementation cannot behave   |
   |                          | rule if the implementation can never     | in a way that violates the rule.               |
   |                          | behave in a way that violates the rule.  |                                                |
   +--------------------------+------------------------------------------+------------------------------------------------+
   | Overly Aggressive Alarm  | A violation is overaggressive if it is   | Justify why the violation is within the        |
   |                          | in the scope of the rule implemented by  | scope of the Coverity rule but does not hold   |
   |                          | Coverity, but out of the scope of the    | under the ACRN coding guidelines.              |
   |                          | ACRN coding guidelines.                  |                                                |
   +--------------------------+------------------------------------------+------------------------------------------------+


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

The corresponding rules in ACRN Coding Guidelines are:

 - C-PP-02: Function-like MACRO shall be used with restrictions
 - C-EP-21: The comma operator shall not be used
 - C-EP-19: ++ or -- operation shall be used with restrictions
 - C-CU-01: Only one assignment shall be on a single line
 - C-EP-01: The initialization expression of a for loop shall be simple
 - C-EP-02: The controlling expression of a for loop shall not be empty
 - C-EP-03: The third expression of a for loop shall be simple
 - C-ST-07: The for loop counter shall not be changed inside the loop body
 - C-ST-02: The loop body shall be enclosed with brackets

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

The corresponding rules in ACRN Coding Guidelines are:

 - C-PP-02: Function-like MACRO shall be used with restrictions
 - C-EP-21: The comma operator shall not be used
 - C-CU-01: Only one assignment shall be on a single line
 - C-ST-02: The loop body shall be enclosed with brackets

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
the same functionality.

While helpful, it causes Coverity to complain due to the following rule.

 - Rule 20.10	The # and ## preprocessor operators should not be used

The corresponding rule in ACRN Coding Guidelines is:

 - C-PP-01: ## or # operators shall be used with restrictions

It has been reviewed that the ## operator is not used together with the # operator.
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

While helpful, it causes Coverity to complain due to the following rule.

 - Rule 20.10	The # and ## preprocessor operators should not be used

The corresponding rule in ACRN Coding Guidelines is:

 - C-PP-01: ## or # operators shall be used with restrictions

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

While necessary, it causes Coverity to complain due to the following rule.

 - Rule 8.7	Functions and objects should not be defined with external linkage if they are referenced
   in only one translation unit

The corresponding rule in ACRN Coding Guidelines is:

 - C-TY-11: A variable shall be declared as static if it is used only in the file where it is declared

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

The corresponding rule in ACRN Coding Guidelines is:

 - C-EP-23: Pointer arithmetic shall be used with restrictions

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

While necessary, it causes Coverity to complain due to the following rule.

 - Rule 20.7	Expressions resulting from the expansion of macro parameters shall be enclosed in parentheses

The corresponding rules in ACRN Coding Guidelines are:

 - C-EP-05: Parentheses shall be used to set the operator precedence explicitly
 - C-PP-04: Parentheses shall be used when referencing a MACRO parameter

The macro usages and its expansions have been reviewed to avoid side effects. Therefore, those instances
are considered safe and does not compromise the maintainability or correctness of the code.


JD_008: Names starting with underscore
--------------------------------------

ACRN hypervisor defines the following names starting with underscore.

 - __aligned, __packed and __unused which are macros wrapping compiler attributes.
 - __stack_chk_fail which is the interface to the compiler stack protector.

The macros wrapping compiler attributes conventionally start with underscores to reflect its
close relationship to compiler-specific attributes. The name __stack_chk_fail follows the
defined compiler interface.

While necessary, it causes Coverity to complain due to the following rule.

 - Rule 21.1	#define and #undef shall not be used on a reserved identifier or reserved macro name
 - Rule 21.2	A reserved identifier or macro name shall not be declared

The corresponding rule in ACRN Coding Guidelines is:

 - C-ID-07: Names defined by developers shall not start with underscore

The names starting with underscores are known not to be reserved either by C99 or by the
compiler. Thus there is no further consequence due to the non-compliance and no follow-up
actions required.


JD_009: Use of function-like macro
----------------------------------

Function-like macros are used in the following cases.

 - Macro that switches to the new stack and transfers control to the given function.

    - SWITCH_TO

 - Macro that wraps compiler attributes.

    - __aligned

 - Macros that abstract a traversal pattern over a list (expand into a for loop header).

    - hlist_for_each, list_for_each, list_for_each_safe

 - Macros that accept a user-given token as the name of the structure field.

    - get_cpu_var, per_cpu, container_of, get_first_item, hlist_entry, offsetof

 - Macros that accept a user-given token as the name of the variable to be initialized.

    - DEFINE_INTX_SID, DEFINE_MSI_SID

 - Macros that accept a user-given token to be concatenated to a new token.

    - load_segment, save_segment

 - Macros that accept a user-given token to be converted to a string.

    - CPU_CR_READ, CPU_CR_WRITE, CPU_SEG_READ, CPU_XMM_READ, CPU_XMM_WRITE, STRINGIFY

 - Macros that accept polymorphic types.

    - ARRAY_SIZE, clamp, max, min, roundup

 - Macros that are expanded in array sizes, array index, or structure initializers.

    - INT_DIV_ROUNDUP, PD_PAGE_NUM, PDPT_PAGE_NUM, AFFINITY_CPU, HC_IDX, BASE_HC_ID, CTASSERT

 - Macros that generate multiple variants of inline functions for different data types and operand sizes.

    - build_getcc, build_atomic_cmpxchg, build_atomic_dec, build_atomic_inc, build_atomic_swap,
      build_atomic_xadd, build_bitmap_clear, build_bitmap_set, build_bitmap_testandclear,
      build_bitmap_testandset

 - Macros that perform compile-time token concatenation like ``pr_prefix __VA_ARGS__``.

    - dev_dbg, panic, pr_acrnlog, pr_dbg, pr_err, pr_fatal, pr_info, pr_warn

In the use cases above, the function-like macros cannot be rewritten to functions due to limitations of C99.

While necessary, it causes Coverity to complain due to the following rule.

 - Directive 4.9  A function should be used in preference to a function-like macro where they are interchangeable

The corresponding rule in ACRN Coding Guidelines is:

 - C-PP-02: Function-like MACRO shall be used with restrictions

The function-like macros are defined in a pure way, i.e. they have no side effect and only
their expanded value is used. Thus there is no further consequence due to the non-compliance
and no follow-up actions required.


Explanations to False Positives
===============================

FP_001: Return value is not tested
----------------------------------

False alarms reported by Coverity, due to the following rule.

 - Directive 4.7	If a function returns error information, then that error information shall be tested

The corresponding rule in ACRN Coding Guidelines is:

 - C-FN-17: The return value of a non-void function shall be either used or discarded

All the reported violations are classified and justified into below categories:

 - The return value of the function `segment_override` is checked in code to determine if the segment register
   is overrided.


FP_002: Function and variable with external linkage only used in one translation unit
-------------------------------------------------------------------------------------

False alarms reported by Coverity, due to the following rule.

 - Rule 8.7	Functions and objects should not be defined with external linkage if they are referenced
   in only one translation unit

The corresponding rule in ACRN Coding Guidelines is:

 - C-TY-11: A variable shall be declared as static if it is used only in the file where it is declared

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

Coverity warns all the usage of the language extensions (due to the following rule),
which is too restrictive in the context of ACRN.

 - Rule 1.2	Language extensions should not be used

All the reported violations are classified and justified into below categories:

 - Use of `__asm`, refer to ACRN coding guidelines
   `C-LE-01: Use of inline Assembly language in C code is allowed`.
 - Use of `__builtin_va_list`, refer to ACRN coding guidelines
   `C-LE-02: Use of builtin-type __builtin_va_list is allowed`.
 - Use of the attribute `aligned`, `packed`, `section`, and `unused`, refer to ACRN coding guidelines
   `C-LE-03: Use of extended type attributes is allowed`.
 - Use of `__builtin_va_arg`, `__builtin_va_start`, `__builtin_va_end`, and `_builtin_offsetof`,
   refer to ACRN coding guidelines `C-LE-04: Use of extended builtin-function is allowed`.


OAA_002: Use of standard library-like names
-------------------------------------------

As a system software running directly on bare metal, ACRN Hypervisor operates in a freestanding environment
and does not rely on any standard C libraries.
Because of this, ACRN Hypervisor must define its own versions of certain names that are traditionally
provided by the standard library in hosted environments.

It defines the following names that are same as the one in standard library.

 - NULL, true, false, offsetof, va_start, and va_end which are macros wrapping compiler attributes.
 - LONG_MIN which is a macro to represent the maximum positive value a long can hold.
 - bool which is a typedef to represent an Boolean type expected in C code.
 - memset, printf, vprintf, strchr, strcmp, strncmp, snprintf, and vsnprintf which are functions with
   custom implementations to provide basic memory and string manipulation, and formatted output functionality.
 - random, errno, and assert which are variables that the hypervisor uses internally to handle randomness,
   error reporting, and interrupt assert/de-assert, respectively.

Coverity warns such usage due to the following rules.

 - Rule 21.1	#define and #undef shall not be used on a reserved identifier or reserved macro name
 - Rule 21.2	A reserved identifier or macro name shall not be declared

These rules are designed to ensure compatibility and prevent conflicts with the standard library in hosted environments.
However, in the context of ACRN Hypervisor, these warnings are overly restrictive and not applicable, because:

 - ACRN Hypervisor operates without any external standard library, ensuring no conflicts can occur.
 - Re-implementing these functionalities is necessary to provide basic C language support in a bare-metal,
   freestanding environment.

