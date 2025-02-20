/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <hash.h>
#include <asm/per_cpu.h>
#include <asm/guest/vm.h>
#include <softirq.h>
#include <ptdev.h>
#include <irq.h>
#include <logmsg.h>
#include <asm/vtd.h>
#include <ticks.h>

/**
 * @defgroup vp-dm_ptirq vp-dm.ptirq
 * @ingroup vp-dm
 * @brief The definition and implementation of Device Interrupt Passthrough.
 *
 * @{
 */

/**
 * @file
 *
 * @brief Implementation of device interrupt pass-through
 */

#define PTIRQ_ENTRY_HASHBITS	9U      /**< The number of bits used for PTIRQ hash table size */
#define PTIRQ_ENTRY_HASHSIZE	(1U << PTIRQ_ENTRY_HASHBITS)    /**< PTIRQ hash table size */

#define INVALID_PTDEV_ENTRY_ID 0xffffU          /**< Mark the ptirq entry allocation failed */
#define PTIRQ_BITMAP_ARRAY_SIZE	INT_DIV_ROUNDUP(CONFIG_MAX_PT_IRQ_ENTRIES, 64U) /**< Count of PTIRQ entries */
struct ptirq_remapping_info ptirq_entries[CONFIG_MAX_PT_IRQ_ENTRIES];   /**< Static pool for PTIRQ entries */
static uint64_t ptirq_entry_bitmaps[PTIRQ_BITMAP_ARRAY_SIZE];           /**< Bitmap of used items in ptirq_entries */
spinlock_t ptdev_lock = { .head = 0U, .tail = 0U, };            /**< Spin lock for PTIRQ module */

/**
 * @brief Hash table to look up mapping info from physical sid
 *
 * Hashed from physical sid + acrn_vm structure address (always NULL)
 */
static struct hlist_head phys_sid_htable[PTIRQ_ENTRY_HASHSIZE];

/**
 * @brief Hash table to look up mapping info from virtual sid within a vm
 *
 * Hashed from virtual sid + acrn_vm structure address
 */
static struct hlist_head virt_sid_htable[PTIRQ_ENTRY_HASHSIZE];

/**
 * @brief Allocate an ptirq_remapping_info structure
 *
 * Allocate an unused entry in ptirq_entries pool and return its index. This function can be
 * called from multiple processors at the same time. So atomic operation is used to check and
 * set the ptirq_entry_bitmaps.
 *
 * @return Index of allocated entry or INVALID_PTDEV_ENTRY_ID.
 * @retval [0, CONFIG_MAX_PT_IRQ_ENTRIES) Index of allocated entry.
 * @retval INVALID_PTDEV_ENTRY_ID No free entry available.
 */
static inline uint16_t ptirq_alloc_entry_id(void)
{
	uint16_t id = (uint16_t)ffz64_ex(ptirq_entry_bitmaps, CONFIG_MAX_PT_IRQ_ENTRIES);

	while (id < CONFIG_MAX_PT_IRQ_ENTRIES) {
		if (!bitmap_test_and_set_lock((id & 0x3FU), &ptirq_entry_bitmaps[id >> 6U])) {
			break;
		}
		id = (uint16_t)ffz64_ex(ptirq_entry_bitmaps, CONFIG_MAX_PT_IRQ_ENTRIES);
	}

	return (id < CONFIG_MAX_PT_IRQ_ENTRIES) ? id: INVALID_PTDEV_ENTRY_ID;
}

/**
 * @brief Calculate hash key to look up ptirq_remapping_info
 *
 * ptirq_remapping_info manages how a physical interrupt is mapped to a VM as a virtual interrupt.
 * So we may check whether an existing mapped was established for a physical interrupt or a
 * virtual interrupt in a VM. When searching from physical source id, the vm pointer is NULL.
 * Hashing from source id value and acrn_vm structure address can decrease the probability of hash
 * collisions as different VMs may have equal virtual source ids.
 *
 * @param[in] vm The VM we are looking for, or NULL when searching for a physical interrupt
 * @param[in] sid Source id of a physical or virtual interrupt
 *
 * @return A hash table index for the given interrupt source and VM
 */
static inline uint64_t ptirq_hash_key(const struct acrn_vm *vm,
		const union source_id *sid)
{
	return hash64(sid->value + (uint64_t)vm, PTIRQ_ENTRY_HASHBITS);
}

