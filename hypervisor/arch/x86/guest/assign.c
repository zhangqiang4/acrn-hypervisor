/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/assign.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include <asm/guest/vm.h>
#include <asm/vtd.h>
#include <asm/per_cpu.h>
#include <asm/ioapic.h>
#include <asm/pgtable.h>
#include <asm/irq.h>

/**
 * @addtogroup vp-dm_ptirq vp-dm.ptirq
 *
 * @{
 */

/**
 * @file
 *
 * @brief Implementations of passthrough device interrupt assignment
 */

/**
 * @brief Check if the IRQ can be posted to a single vCPU and return the destination vCPU
 *
 * VT-d PI (posted mode) cannot support multicast/broadcast IRQs.
 * If returns NULL, this means it is multicast/broadcast IRQ and
 * we can only handle it in remapped mode.
 * If returns non-NULL, the destination vCPU is returned, which means it is
 * single-destination IRQ and we can handle it in posted mode.
 *
 * @param[in] vm The VM to receive the MSI
 * @param[in] info MSI address and data information
 *
 * @return The exact vCPU to received the MSI for a posted single-destination MSI or NULL
 *
 * @pre (vm != NULL) && (info != NULL)
 */
static struct acrn_vcpu *is_single_destination(struct acrn_vm *vm, const struct msi_info *info)
{
	uint64_t vdmask;
	uint16_t vid;
	struct acrn_vcpu *vcpu = NULL;

	vdmask = vlapic_calc_dest_noshort(vm, false, (uint32_t)(info->addr.bits.dest_field),
		(bool)(info->addr.bits.dest_mode == MSI_ADDR_DESTMODE_PHYS),
		(bool)(info->data.bits.delivery_mode == MSI_DATA_DELMODE_LOPRI));

	vid = ffs64(vdmask);

	/* Can only post fixed and Lowpri IRQs */
	if ((info->data.bits.delivery_mode == MSI_DATA_DELMODE_FIXED)
		|| (info->data.bits.delivery_mode == MSI_DATA_DELMODE_LOPRI)) {
		/* Can only post single-destination IRQs */
		if (vdmask == (1UL << vid)) {
			vcpu = vcpu_from_vid(vm, vid);
		}
	}

	return vcpu;
}

/**
 * @brief Sanitize physical CPU destination mask
 *
 * The physical CPU mask translated from vCPU mask may not be in the same x2APIC cluster and
 * interrupts can not be distributed to all of those physical CPUs. Instead, we choose the first
 * one of target clusters as the real destination. If pdmask is 0, CPU with logical id 0 in
 * cluster 0 is chosen as destination.
 *
 * @param[in] pdmask Original physical CPU destination mask
 *
 * @return Sanitized physical CPU destination mask
 */
static uint32_t calculate_logical_dest_mask(uint64_t pdmask)
{
	uint32_t dest_cluster_id = 0U, cluster_id, logical_id_mask = 0U;
	uint64_t pcpu_mask = pdmask;
	uint16_t pcpu_id;

	pcpu_id = ffs64(pcpu_mask);
	if (pcpu_id != INVALID_BIT_INDEX) {
		/* Guests working in xAPIC mode may use 'Flat Model' to select an
		 * arbitrary list of CPUs. But as the HW is working in x2APIC mode and can only
		 * use 'Cluster Model', destination mask can only be assigned to pCPUs within
		 * one Cluster. So some pCPUs may not be included.
		 * Here we use the first Cluster of all the requested pCPUs.
		 */
		dest_cluster_id = per_cpu(lapic_ldr, pcpu_id) & X2APIC_LDR_CLUSTER_ID_MASK;
		do {
			bitmap_clear_nolock(pcpu_id, &pcpu_mask);
			cluster_id = per_cpu(lapic_ldr, pcpu_id) & X2APIC_LDR_CLUSTER_ID_MASK;
			if (cluster_id == dest_cluster_id) {
				logical_id_mask |= (per_cpu(lapic_ldr, pcpu_id) & X2APIC_LDR_LOGICAL_ID_MASK);
			} else {
				pr_warn("The cluster ID of pCPU %d is %d which differs from that (%d) of "
					"the previous cores in the guest logical destination.\n"
					"Ignore that pCPU in the logical destination for physical interrupts.",
					pcpu_id, cluster_id >> 16U, dest_cluster_id >> 16U);
			}
			pcpu_id = ffs64(pcpu_mask);
		} while (pcpu_id != INVALID_BIT_INDEX);
	}

	return (dest_cluster_id | logical_id_mask);
}

/**
 * @brief Free IRTE entry associated with a ptirq_remapping_info
 *
 * It was allocated when building the IRTE for physical IOAPIC RTE or MSI/MSI-x registers.
 *
 * @param[in] entry ptirq_remapping_info for a remapped interrupt
 *
 * @return None
 *
 * @pre entry != NULL
 */
