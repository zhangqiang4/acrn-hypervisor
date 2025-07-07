/*-
* Copyright (c) 2011 NetApp, Inc.
* Copyright (c) 2018-2025 Intel Corporation.
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

#ifndef VPCI_H_
#define VPCI_H_

#include <asm/lib/spinlock.h>
#include <lib/util.h>
#include <pci.h>
#include <list.h>

/**
 * @addtogroup vp-dm_vpci
 *
 * @{
 */

/**
 * @file
 * @brief This file declares external APIs exposed by vPCI module
 *
 * This file declares all external macros, data structures and functions that should be provided
 * by the vp-dm.vpci module.
 */

#define VDEV_LIST_HASHBITS 4U  /**< Bit of hash value to be returned */
#define VDEV_LIST_HASHSIZE (1U << VDEV_LIST_HASHBITS)  /**< Size of the hash list */

/**
 * @brief Data structure to present virtual PCI BAR information
 *
 * For a PCI BAR, this structure includes its type (MMIO or IO BAR and 32bits or 64bits), guest
 * sizing this BAR, BAR size, GPA/HPA of BAR base, BAR memory type (low 4bits of a BAR register)
 * and BAR size mask.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_vbar {
	bool is_mem64hi;	/**< Indicating whether this instance is the high part of 64 bits MMIO BAR */
	bool sizing;		/**< Indicating whether the guest is sizing this BAR */
	uint64_t size;		/**< BAR size */
	uint64_t base_gpa;	/**< BAR GPA */
	uint64_t base_hpa;	/**< BAR HPA */
	union pci_bar_type bar_type; /**< The low 2(PIO)/4(MMIO) bits of BAR */
	uint32_t mask;		/**< BAR size mask */
};

/**
 * @brief Data structure to present PCI MSI-X Table structure
 *
 * For PCI MSI-X, this structure includes MSI-X Table Entry Message Address, Message Data
 * and its Vector Control information.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct msix_table_entry {
	uint64_t	addr;  /**< MSI-X Table Entry Message Address register value */
	uint32_t	data;  /**< MSI-X Table Entry Message Data register value */
	uint32_t	vector_control; /**< MSI-X Table Entry Vector Control register value */
};

/**
 * @brief Data structure to present PCI MSI capability basic information
 *
 * For a PCI MSI capability, this structure includes its type (32bits or 64bits capability),
 * offset and length.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_msi {
	bool      is_64bit;  /**< Flag to indicate if Message Address is 64 bit or not */
	uint32_t  capoff;  /**< MSI capability offset */
	uint32_t  caplen;  /**< MSI capability length */
};

/**
 * @brief Data structure to present PCI MSI-X capability structure
 *
 * This structure includes the values of MSI-X capability registers.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark This structure shall be packed.
 */
struct msixcap {
	uint8_t		capid;      /**< MSI-X Capability Capability ID */
	uint8_t		nextptr;    /**< Next Capability Pointer in MSI-X Capability Header */
	uint16_t	msgctrl;    /**< Message Control Register for MSI-X */
	uint32_t	table_info; /**< Table Offset/Table BIR Register for MSI-X */
	uint32_t	pba_info;   /**< PBA Offset/PBA BIR Register for MSI-X */
} __packed;

/**
 * @brief Data structure to present some information of PCI MSI-X capability
 *
 * This structure saves the information related with MSI-X capability and MSI-X Table.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_msix {
	/**
	 * @brief Array to save MSI-X Table Entries
	 */
	struct msix_table_entry table_entries[CONFIG_MAX_MSIX_TABLE_NUM];
	uint64_t  mmio_gpa;  /**< MSI-X Table BAR's GPA */
	uint64_t  mmio_hpa;  /**< MSI-X Table BAR's HPA */
	uint64_t  mmio_size; /**< MSI-X Table BAR's size */
	uint32_t  capoff;    /**< MSI-X capability offset */
	uint32_t  caplen;    /**< MSI-X capability length */
	uint32_t  table_bar; /**< MSI-X Table corresponding BAR's index */
	uint32_t  table_offset;  /**< MSI-X Table Offset */
	uint32_t  table_count;  /**< Count of MSI-X Table Entries */
};