/**
 * @brief Find a ptdev entry for a VM with given source id
 *
 * Find the active ptirq_remapping_info from physical source id (vm is NULL) or
 * virtual source id of a VM.
 *
 * param[in] intr_type Interrupt type of the ptirq entry
 * param[in] sid Source id of the ptirq entry
 * param[in] vm VM pointer of the ptirq entry if find the ptdev entry for a virtual sid
 *
 * @return A pointer to the ptirq_remapping_info entry found or NULL
 * @retval NULL when no ptirq entry matches.
 * @retval pointer_to_found_entry When there is active ptirq entry matching the vm, sid and intr_type.
 */
struct ptirq_remapping_info *find_ptirq_entry(uint32_t intr_type,
		const union source_id *sid, const struct acrn_vm *vm)
{
	struct hlist_node *p;
	struct hlist_head *b;
	struct ptirq_remapping_info *n, *entry = NULL;
	uint64_t key = ptirq_hash_key(vm, sid);

	if (vm == NULL) {
		b = &(phys_sid_htable[key]);

		hlist_for_each(p, b) {
			n = hlist_entry(p, struct ptirq_remapping_info, phys_link);
			if (is_entry_active(n)) {
				if ((intr_type == n->intr_type) && (sid->value == n->phys_sid.value)) {
					entry = n;
					break;
				}
			}
		}
	} else {
		b = &(virt_sid_htable[key]);
		hlist_for_each(p, b) {
			n = hlist_entry(p, struct ptirq_remapping_info, virt_link);
			if (is_entry_active(n)) {
				if ((intr_type == n->intr_type) && (sid->value == n->virt_sid.value) && (vm == n->vm)) {
					entry = n;
					break;
				}
			}
		}
	}

	return entry;
}

/**
 * @brief Raise a PTDEV softirq for a mapped interrupt
 *
 * Insert the ptirq_remapping_info to current CPU's softirq_dev_entry_list and fire a
 * SOFTIRQ_PTDEV softirq.
 *
 * @param[in] entry The ptirq entry describing a mapped interrupt
 *
 * @return None
 */
static void ptirq_enqueue_softirq(struct ptirq_remapping_info *entry)
{
	uint64_t rflags;

	/* enqueue request in order, SOFTIRQ_PTDEV will pickup */
	CPU_INT_ALL_DISABLE(&rflags);

	/* avoid adding recursively */
	list_del(&entry->softirq_node);
	/* TODO: assert if entry already in list */
	list_add_tail(&entry->softirq_node, &get_cpu_var(softirq_dev_entry_list));
	CPU_INT_ALL_RESTORE(rflags);
	fire_softirq(SOFTIRQ_PTDEV);
}

/**
 * @brief Interrupt remapping timer callback
 *
 * This timer callback enqueues a softirq for the mapped interrupt.
 *
 * @param[in] data Private data for timer, it's a pointer to ptirq remapping info
 *
 * @return None
 */
static void ptirq_intr_delay_callback(void *data)
{
	struct ptirq_remapping_info *entry = (struct ptirq_remapping_info *) data;

	ptirq_enqueue_softirq(entry);
}

/**
 * @brief Dequeue a pending ptirq remapping info node
 *
 * Check if there are pending interrupts to remap in ptirq softirq handler. PTIRQs for
 * Post-launched VMs can be remapped with a configurable delay to prevent interrupt storm for VMs.
 *
 * @param[in] pcpu_id ID of the Physical processor from which we dequeue a pending softirq node
 *
 * @return A pointer to the dequeued ptirq_remapping_info entry or NULL
 * @retval NULL when no pending ptirq entry.
 * @retval pointer_to_dequeued_entry When a pending ptirq entry is successfully dequeued.
 */
struct ptirq_remapping_info *ptirq_dequeue_softirq(uint16_t pcpu_id)
{
	uint64_t rflags;
	struct ptirq_remapping_info *entry = NULL;

	CPU_INT_ALL_DISABLE(&rflags);

	while (!list_empty(&get_cpu_var(softirq_dev_entry_list))) {
		entry = get_first_item(&per_cpu(softirq_dev_entry_list, pcpu_id), struct ptirq_remapping_info, softirq_node);

		list_del_init(&entry->softirq_node);

		/* if Service VM, just dequeue, if User VM, check delay timer */
		if (is_service_vm(entry->vm) || timer_expired(&entry->intr_delay_timer, cpu_ticks(), NULL)) {
			break;
		} else {
			/* add it into timer list; dequeue next one */
			(void)add_timer(&entry->intr_delay_timer);
			entry = NULL;
		}
	}

	CPU_INT_ALL_RESTORE(rflags);
	return entry;
}