static void ptirq_free_irte(const struct ptirq_remapping_info *entry)
{
	struct intr_source intr_src;

	if (entry->intr_type == PTDEV_INTR_MSI) {
		intr_src.is_msi = true;
		intr_src.src.msi.value = entry->phys_sid.msi_id.bdf;
	} else {
		intr_src.is_msi = false;
		intr_src.src.ioapic_id = ioapic_irq_to_ioapic_id(entry->allocated_pirq);
	}
	dmar_free_irte(&intr_src, entry->irte_idx);
}

/**
 * @brief Build physical MSI address and data for a remapped MSI/MSI-x interrupt
 *
 * - Calculate destination vCPU mask and corresponding pCPU mask according to virtual MSI address
 *   register
 * - Set DMAR Interrupt Remapping Table Entry (IRTE) to posted mode or remapped mode according to
 *   pid_paddr and update the IRTE index in physical MSI address which will be written to MSI
 *   address register later.
 * - For device without IOMMU or there is no available IRTE entry, the physical MSI address
 *   and data fall back to compatibility mode values which goes the trap and inject path.
 *
 * @param[in] vm VM the interrupt is remapped to
 * @param[inout] entry The ptirq_remapping_info for this interrupt
 * @param[in] vector Virtual vector in virtual MSI data register for posted interrupt or physical
 *      vector for directly remapped interrupt.
 * @param[in] pid_paddr 0 means on PID available, thus using remapped mode. Otherwise, this is
 *      physical address of a PID for posted interrupt.
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre entry != NULL
 */
static void ptirq_build_physical_msi(struct acrn_vm *vm,
	struct ptirq_remapping_info *entry, uint32_t vector, uint64_t pid_paddr)
{
	uint64_t vdmask, pdmask;
	uint32_t dest, delmode, dest_mask;
	bool phys;
	union dmar_ir_entry irte;
	union irte_index ir_index;
	int32_t ret;
	struct intr_source intr_src;

	/* get physical destination cpu mask */
	dest = entry->vmsi.addr.bits.dest_field;
	phys = (entry->vmsi.addr.bits.dest_mode == MSI_ADDR_DESTMODE_PHYS);

	vdmask = vlapic_calc_dest_noshort(vm, false, dest, phys, false);
	pdmask = vcpumask2pcpumask(vm, vdmask);

	/* get physical delivery mode */
	delmode = entry->vmsi.data.bits.delivery_mode;
	if ((delmode != MSI_DATA_DELMODE_FIXED) && (delmode != MSI_DATA_DELMODE_LOPRI)) {
		delmode = MSI_DATA_DELMODE_LOPRI;
	}

	dest_mask = calculate_logical_dest_mask(pdmask);

	/* Using phys_irq as index in the corresponding IOMMU */
	irte.value.lo_64 = 0UL;
	irte.value.hi_64 = 0UL;
	irte.bits.remap.vector = vector;
	irte.bits.remap.delivery_mode = delmode;
	irte.bits.remap.dest_mode = MSI_ADDR_DESTMODE_LOGICAL;
	irte.bits.remap.rh = MSI_ADDR_RH;
	irte.bits.remap.dest = dest_mask;

	intr_src.is_msi = true;
	intr_src.pid_paddr = pid_paddr;
	intr_src.src.msi.value = entry->phys_sid.msi_id.bdf;
	ret = dmar_assign_irte(&intr_src, &irte, entry->irte_idx, &ir_index.index);

	if (ret == 0) {
		entry->pmsi.data.full = 0U;
		entry->pmsi.addr.full = 0UL;
		entry->irte_idx = ir_index.index;
		if (ir_index.index != INVALID_IRTE_ID) {
			/*
			 * Update the MSI interrupt source to point to the IRTE
			 * SHV is set to 0 as ACRN disables MMC (Multi-Message Capable
			 * for MSI devices.
			 */
			entry->pmsi.addr.ir_bits.intr_index_high = ir_index.bits.index_high;
			entry->pmsi.addr.ir_bits.shv = 0U;
			entry->pmsi.addr.ir_bits.intr_format = 0x1U;
			entry->pmsi.addr.ir_bits.intr_index_low = ir_index.bits.index_low;
			entry->pmsi.addr.ir_bits.constant = 0xFEEU;
		}
	} else {
		/* In case there is no corresponding IOMMU, for example, if the
		 * IOMMU is ignored, pass the MSI info in Compatibility Format
		 */
		entry->pmsi.data = entry->vmsi.data;
		entry->pmsi.data.bits.delivery_mode = delmode;
		entry->pmsi.data.bits.vector = vector;

		entry->pmsi.addr = entry->vmsi.addr;
		entry->pmsi.addr.bits.dest_field = dest_mask;
		entry->pmsi.addr.bits.rh = MSI_ADDR_RH;
		entry->pmsi.addr.bits.dest_mode = MSI_ADDR_DESTMODE_LOGICAL;
	}
	dev_dbg(DBG_LEVEL_IRQ, "MSI %s addr:data = 0x%lx:%x(V) -> 0x%lx:%x(P)",
		(entry->pmsi.addr.ir_bits.intr_format != 0U) ? " Remappable Format" : "Compatibility Format",
		entry->vmsi.addr.full, entry->vmsi.data.full,
		entry->pmsi.addr.full, entry->pmsi.data.full);
}

