#include "diagnostics.h"
#include <dxe.h>
#include <rtl.h>
#include <errno.h>
#include <pci.h>
#include <asm/pgtable.h>

/* TODO add lock for each instance ? Not needed if only one consumer */
struct telltale_private_data {
	struct telltale_enable_crc_data current;
	struct display_region regions[MAX_REGION_PER_PIPE];
	struct telltale_crc_regs records[MAX_RECORDS_PER_PIPE];
	uint32_t next_region; 	/* next region to check */
	uint32_t next_record; 	/* next record to fill, actual index is next_record % MAX_RECORDS_PER_PIPE */
	uint32_t enabled;
	uint64_t reg; 	/* reg base of GPU MMIO bar 0 */
	uint32_t vector; 	/* vblank MSI-X vector */
};

/*
 * Define multiple instances if more GPUs or more pipes are applied for
 * telltale simultaneously. Move this settings to a diagnosit init hypercall.
 */
static struct telltale_private_data priv_data[MAX_TELLTALE_INSTANCES] = {
	{
		.current = {
			.bdf = 0x0040,
			.pipe = 0,
		},
	},
	{
		.current = {
			.bdf = 0x0040,
			.pipe = 1,
		},
	},
};

/*
 * Get the private data for given GPU and pipe.
 *
 * If not enabled before, use a free private data structure for it.
 * Otherwise, return the matched private data structure.
 */
static struct telltale_private_data *get_priv_data(uint16_t bdf, uint8_t pipe)
{
	int i;
	struct telltale_private_data *priv;

	for (i = 0; i < MAX_TELLTALE_INSTANCES; i++) {
		priv = &priv_data[i];
		if (priv->current.bdf == bdf && priv->current.pipe == pipe) {
			return priv;
		}
	}

	return NULL;
}

static uint64_t get_gpu_reg_base(uint16_t bdf)
{
	const struct pci_pdev *pdev = pci_find_pdev(bdf);
	return pdev->bars[0].phy_bar;
}

static int32_t fill_crc_record(struct telltale_private_data *priv)
{
	struct telltale_crc_regs *record = &priv->records[priv->next_record++ % MAX_RECORDS_PER_PIPE];
	uint64_t reg_base = priv->reg + PIPE_CRC_BASE(priv->current.pipe);

	record->ctrl = mmio_read32(hpa2hva(reg_base + PIPE_CRC_CTL));
	record->pos = mmio_read32(hpa2hva(reg_base + PIPE_CRC_REGIONAL_POS));
	record->size = mmio_read32(hpa2hva(reg_base + PIPE_CRC_REGIONAL_SIZE));
	record->val = mmio_read32(hpa2hva(reg_base + PIPE_CRC_RES));

	return 0;
}

static int32_t change_crc_region(struct telltale_private_data *priv)
{
	struct display_region *region = &priv->regions[priv->next_region++];
	uint64_t reg_base = priv->reg + PIPE_CRC_BASE(priv->current.pipe);

	stac();
	uint32_t val = mmio_read32(hpa2hva(reg_base + PIPE_CRC_REGIONAL_SIZE));
	val &= ~PIPE_CRC_REGIONAL_SIZE_Y_MASK;
	val |= (region->height & PIPE_CRC_REGIONAL_SIZE_Y_MASK) << PIPE_CRC_REGIONAL_SIZE_Y_SHIFT;
	val &= ~PIPE_CRC_REGIONAL_SIZE_X_MASK;
	val |= (region->width & PIPE_CRC_REGIONAL_SIZE_X_MASK) << PIPE_CRC_REGIONAL_SIZE_X_SHIFT;
	mmio_write32(val, hpa2hva(reg_base + PIPE_CRC_REGIONAL_SIZE));

	val = mmio_read32(hpa2hva(reg_base + PIPE_CRC_REGIONAL_POS));
	val &= ~PIPE_CRC_REGIONAL_POS_Y_MASK;
	val |= (region->y & PIPE_CRC_REGIONAL_POS_Y_MASK) << PIPE_CRC_REGIONAL_POS_Y_SHIFT;
	val &= ~PIPE_CRC_REGIONAL_POS_X_MASK;
	val |= (region->x & PIPE_CRC_REGIONAL_POS_X_MASK) << PIPE_CRC_REGIONAL_POS_X_SHIFT;
	mmio_write32(val, hpa2hva(reg_base + PIPE_CRC_REGIONAL_POS));

	val = mmio_read32(hpa2hva(reg_base + PIPE_CRC_CTL));
	val &= ~PIPE_CRC_SOURCE_MASK;
	val |= PIPE_CRC_SOURCE_DMUX << PIPE_CRC_SOURCE_SHIFT;
	val |= PIPE_CRC_ENABLE;
	mmio_write32(val, hpa2hva(reg_base + PIPE_CRC_CTL));
	clac();

	if (priv->next_region == priv->current.region_cnt) {
		priv->next_region = 0;
	}

	return 0;
}

static int32_t cb_on_vblank(void *data)
{
	struct telltale_private_data *priv = (struct telltale_private_data *)data;

	fill_crc_record(priv);

	/* may fail ? */
	change_crc_region(priv);

	return 0;
}

int32_t telltale_enable_crc(void *data)
{
	int i;
	struct telltale_enable_crc_data *desc = (struct telltale_enable_crc_data *)data;
	struct telltale_private_data *priv = get_priv_data(desc->bdf, desc->pipe);

	if (!priv) {
		return -EINVAL;
	}
	if (priv->enabled) {
		return -EBUSY;
	}

	memset(priv, 0, sizeof(*priv));
	priv->current = *desc;
	for (i = 0; i < desc->region_cnt; i++) {
		priv->regions[i] = desc->regions[i];
	}
	priv->reg = get_gpu_reg_base(priv->current.bdf);
	/* TODO check return value */
	(void)register_diagnostics_on_msi(priv->current.bdf, cb_on_vblank, priv);
	priv->enabled = 1;

	return 0;
}

int32_t telltale_disable_crc(void *data)
{
	struct telltale_disable_crc_data *desc = (struct telltale_disable_crc_data *)data;
	struct telltale_private_data *priv = get_priv_data(desc->bdf, desc->pipe);

	if (!priv || !priv->enabled) {
		return -EINVAL;
	}

	(void)unregister_diagnostics_on_msi(priv->current.bdf);
	priv->enabled = 0;

	return 0;
}

int32_t telltale_get_crc(void *data)
{
	struct telltale_get_crc_data *desc = (struct telltale_get_crc_data *)data;
	struct telltale_private_data *priv = get_priv_data(desc->bdf, desc->pipe);
	uint32_t i, next = priv->next_record - 1;

	for (i = 0; i < desc->frames;) {
		desc->records[i++] = priv->records[next-- % MAX_RECORDS_PER_PIPE];
	}

	return 0;
}