/**
 * @brief Allocate a ptirq_remapping_info and initialize it with given vm and interrupt type
 *
 * If there is available free ptirq_remapping_info entry, allocate one and initialize it with
 * given vm and interrupt type. The intr_delay_timer is initialized with handler
 * ptirq_intr_delay_callback.
 *
 * @param[in] vm The VM this ptirq_remapping_info is for
 * @param[in] intr_type Type of the interrupt to map
 *
 * @return A pointer to the allocated ptirq_remapping_info entry or NULL
 */
struct ptirq_remapping_info *ptirq_alloc_entry(struct acrn_vm *vm, uint32_t intr_type)
{
	struct ptirq_remapping_info *entry = NULL;
	uint16_t ptirq_id = ptirq_alloc_entry_id();

	if (ptirq_id < CONFIG_MAX_PT_IRQ_ENTRIES) {
		entry = &ptirq_entries[ptirq_id];
		(void)memset((void *)entry, 0U, sizeof(struct ptirq_remapping_info));
		entry->ptdev_entry_id = ptirq_id;
		entry->intr_type = intr_type;
		entry->vm = vm;
		entry->intr_count = 0UL;
		entry->irte_idx = INVALID_IRTE_ID;

		INIT_LIST_HEAD(&entry->softirq_node);

		initialize_timer(&entry->intr_delay_timer, ptirq_intr_delay_callback, entry, 0UL, 0UL);

		entry->active = false;
	} else {
		pr_err("Alloc ptdev irq entry failed");
	}

	return entry;
}

/**
 * @brief Release a ptirq remapping info entry
 *
 * - Remove softirq node from local list.
 * - Remove the interrupt delay timer
 * - Clear the bitmap to mark it free.
 *
 * @param[in] entry The ptirq remapping info entry to release
 *
 * @return None
 */
void ptirq_release_entry(struct ptirq_remapping_info *entry)
{
	uint64_t rflags;

	CPU_INT_ALL_DISABLE(&rflags);
	list_del_init(&entry->softirq_node);
	del_timer(&entry->intr_delay_timer);
	CPU_INT_ALL_RESTORE(rflags);

	bitmap_clear_lock((entry->ptdev_entry_id) & 0x3FU, &ptirq_entry_bitmaps[entry->ptdev_entry_id >> 6U]);

	(void)memset((void *)entry, 0U, sizeof(struct ptirq_remapping_info));
}

/**
 * @brief Handle a passthrough interrupt
 *
 * For Service VM, the ptirq is enqueued directly. For VMs with interrupt storm detection enabled,
 * i.e. vm->intr_inject_delay_delta > 0, start a timer with the delay to enqueue the ptirq.
 * Subsequent physical interrupt within the delay window are ignored to avoid interrupt flooding.
 *
 * This function is executed in interrupt context.
 *
 * @param[in] irq IRQ number of the interrupt
 * @param[in] data PTIRQ remapping info for the interrupt
 *
 * @return None
 */
static void ptirq_interrupt_handler(__unused uint32_t irq, void *data)
{
	struct ptirq_remapping_info *entry = (struct ptirq_remapping_info *) data;
	bool to_enqueue = true;

	/*
	 * "interrupt storm" detection & delay intr injection just for User VM
	 * pass-thru devices, collect its data and delay injection if needed
	 */
	if (!is_service_vm(entry->vm)) {
		entry->intr_count++;

		/* if delta > 0, set the delay TSC, dequeue to handle */
		if (entry->vm->intr_inject_delay_delta > 0UL) {

			/* if the timer started (entry is in timer-list), not need enqueue again */
			if (timer_is_started(&entry->intr_delay_timer)) {
				to_enqueue = false;
			} else {
				update_timer(&entry->intr_delay_timer,
					     cpu_ticks() + entry->vm->intr_inject_delay_delta, 0UL);
			}
		} else {
			update_timer(&entry->intr_delay_timer, 0UL, 0UL);
		}
	}

	if (to_enqueue) {
		ptirq_enqueue_softirq(entry);
	}
}

/**
 * @brief Activate remapping for an physical interrupt mapped to guest
 *
 * If the VM doesn't support Posted Interrupt, or this is a PTDEV_INTR_INTX interrupt, the
 * physical interrupt will always be trapped to hypervisor. So request a handler
 * ptirq_interrupt_handler for the physical irq. The handler will inject a virtual interrupt to
 * guest to achieve interrupt remapping.
 *
 * @param[in] entry PTIRQ remapping info for the interrupt
 * @param[in] phys_irq IRQ of the physical interrupt to remap
 *
 * @return Error code or 0 on success
 * @retval 0 Successfully activated the interrupt remapping
 * @retval <0 Error code from request_irq. Refer it for details.
 */