/**
 * @brief Build physical IOAPIC RTE for a remapped interrupt according to vRTE in vIOAPIC
 *
 * - Get the vRTE from vGSI and vIOAPIC
 * - Set initial vIOAPIC pin state to inactive per polarity in vRTE
 * - Calculate destination vCPU mask and corresponding pCPU mask from vRTE
 * - Set DMAR Interrupt Remapping Table Entry (IRTE) and update the IRTE index in physical IOAPIC
 *   RTE. INTx interrupts always goes interrupt remapping instead of posted interrupt. The
 *   remapped vector matches the physical vector. If no available IRET entry, the interrupt is
 *   just masked.
 *   For IOAPIC without IOMMU or there is no available IRTE entry, just use unmapped format RTE
 *   to route interrupt to hypervisor and let it inject interrupt to the VM.
 *
 * @param[in] vm VM to which the interrupt is remapped
 * @param[in] entry The ptirq_remapping_info to describe the remapped interrupt
 *
 * @return A physical IOAPIC RTE to be set
 *
 * @pre vm != NULL
 * @pre entry != NULL
 */
static union ioapic_rte
ptirq_build_physical_rte(struct acrn_vm *vm, struct ptirq_remapping_info *entry)
{
	union ioapic_rte rte;
	uint32_t phys_irq = entry->allocated_pirq;
	union source_id *virt_sid = &entry->virt_sid;
	union irte_index ir_index;
	union dmar_ir_entry irte;
	struct intr_source intr_src;
	int32_t ret;
	uint64_t vdmask, pdmask;
	uint32_t dest, delmode, dest_mask, vector;
	union ioapic_rte virt_rte;
	bool phys;

	vioapic_get_rte(vm, virt_sid->intx_id.gsi, &virt_rte);
	rte = virt_rte;

	/* init polarity & pin state */
	if (rte.bits.intr_polarity == IOAPIC_RTE_INTPOL_ALO) {
		if (entry->polarity == 0U) {
			vioapic_set_irqline_nolock(vm, virt_sid->intx_id.gsi, GSI_SET_HIGH);
		}
		entry->polarity = 1U;
	} else {
		if (entry->polarity == 1U) {
			vioapic_set_irqline_nolock(vm, virt_sid->intx_id.gsi, GSI_SET_LOW);
		}
		entry->polarity = 0U;
	}

	/* physical destination cpu mask */
	phys = (virt_rte.bits.dest_mode == IOAPIC_RTE_DESTMODE_PHY);
	dest = (uint32_t)virt_rte.bits.dest_field;
	vdmask = vlapic_calc_dest_noshort(vm, false, dest, phys, false);
	pdmask = vcpumask2pcpumask(vm, vdmask);

	/* physical delivery mode */
	delmode = virt_rte.bits.delivery_mode;
	if ((delmode != IOAPIC_RTE_DELMODE_FIXED) &&
		(delmode != IOAPIC_RTE_DELMODE_LOPRI)) {
		delmode = IOAPIC_RTE_DELMODE_LOPRI;
	}

	/* update physical delivery mode, dest mode(logical) & vector */
	if (is_lapic_pt_configured(vm)) {
		/*
		 * In local APIC passthrough case, when devices triggered a INTx interrupt, this interrupt
		 * would be delivered to vCPU directly. For this case, need to set the virtual vector in
		 * the 'Interrupt Vector' field of physical IOxAPIC I/O REDIRECTION TABLE REGISTER (bits 7:0)
		 * and 'Vector' field of vt-d Interrupt Remapping Table Entry (IRTE) for Remapped Interrupts.
		 *
		 * Assumption:
		 * (a) IOAPIC pins won't be shared between LAPIC PT guest and other guests;
		 * (b) The guest would not trigger this IRQ before it switched to x2 APIC mode.
		 */
		vector = rte.bits.vector;
	} else {
		vector = irq_to_vector(phys_irq);
	}

	dest_mask = calculate_logical_dest_mask(pdmask);

	irte.value.lo_64 = 0UL;
	irte.value.hi_64 = 0UL;
	irte.bits.remap.vector = vector;
	irte.bits.remap.delivery_mode = delmode;
	irte.bits.remap.dest_mode = IOAPIC_RTE_DESTMODE_LOGICAL;
	irte.bits.remap.dest = dest_mask;
	irte.bits.remap.trigger_mode = rte.bits.trigger_mode;

	intr_src.is_msi = false;
	intr_src.pid_paddr = 0UL;
	intr_src.src.ioapic_id = ioapic_irq_to_ioapic_id(phys_irq);
	ret = dmar_assign_irte(&intr_src, &irte, entry->irte_idx, &ir_index.index);

	if (ret == 0) {
		entry->irte_idx = ir_index.index;
		if (ir_index.index != INVALID_IRTE_ID) {
			rte.ir_bits.vector = vector;
			rte.ir_bits.constant = 0U;
			rte.ir_bits.intr_index_high = ir_index.bits.index_high;
			rte.ir_bits.intr_format = 1U;
			rte.ir_bits.intr_index_low = ir_index.bits.index_low;
		} else {
			rte.bits.intr_mask = 1;
		}
	} else {
		rte.bits.dest_mode = IOAPIC_RTE_DESTMODE_LOGICAL;
		rte.bits.delivery_mode = delmode;
		rte.bits.vector = vector;
		rte.bits.dest_field = dest_mask;
	}

	dev_dbg(DBG_LEVEL_IRQ, "IOAPIC RTE %s = 0x%x:%x(V) -> 0x%x:%x(P)",
		(rte.ir_bits.intr_format != 0U) ? "Remappable Format" : "Compatibility Format",
		virt_rte.u.hi_32, virt_rte.u.lo_32,
		rte.u.hi_32, rte.u.lo_32);

	return rte;
}