/**
 * @brief Data structure to present some information of PCIe SR-IOV capability
 *
 * This structure saves the information related with SR-IOV capability.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_cap_sriov {
	uint32_t  capoff;  /**< SR-IOV capability offset */
	uint32_t  caplen;  /**< SR-IOV capability length */
	bool      sriov_ro; /**< This is to prevent a guest VM enabling SR-IOV */

	/*
	 * If the vdev is a SRIOV PF vdev, the vbars is used to store
	 * the bar information that is using to initialize SRIOV VF vdev bar.
	 */
	struct pci_vbar vbars[PCI_BAR_COUNT];  /**< Array to save BARs information */
};

/**
 * @brief A union data structure to store PCI Configuration Space data
 *
 * The data can be saved in three formats: byte, word and dword.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
union pci_cfgdata {
	uint8_t data_8[PCIE_CONFIG_SPACE_SIZE];  /**< PCI Configuration Space data save in one byte format */
	uint16_t data_16[PCIE_CONFIG_SPACE_SIZE >> 1U]; /**< PCI Configuration Space data save in word format */
	uint32_t data_32[PCIE_CONFIG_SPACE_SIZE >> 2U]; /**< PCI Configuration Space data save in dword format */
};

struct pci_vdev;

/**
 * @brief Data structure to present a set of operation functions for a virtual PCI device
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_vdev_ops {
	/**
	 * @brief Callback function used to initialize a virtual PCI device
	 */
	void    (*init_vdev)(struct pci_vdev *vdev);
	/**
	 * @brief Callback function used to deinitialize a virtual PCI device
	 */
	void    (*deinit_vdev)(struct pci_vdev *vdev);
	/**
	 * @brief Callback function used to write virtual PCI device Configuration Space
	 */
	int32_t (*write_vdev_cfg)(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val);
	/**
	 * @brief Callback function used to read virtual PCI device Configuration Space
	 */
	int32_t (*read_vdev_cfg)(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t *val);
};

/**
 * @brief Data structure to present information used to handle a virtual PCI device
 *
 * This data structure contains all useful information used to handle a virtual PCI device,
 * e.g. information about PCI Configuration Space, callback functions used to handle device,
 * pointers used to judge the device serving target and so on.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_vdev {
	uint32_t id;  /**< Device ID */
	struct acrn_vpci *vpci;  /**< Pointer to acrn_vpci structure instance */
	/* The bus/device/function triple of the virtual PCI device. */
	union pci_bdf bdf;  /**< Device BDF (bus/device/function) */

	struct pci_pdev *pdev;  /**< Pointer to the data structure of PCI physical device */

	union pci_cfgdata cfgdata;  /**< PCI Configuration Space data structure instance */

	uint32_t flags;  /**< Flags to indicate device type, e.g. quirk passthrough device */

	/* The bar info of the virtual PCI device. */
	uint32_t nr_bars; /**< Number of BARs: 6 for normal device, 2 for bridge, 1 for cardbus */
	struct pci_vbar vbars[PCI_BAR_COUNT];  /**< Array to save BARs data */

	uint8_t	prev_capoff; /**< Offset of previous PCI capability */
	uint8_t	free_capoff; /**< Next free offset to add PCI capability */

	struct pci_msi msi;  /**< MSI data structure instance */
	struct pci_msix msix;  /**< MSI-X data structure instance */
	struct pci_cap_sriov sriov;  /**< SR-IOV data structure instance */
	struct pcie_cap ats;  /**< ATS data structure instance */
	struct pcie_cap pri;  /**< PRI data structure instance */
	struct pcie_cap acs;  /**< ACS data structure instance */

	struct pci_vdev *phyfun;  /**< Pointer to the SR-IOV VF associated PF's vdev */

	/**
	 * @brief Pointer to corresponding PCI device's configuration in VM
	 */
	struct acrn_vm_pci_dev_config *pci_dev_config;

	/**
	 * @brief Pointer to data structure of a set of operation functions for a virtual PCI device
	 */
	const struct pci_vdev_ops *vdev_ops;

	/*
	 * vdev in    |   HV       |   pre-VM       |          Service VM                        | post-VM
	 *            |            |                |vdev used by Service VM|vdev used by post-VM|
	 * ----------------------------------------------------------------------------------------------------------
	 * parent_user| NULL(HV)   |   NULL(HV)     |   NULL(HV)            |   NULL(HV)         | vdev in Service VM
	 * ----------------------------------------------------------------------------------------------------------
	 * user       | vdev in HV | vdev in pre-VM |   vdev in Service VM  |   vdev in post-VM  | vdev in post-VM
	 */
	struct pci_vdev *parent_user;  /**< Pointer to current virtual PCI device's parent */
	/**
	 * @brief Pointer to current virtual PCI device
	 *
	 * NULL means this device is not used or is a zombie VF.
	 */
	struct pci_vdev *user;
	/**
	 * @brief Instance of hash list node
	 *
	 * The node is used to add the virtual PCI device into a device hash list.
	 */
	struct hlist_node link;
	void *priv_data; /**< Pointer to a buffer which saves private data used by the device */
};