int32_t ptirq_activate_entry(struct ptirq_remapping_info *entry, uint32_t phys_irq)
{
	int32_t ret = 0;
	uint32_t irq = IRQ_INVALID;
	uint64_t key;

	if ((entry->intr_type == PTDEV_INTR_INTX) || !is_pi_capable(entry->vm)) {
		/* register and allocate host vector/irq */
		ret = request_irq(phys_irq, ptirq_interrupt_handler, (void *)entry, IRQF_PT);
		if (ret >=0) {
			irq = (uint32_t)ret;
		} else {
			pr_err("request irq failed, please check!, phys-irq=%d", phys_irq);
		}
	}

	if (ret >=0) {
		entry->allocated_pirq = irq;
		entry->active = true;

		key = ptirq_hash_key(NULL, &(entry->phys_sid));
		hlist_add_head(&entry->phys_link, &(phys_sid_htable[key]));
		key = ptirq_hash_key(entry->vm, &(entry->virt_sid));
		hlist_add_head(&entry->virt_link, &(virt_sid_htable[key]));
	}

	return ret;
}

/**
 * @brief Deactivate an interrupt remapping
 *
 * - Delete links on ptirq remapping info hash tables
 * - Set active false
 * - If a physical irq was requested (for a non-posted interrupt), free the irq.
 *
 * @param[in] entry The ptirq remapping info entry
 *
 * @return None
 */
void ptirq_deactivate_entry(struct ptirq_remapping_info *entry)
{
	hlist_del(&entry->phys_link);
	hlist_del(&entry->virt_link);
	entry->active = false;
	if (entry->allocated_pirq != IRQ_INVALID) {
		free_irq(entry->allocated_pirq);
	}
}

/**
 * @brief Passthrough device global data structure initialization.
 *
 * During the hypervisor cpu initialization stage, this function:
 * - BSP registers SOFTIRQ_PTDEV handler for all CPU
 * - Initialize the softirq entry list for each CPU
 *
 * @return None
 */
void ptdev_init(void)
{
	if (get_pcpu_id() == BSP_CPU_ID) {
		register_softirq(SOFTIRQ_PTDEV, ptirq_softirq);
	}
	INIT_LIST_HEAD(&get_cpu_var(softirq_dev_entry_list));
}

/**
 * @brief Deactivate and release all ptirq entries for a VM.
 *
 * This function deactivates and releases all ptirq entries for a VM. The function
 * should only be called after the VM is already down.
 *
 * @param[in]    vm acrn_vm on which the ptirq entries will be released
 *
 * @return None
 *
 * @remark VM is already down
 */
void ptdev_release_all_entries(const struct acrn_vm *vm)
{
	struct ptirq_remapping_info *entry;
	uint16_t idx;

	/* VM already down */
	for (idx = 0U; idx < CONFIG_MAX_PT_IRQ_ENTRIES; idx++) {
		entry = &ptirq_entries[idx];
		if ((entry->vm == vm) && is_entry_active(entry)) {
			spinlock_obtain(&ptdev_lock);
			if (entry->release_cb != NULL) {
				entry->release_cb(entry);
			}
			ptirq_deactivate_entry(entry);
			ptirq_release_entry(entry);
			spinlock_release(&ptdev_lock);
		}
	}

}

/**
 * @brief Get statistics for all non-posted interrupts remapped to a VM
 *
 * Only active and non-posted remapping info are taken into account.
 *
 * @param[in] target_vm The VM of the ptirq
 * @param[out] buffer The buffer to store irq statistics
 * @param[in] buffer_cnt Size of buffer
 *
 * @return Count of filled data in given buffer
 */
uint32_t ptirq_get_intr_data(const struct acrn_vm *target_vm, uint64_t *buffer, uint32_t buffer_cnt)
{
	uint32_t index = 0U;
	uint16_t i;
	struct ptirq_remapping_info *entry;

	for (i = 0U; i < CONFIG_MAX_PT_IRQ_ENTRIES; i++) {
		entry = &ptirq_entries[i];
		if (!is_entry_active(entry) || (entry->allocated_pirq == IRQ_INVALID)) {
			continue;
		}
		if (entry->vm == target_vm) {
			buffer[index] = entry->allocated_pirq;
			buffer[index + 1U] = entry->intr_count;

			index += 2U;
			if (index > (buffer_cnt - 2U)) {
				break;
			}
		}
	}

	return index;
}

/**
 * @}
 */