/**
 * @brief Remap a MSI/MSI-x interrupt to a VM
 *
 * - If the physical interrupt have not been remapped, create a new ptirq remapping info entry for
 *   it and activate the entry. If no free entries available, return NULL.
 * - Otherwise return the existing entry.
 *
 * @param[in] vm VM to add the MSI/MSI-x interrupt remapping
 * @param[in] virt_bdf PCI bdf for virtual device representation within vm
 * @param[in] phys_bdf PCI bdf for the physical device
 * @param[in] entry_nr Vector index for the MSI/MSI-x interrupt
 *
 * @return A pointer to the created ptirq_remapping_info entry or NULL
 * @retval pointer_to_created_entry When the remap is added successfully.
 * @retval NULL No free ptirq_remapping_info entry or activation failed
 *
 * @pre vm != NULL
 */
static struct ptirq_remapping_info *add_msix_remapping(struct acrn_vm *vm,
	uint16_t virt_bdf, uint16_t phys_bdf, uint32_t entry_nr)
{
	struct ptirq_remapping_info *entry;
	DEFINE_MSI_SID(phys_sid, phys_bdf, entry_nr);
	DEFINE_MSI_SID(virt_sid, virt_bdf, entry_nr);

	entry = find_ptirq_entry(PTDEV_INTR_MSI, &phys_sid, NULL);
	if (entry == NULL) {
		entry = ptirq_alloc_entry(vm, PTDEV_INTR_MSI);
		if (entry != NULL) {
			entry->phys_sid.value = phys_sid.value;
			entry->virt_sid.value = virt_sid.value;
			entry->release_cb = ptirq_free_irte;

			/* update msi source and active entry */
			if (ptirq_activate_entry(entry, IRQ_INVALID) < 0) {
				ptirq_release_entry(entry);
				entry = NULL;
			}
		}

		dev_dbg(DBG_LEVEL_IRQ, "VM%d MSIX add vector mapping vbdf%x:pbdf%x idx=%d",
			vm->vm_id, virt_bdf, phys_bdf, entry_nr);
	}

	return entry;
}

/**
 * @brief Deactivate and remove mapping entry for given MSI/MSI-x of a PCI device
 *
 * If such a mapping entry exists and it's for the given vm, deactivate this mapping, free DMAR
 * IRTE and release ptirq_remapping_info entry.
 *
 * @param[in] vm VM to remove the MSI/MSI-x interrupt remapping
 * @param[in] phys_bdf PCI bdf of the physical device
 * @param[in] entry_nr MSI/MSI-x interrupt vector index
 *
 * @return None
 *
 * @pre vm != NULL
 */
static void
remove_msix_remapping(const struct acrn_vm *vm, uint16_t phys_bdf, uint32_t entry_nr)
{
	struct ptirq_remapping_info *entry;
	DEFINE_MSI_SID(phys_sid, phys_bdf, entry_nr);
	struct intr_source intr_src;

	entry = find_ptirq_entry(PTDEV_INTR_MSI, &phys_sid, NULL);
	if ((entry != NULL) && (entry->vm == vm)) {
		if (is_entry_active(entry)) {
			/*TODO: disable MSIX device when HV can in future */
			ptirq_deactivate_entry(entry);
		}

		intr_src.is_msi = true;
		intr_src.src.msi.value = entry->phys_sid.msi_id.bdf;
		dmar_free_irte(&intr_src, entry->irte_idx);

		dev_dbg(DBG_LEVEL_IRQ, "VM%d MSIX remove vector mapping vbdf-pbdf:0x%x-0x%x idx=%d",
			vm->vm_id, entry->virt_sid.msi_id.bdf, phys_bdf, entry_nr);

		ptirq_release_entry(entry);
	}

}

