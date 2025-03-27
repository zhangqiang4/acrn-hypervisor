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

JD_003: Use of ## operator
--------------------------

The # operator is used to convert a user-given token into a string. It is directly used only by the STRINGIFY macro,
which is further used by CPU_SEG_READ, CPU_CR_READ, CPU_CR_WRITE, CPU_XMM_READ, and CPU_XMM_WRITE macros.
Without using # operator, the implementation needs to add a series of functions having similar code for
the same functionality. It has been reviewed that the ## operator is not used together with the # operator.
Thus the behavior is well defined in C99.

JD_004: Use of # operator
-------------------------

JD_005: Externally linked function and variable used in a single translation unit
---------------------------------------------------------------------------------

All the reported violations are classified and justified into below categories:

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

