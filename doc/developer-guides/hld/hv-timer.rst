.. _timer-hld:

Timer
#####

Because ACRN is a flexible, lightweight reference hypervisor, we provide
limited timer management services:

- Only the LAPIC tsc-deadline timer is supported as the clock source.

- A timer can only be added on the logical CPU for a process or thread. Timer
  scheduling or timer migrating is not supported.

How It Works
************

When the system boots, we check that the hardware supports the LAPIC
tsc-deadline timer by checking CPUID.01H:ECX.TSC_Deadline[bit 24]. If
support is missing, we output an error message and panic the hypervisor.
If supported, we register the timer interrupt callback that raises a
timer softirq on each logical CPU and sets the LAPIC timer mode to
tsc-deadline timer mode by writing the local APIC LVT register.

Data Structures and APIs
************************

Interfaces Design
=================

Refer to the module design specification for more details.

.. doxygenfunction:: initialize_timer
   :project: Project ACRN

.. doxygenfunction:: timer_expired
   :project: Project ACRN

.. doxygenfunction:: timer_is_started
   :project: Project ACRN

.. doxygenfunction:: add_timer
   :project: Project ACRN

.. doxygenfunction:: del_timer
   :project: Project ACRN

.. doxygenfunction:: timer_init
   :project: Project ACRN

:c:func:`calibrate_tsc`

:c:func:`cpu_ticks`

:c:func:`cpu_tickrate`

:c:func:`us_to_ticks`

:c:func:`ticks_to_ms`

:c:func:`ticks_to_us`

:c:func:`udelay`