/**
 * @brief add intx entry for a vm, based on intx id (phys_pin)
 *
 * - If both the physical interrupt and the virtual interrupt of vm are not remapped, allocate a
 *   ptirq_remapping_info and activate it.
 * - If the physical interrupt has been remapped to other VM, return NULL.
 * - If the physical interrupt has been remapped to the VM, just return existing entry
 *
 * @param[in] vm Pointer to VM for which the INTx interrupt is remapped
 * @param[in] virt_gsi Virtual GSI number in VM vIOAPIC context
 * @param[in] phys_gsi Physical GSI number of the interrupt
 *
 * @return A pointer to the created ptirq_remapping_info entry or NULL
 * @retval NULL No free ptirq entries for new remap or the physical interrupt has been remapped to
 *         another VM or activation failed
 * @retval pointer_to_added_entry When the INTx interrupt is remapped successfully.
 *
 * @pre vm != NULL
 */
static struct ptirq_remapping_info *add_intx_remapping(struct acrn_vm *vm, uint32_t virt_gsi,
		uint32_t phys_gsi)
{
	struct ptirq_remapping_info *entry = NULL;
	DEFINE_INTX_SID(phys_sid, phys_gsi);
	DEFINE_INTX_SID(virt_sid, virt_gsi);
	uint32_t phys_irq = ioapic_gsi_to_irq(phys_gsi);

	entry = find_ptirq_entry(PTDEV_INTR_INTX, &phys_sid, NULL);
	if (entry == NULL) {
		if (find_ptirq_entry(PTDEV_INTR_INTX, &virt_sid, vm) == NULL) {
			entry = ptirq_alloc_entry(vm, PTDEV_INTR_INTX);
			if (entry != NULL) {
				entry->phys_sid.value = phys_sid.value;
				entry->virt_sid.value = virt_sid.value;
				entry->release_cb = ptirq_free_irte;

				/* activate entry */
				if (ptirq_activate_entry(entry, phys_irq) < 0) {
					ptirq_release_entry(entry);
					entry = NULL;
				}
			}
		} else {
			pr_err("INTX re-add vpin %d", virt_gsi);
		}
	} else if (entry->vm != vm) {
		pr_err("INTX gsi%d already in vm%d with vgsi%d, not able to add into vm%d with vgsi%d",
				phys_gsi, entry->vm->vm_id, entry->virt_sid.intx_id.gsi, vm->vm_id, virt_gsi);
		entry = NULL;
	} else {
		/* The mapping has already been added to the VM. No action
		 * required.
		 */
	}


	/*
	 * ptirq entry is either created or transferred from Service VM to Post-launched VM
	 */

	if (entry != NULL) {
		dev_dbg(DBG_LEVEL_IRQ, "VM%d INTX add pin mapping vgsi%d:pgsi%d",
			entry->vm->vm_id, virt_gsi, phys_gsi);
	}

	return entry;
}

/**
 * @brief Deactivate and remove an INTx remapping
 *
 * The INTx remapping can be determined by a physical GSI or a virtual GSI in a VM.
 * If the remapping exists, deactivate it first in following steps:
 * - Mask the physical IOAPIC interrupt
 * - Deactivate the ptirq entry by unregister the irq handler
 * - Free DMAR interrupt remapping table entry whose index is recorded in the remapping info
 * After that, free the ptirq remapping info entry.
 *
 * @param[in] vm VM from which the remapping is removed
 * @param[in] gsi A virtual GSI or physical GSI depending on is_phy_gsi
 * @param[in] is_phy_gsi True if gsi is for a physical interrupt, False if virtual interrupt
 *            within the vm
 *
 * @return None
 *
 * @pre vm != NULL
 */
static void remove_intx_remapping(const struct acrn_vm *vm, uint32_t gsi, bool is_phy_gsi)
{
	uint32_t phys_irq;
	struct ptirq_remapping_info *entry;
	struct intr_source intr_src;

	if (is_phy_gsi) {
		DEFINE_INTX_SID(sid, gsi);
		entry = find_ptirq_entry(PTDEV_INTR_INTX, &sid, NULL);
		if ((entry != NULL) && (entry->vm != vm)) {
			entry = NULL;
		}
	} else {
		DEFINE_INTX_SID(sid, gsi);
		entry = find_ptirq_entry(PTDEV_INTR_INTX, &sid, vm);
	}

	if (entry != NULL) {
		if (is_entry_active(entry)) {
			phys_irq = entry->allocated_pirq;
			/* disable interrupt */
			ioapic_gsi_mask_irq(phys_irq);

			ptirq_deactivate_entry(entry);
			intr_src.is_msi = false;
			intr_src.src.ioapic_id = ioapic_irq_to_ioapic_id(phys_irq);

			dmar_free_irte(&intr_src, entry->irte_idx);
			dev_dbg(DBG_LEVEL_IRQ,
				"deactivate vIOAPIC intx entry:pgsi=%d, pirq=%d ",
				entry->phys_sid.intx_id.gsi, phys_irq);
			dev_dbg(DBG_LEVEL_IRQ, "from vm%d vgsi=%d\n",
				entry->vm->vm_id, entry->virt_sid.intx_id.gsi);
		}

		ptirq_release_entry(entry);
	}
}