/**
 * @brief A union data structure to store CONFIG_ADDRESS register value
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
union pci_cfg_addr_reg {
	uint32_t value;  /**< CONFIG_ADDRESS register value */
	/**
	 * @brief Data structure to present CONFIG_ADDRESS register value according to bits
	 */
	struct {
		uint32_t reg_num : 8;	/**< BITs 0-7, Register Number (BITs 0-1, always reserve to 0) */
		uint32_t bdf : 16;	/**< BITs 8-23, BDF Number */
		uint32_t resv : 7;	/**< BITs 24-30, Reserved */
		uint32_t enable : 1;	/**< BITs 31, Enable bit */
	} bits;
};

/**
 * @brief Data structure to record MMIO resource
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_mmio_res {
	uint64_t start;  /**< MMIO region start address */
	uint64_t end;    /**< MMIO region end address */
};

/**
 * @brief Data structure to present information used to handle virtual PCI component
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct acrn_vpci {
	spinlock_t lock;  /**< Spin lock used to protect critical data */
	/**
	 * @brief Instance of the union data structure to store CONFIG_ADDRESS register value
	 */
	union pci_cfg_addr_reg addr;
	/**
	 * @brief Instance of the data structure to save MMCFG region information
	 */
	struct pci_mmcfg_region pci_mmcfg;
	struct pci_mmio_res res32;  /**< 32-bit MMIO region start/end address */
	struct pci_mmio_res res64;  /**< 64-bit MMIO region start/end address */
	struct pci_vdev pci_vdevs[CONFIG_MAX_PCI_DEV_NUM]; /**< Array to manage virtual PCI devices */
	/**
	 * @brief Bitmap used to indicate whether the virtual PCI device has been initialized
	 */
	uint64_t vdev_bitmaps[INT_DIV_ROUNDUP(CONFIG_MAX_PCI_DEV_NUM, 64U)];
	/**
	 * @brief Hash list to manage virtual PCI devices
	 */
	struct hlist_head vdevs_hlist_heads [VDEV_LIST_HASHSIZE];
};

struct acrn_vm;

extern const struct pci_vdev_ops vhostbridge_ops;
extern const struct pci_vdev_ops vpci_bridge_ops;
#ifdef CONFIG_PCI_MULTIFUNCTION_SUPPORT
extern const struct pci_vdev_ops vpci_mf_dev_ops;
#endif

int32_t init_vpci(struct acrn_vm *vm);
void deinit_vpci(struct acrn_vm *vm);
struct pci_vdev *pci_find_vdev(struct acrn_vpci *vpci, union pci_bdf vbdf);
struct acrn_pcidev;
int32_t vpci_assign_pcidev(struct acrn_vm *tgt_vm, struct acrn_pcidev *pcidev);
int32_t vpci_deassign_pcidev(struct acrn_vm *tgt_vm, struct acrn_pcidev *pcidev);
struct pci_vdev *vpci_init_vdev(struct acrn_vpci *vpci, struct acrn_vm_pci_dev_config *dev_config,
				struct pci_vdev *parent_pf_vdev);
void vpci_deinit_vdev(struct pci_vdev *vdev);
int32_t vpci_read_cfg(struct acrn_vpci *vpci, union pci_bdf bdf,
		      uint32_t offset, uint32_t bytes, uint32_t *val);

