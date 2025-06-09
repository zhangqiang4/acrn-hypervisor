.. _config_data:

Design of Component `config-data`
#################################

This section contains the documentation of the component `config-data`.

While below C files are part of the Hypervisor source code, additional details
on the configuration data are specified in the
Software Configuration Data Specification and Software Configuration Data documents.
These documents aim to ensure the correct usage of configurable software, including:

a. the valid values of the configuration data;
b. the intent and usage of the configuration data;
c. the range, scaling, units; and
d. the interdependencies between different elements of the configuration data.

.. doxygengroup:: config-data
   :project: Project ACRN
   :desc-only:

.. contents::
   :local:


[File] include/arch/x86/asm/board.h
======================================================================

.. doxygenfile:: asm/board.h
   :project: Project ACRN