/**
 * @brief Handle INTx injection for a passthrough device
 *
 * INTx interrupts are not posted and routed to hypervisor by interrupt remapping or unmapped
 * IOAPIC RTE. The interrupt handler finally reach here to update vIOAPIC pin and inject interrupts
 * if virtual interrupt are not masked.
 *
 * @param[in] vm VM to receive the interrupt
 * @param[in] entry ptirq_remapping_info for the remapped interrupt
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre entry != NULL
 */
static void ptirq_handle_intx(struct acrn_vm *vm,
		const struct ptirq_remapping_info *entry)
{
	const union source_id *virt_sid = &entry->virt_sid;

	/* Assert interrupt */
	vioapic_assert_irq_lock(vm, virt_sid->intx_id.gsi);

	dev_dbg(DBG_LEVEL_PTIRQ,
		"dev-assign: irq=0x%x assert vr: 0x%x",
		entry->allocated_pirq,
		irq_to_vector(entry->allocated_pirq));
}

/**
 * @brief Handler of softirq to remap interrupts
 *
 * When hypervisor receive a physical interrupt from passthrough device, it
 * will enqueue a ptirq_remapping_info entry and raise softirq SOFTIRQ_PTDEV. This function
 * is the handler of the softirq. If the ptirq_remapping_info entry is active, it handles the
 * interrupt and injects the virtual interrupt into VM.
 * The handler is registered by calling @ref ptdev_init during hypervisor
 * initialization.
 *
 * @param[in]    pcpu_id physical cpu id of the soft irq
 *
 * @return None
 */
void ptirq_softirq(uint16_t pcpu_id)
{
	while (true) {
		struct ptirq_remapping_info *entry = ptirq_dequeue_softirq(pcpu_id);
		struct msi_info *vmsi;

		if (entry == NULL) {
			break;
		}

		vmsi = &entry->vmsi;

		/* skip any inactive entry */
		if (!is_entry_active(entry)) {
			/* service next item */
			continue;
		}

		/* handle real request */
		if (entry->intr_type == PTDEV_INTR_INTX) {
			ptirq_handle_intx(entry->vm, entry);
		} else {
			if (vmsi != NULL) {
				/* TODO: vmsi destmode check required */
				(void)vlapic_inject_msi(entry->vm, vmsi->addr.full, vmsi->data.full);
				dev_dbg(DBG_LEVEL_PTIRQ, "dev-assign: irq=0x%x MSI VR: 0x%x-0x%x",
					entry->allocated_pirq, vmsi->data.bits.vector,
					irq_to_vector(entry->allocated_pirq));
				dev_dbg(DBG_LEVEL_PTIRQ, " vmsi_addr: 0x%lx vmsi_data: 0x%x",
					vmsi->addr.full, vmsi->data.full);
			}
		}
	}
}

/**
 * @brief Acknowledge a virtual INTx interrupt for passthrough device.
 *
 * If the virtual interrupt is remapped, update the line state in vIOAPIC according to interrupt
 * polarity and then unmask the physical interrupt.
 *
 * @param[in] vm The VM the virt_gsi is from
 * @param[in] virt_gsi virtual GSI number associated with the passthrough device
 *
 * @return None
 *
 * @pre vm != NULL
 *
 */
void ptirq_intx_ack(struct acrn_vm *vm, uint32_t virt_gsi)
{
	uint32_t phys_irq;
	struct ptirq_remapping_info *entry;
	DEFINE_INTX_SID(virt_sid, virt_gsi);

	entry = find_ptirq_entry(PTDEV_INTR_INTX, &virt_sid, vm);
	if (entry != NULL) {
		phys_irq = entry->allocated_pirq;

		/* NOTE: only Level trigger will process EOI/ACK and if we got here
		 * means we have this vioapic or vpic or both enabled
		 */
		/* Deassert: restore to inactive state after interrupt is acknowledged */
		vioapic_deassert_irq_nolock(vm, virt_gsi);

		dev_dbg(DBG_LEVEL_PTIRQ, "dev-assign: irq=0x%x acked vr: 0x%x",
				phys_irq, irq_to_vector(phys_irq));
		ioapic_gsi_unmask_irq(phys_irq);
	}
}

/**
 * @brief MSI/MSI-x remapping for passthrough device.
 *
 * Main entry for PCI device interrupt assignment with MSI and MSI-X.
 * MSI can up to 8 vectors and MSI-X can up to 1024 Vectors.
 * This function first add a ptirq_remapping_info for this interrupt and then update physical MSI
 * address and data registers according to the virtual MSI address and data register values and
 * whether the interrupt can be posted or not.
 *
 * @param[in] vm VM the interrupt is remapped to
 * @param[in] virt_bdf Virtual bdf within vm for the passthrough device
 * @param[in] phys_bdf Physical bdf of the passthrough device
 * @param[in] entry_nr MSI/MSI-x vector index of the interrupt, 0 means first vector
 * @param[inout] info Virtual MSI address and data
 *
 * @return Error code or 0 on success
 * @retval 0 On success
 * @retval -ENODEV For Service VM, the entry already be held by others. For User VM, no pre-hold mapping found.
 *
 * @pre vm != NULL
 * @pre info != NULL
 *
 */