/**
 * @brief Check if the BAR is mapped into I/O Space
 *
 * @param[in] vbar Pointer to virtual PCI BAR.
 *
 * @return A boolean value to indicate whether the BAR is mapped into I/O Space
 *
 * @retval true  I/O Space BAR.
 * @retval false Memory Space BAR.
 *
 * @pre vbar != NULL
 */
static inline bool is_pci_io_bar(struct pci_vbar *vbar)
{
        return ((vbar->bar_type.io_space.indicator == 1U) && (!vbar->is_mem64hi));
}

/**
 * @brief Check if the BAR is mapped into Memory Space
 *
 * @param[in] vbar Pointer to virtual PCI BAR.
 *
 * @return A boolean value to indicate whether the BAR is mapped into Memory Space
 *
 * @retval true  Memory Space BAR.
 * @retval false I/O Space BAR.
 *
 * @pre vbar != NULL
 */
static inline bool is_pci_mem_bar(struct pci_vbar *vbar)
{
        return ((vbar->is_mem64hi) || ((vbar->bar_type.mem_space.indicator == 0U)));
}

/**
 * @brief Check if the reserve bit of the BAR is set
 *
 * For Memory Space mapped BAR, check bit 1 of Memory Type and confirm we are checking the low
 * BAR. For I/O Space mapped BAR, check the Reserved bit directly.
 *
 * @param[in] vbar Pointer to virtual PCI BAR.
 *
 * @return A boolean value to indicate whether the reserve bit of the BAR is set
 *
 * @retval true  Reserved bit is set.
 * @retval false Reserved bit is not set.
 *
 * @pre vbar != NULL
 */
static inline bool is_pci_reserved_bar(struct pci_vbar *vbar)
{
        return (((vbar->bar_type.mem_space.indicator == 0U) &&
		((vbar->bar_type.mem_space.mem_type & 0x1U) == 0x1U) && (!vbar->is_mem64hi)) ||
		((vbar->bar_type.io_space.indicator == 1U) && (vbar->bar_type.io_space.reserved == 1U)));
}

/**
 * @brief Check if the BAR is 32 bits Memory Base BAR
 *
 * First, check whether the BAR is mapped into Memory Space. Second, check if the BAR's Memory
 * Type is 32 bit wide. Last, confirm the BAR is the low part which contains the space indicator
 * and Memory Type information.
 *
 * @param[in] vbar Pointer to virtual PCI BAR.
 *
 * @return A boolean value to indicate whether the Memory Space mapped BAR is 32 bits wide
 *
 * @retval true  The Memory Space mapped BAR is 32 bits wide.
 * @retval false The Memory Space mapped BAR is not 32 bits wide or the BAR is I/O Space mapped.
 *
 * @pre vbar != NULL
 */
static inline bool is_pci_mem32_bar(struct pci_vbar *vbar)
{
        return ((vbar->bar_type.mem_space.indicator == 0U) &&
		(vbar->bar_type.mem_space.mem_type == 0U) && (!vbar->is_mem64hi));
}

/**
 * @brief Check if the BAR is 64 bits Memory Base BAR
 *
 * First, check whether the BAR is mapped into Memory Space. Second, check if the BAR's Memory
 * Type is 64 bit wide. Last, confirm the BAR is the low part which contains the space indicator
 * and Memory Type information.
 *
 * @param[in] vbar Pointer to virtual PCI BAR.
 *
 * @return A boolean value to indicate whether the BAR is 64 bits wide and it is the low part
 *
 * @retval true  The Memory Space mapped BAR is 64 bits wide.
 * @retval false The Memory Space mapped BAR is not 64 bits wide or the BAR is the high part
 *               or the BAR is I/O Space mapped.
 *
 * @pre vbar != NULL
 */
static inline bool is_pci_mem64lo_bar(struct pci_vbar *vbar)
{
        return ((vbar->bar_type.mem_space.indicator == 0U) &&
		(vbar->bar_type.mem_space.mem_type == 2U) && (!vbar->is_mem64hi));
}

/**
 * @}
 */
#endif /* VPCI_H_ */