int32_t ptirq_prepare_msix_remap(struct acrn_vm *vm, uint16_t virt_bdf, uint16_t phys_bdf,
				uint16_t entry_nr, struct msi_info *info)
{
	struct ptirq_remapping_info *entry;
	int32_t ret = -ENODEV;
	union pci_bdf vbdf;

	/*
	 * adds the mapping entries at runtime, if the
	 * entry already be held by others, return error.
	 */
	spinlock_obtain(&ptdev_lock);
	entry = add_msix_remapping(vm, virt_bdf, phys_bdf, entry_nr);
	spinlock_release(&ptdev_lock);

	if (entry != NULL) {
		ret = 0;
		entry->vmsi = *info;

		/* build physical config MSI, update to info->pmsi_xxx */
		if (is_lapic_pt_configured(vm)) {
				/*
				 * Use guest vector to program the interrupt source
				 */
				ptirq_build_physical_msi(vm, entry, (uint32_t)info->data.bits.vector, 0UL);
		} else {
			struct acrn_vcpu *vcpu = is_single_destination(vm, info);

			if (is_pi_capable(vm) && (vcpu != NULL)) {
				ptirq_build_physical_msi(vm, entry,
					(uint32_t)info->data.bits.vector, hva2hpa(get_pi_desc(vcpu)));
			} else {
				/* Go with remapped mode if we cannot handle it in posted mode */
				ptirq_build_physical_msi(vm, entry, irq_to_vector(entry->allocated_pirq), 0UL);
			}
		}

		if (ret == 0) {
			*info = entry->pmsi;
			vbdf.value = virt_bdf;
			dev_dbg(DBG_LEVEL_IRQ, "PCI %x:%x.%x MSI VR[%d] 0x%x->0x%x assigned to vm%d",
				vbdf.bits.b, vbdf.bits.d, vbdf.bits.f, entry_nr, entry->vmsi.data.bits.vector,
				irq_to_vector(entry->allocated_pirq), entry->vm->vm_id);
		}
	}

	return ret;
}

/**
 * @brief Activate an remapped interrupt by setting up physical IOAPIC RTE
 *
 * - Mask the physical IOAPIC interrupt
 * - Build the remapped physical IOAPIC RTE from virtual RTE programmed in vIOAPIC.
 * - Set trigger mode of the physical interrupt according to virtual RTE
 * - Set the remapped physical IOAPIC RTE, with mask bit set.
 * - If the virtual RTE was unmasked, unmask the physical RTE as well.
 *
 * @param[in] vm VM the interrupt is remapped to
 * @param[in] entry The ptirq_remapping_info to describe the remapped interrupt
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre entry != NULL
 */
static void activate_ioapic_rte(struct acrn_vm *vm,
		struct ptirq_remapping_info *entry)
{
	union ioapic_rte rte;
	uint32_t phys_irq = entry->allocated_pirq;
	uint64_t intr_mask;
	bool is_lvl_trigger = false;

	/* disable interrupt */
	ioapic_gsi_mask_irq(phys_irq);

	/* build physical IOAPIC RTE */
	rte = ptirq_build_physical_rte(vm, entry);
	intr_mask = rte.bits.intr_mask;

	/* update irq trigger mode according to info in guest */
	if (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL) {
		is_lvl_trigger = true;
	}
	set_irq_trigger_mode(phys_irq, is_lvl_trigger);

	/* set rte entry when masked */
	rte.bits.intr_mask = IOAPIC_RTE_MASK_SET;
	ioapic_set_rte(phys_irq, rte);

	if (intr_mask == IOAPIC_RTE_MASK_CLR) {
		ioapic_gsi_unmask_irq(phys_irq);
	}
}

/**
 * @brief INTx interrupt remapping for passthrough device.
 *
 * This is called to add identical INTx remapping for Service VM when it unmasks vIOAPIC RTE for
 * a vGSI at the first time. This function also activates the remapping first time the interrupt
 * is unmasked. Later masking operations will mask the physical interrupt.
 *
 * @param[in] vm The VM to check and remap
 * @param[in] virt_gsi Virtual as well as physical GSI number associated with the passthrough device
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 * @retval -ENODEV For Service VM, the entry already be held by others. For User VM, no pre-hold mapping found.
 * @retval -EINVAL The virt_gsi is not for a passthrough device
 *
 * @pre vm != NULL
 */
int32_t ptirq_intx_pin_remap(struct acrn_vm *vm, uint32_t virt_gsi)
{
	int32_t status = 0;
	struct ptirq_remapping_info *entry = NULL;
	DEFINE_INTX_SID(virt_sid, virt_gsi);

	/*
	 * virt pin could come from primary vPIC, secondary vPIC or vIOAPIC
	 * while phys pin is always means for physical IOAPIC.
	 *
	 * Device Model should pre-hold the mapping entries by calling
	 * ptirq_add_intx_remapping for User VM.
	 *
	 * For Service VM, it adds the mapping entries at runtime, if the
	 * entry already be held by others, return error.
	 */

#ifdef CONFIG_VUART_ENABLED
	/* no remap for vuart intx */
	if (is_vuart_intx(vm, virt_sid.intx_id.gsi)) {
		status = -EINVAL;
	}
#endif

	if (status == 0) {
		/* query if we have virt to phys mapping */
		spinlock_obtain(&ptdev_lock);
		entry = find_ptirq_entry(PTDEV_INTR_INTX, &virt_sid, vm);
		if (entry == NULL) {
			if (is_service_vm(vm)) {
				uint32_t phys_gsi = virt_gsi;

				entry = add_intx_remapping(vm, virt_gsi, phys_gsi);
				if (entry == NULL) {
					pr_err("%s, add intx remapping failed",
							__func__);
					status = -ENODEV;
				}
			} else {
				/* ptirq_intx_pin_remap is triggered by vIOAPIC
				 * every time a pin get unmask, here filter out pins
				 * not get mapped.
				 */
				status = -ENODEV;
			}
		}
		spinlock_release(&ptdev_lock);
	}

	if (status == 0) {
		activate_ioapic_rte(vm, entry);
	}

	return status;
}

/**
 * @brief Add an interrupt remapping entry for INTx as pre-hold mapping.
 *
 * Except Service VM, this function is called to build INTx remapping during VM creation.
 * The entry is identified by phys_pin, one entry vs. one phys_pin.
 * Currently, one phys_pin can only be held by one pin source (vPIC or vIOAPIC).
 *
 * @param[in] vm The VM the virt_gsi is in, for the interrupt remapping
 * @param[in] virt_gsi virtual pin number associated with the passthrough device
 * @param[in] phys_gsi physical pin number associated with the passthrough device
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 * @retval -ENODEV failed to add the remapping entry
 *
 * @pre vm != NULL
 */
int32_t ptirq_add_intx_remapping(struct acrn_vm *vm, uint32_t virt_gsi, uint32_t phys_gsi)
{
	struct ptirq_remapping_info *entry;

	spinlock_obtain(&ptdev_lock);
	entry = add_intx_remapping(vm, virt_gsi, phys_gsi);
	spinlock_release(&ptdev_lock);

	return (entry != NULL) ? 0 : -ENODEV;
}

/**
 * @brief Remove an interrupt remapping entry for INTx.
 *
 * Deactivate & remove mapping entry of the given virt gsi for given vm or
 * phys gsi assigned to this vm.
 *
 * @param[in] vm The VM the gsi is in if is_phy_gsi is False, or NULL if is_phy_gsi is True
 * @param[in] gsi virtual gsi number or physical gsi number associated with the passthrough device
 * @param[in] is_phy_gsi true if gsi is physical, false if gsi is virtual for the vm
 *
 * @return None
 *
 * @pre vm != NULL
 *
 */
void ptirq_remove_intx_remapping(const struct acrn_vm *vm, uint32_t gsi, bool is_phy_gsi)
{
	spinlock_obtain(&ptdev_lock);
	remove_intx_remapping(vm, gsi, is_phy_gsi);
	spinlock_release(&ptdev_lock);
}

/**
 * @brief Remove interrupt remapping entries for MSI/MSI-x.
 *
 * Remove the mapping of given number of vectors of the given physical BDF for the given vm.
 *
 * @param[in] vm The VM from which we remove the MSI-x remappings
 * @param[in] phys_bdf Physical BDF associated with the passthrough device
 * @param[in] vector_count Number of vector remappings to remove
 *
 * @return None
 *
 * @pre vm != NULL
 *
 */
void ptirq_remove_msix_remapping(const struct acrn_vm *vm, uint16_t phys_bdf,
		uint32_t vector_count)
{
	uint32_t i;

	for (i = 0U; i < vector_count; i++) {
		spinlock_obtain(&ptdev_lock);
		remove_msix_remapping(vm, phys_bdf, i);
		spinlock_release(&ptdev_lock);
	}
}

/**
 * @brief Remove all interrupt remappings for INTx which are defined in VM config.
 *
 * Deactivate & remove all mapping entries of the virt_gsis defined in VM config for given vm.
 *
 * @param[in] vm The VM we remove the INTx remappings from
 *
 * @return None
 *
 * @pre vm != NULL
 */
void ptirq_remove_configured_intx_remappings(const struct acrn_vm *vm)
{
	const struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);
	uint16_t i;

	for (i = 0; i < vm_config->pt_intx_num; i++) {
		ptirq_remove_intx_remapping(vm, vm_config->pt_intx[i].virt_gsi, false);
	}
}

/**
  * @}
  */
