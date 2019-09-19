#include <linux/ctype.h>
#include "check.h"
#include "part_ufs.h"

int ufs_partition(struct parsed_partitions *state)
{
	int slot = 1;
	Sector sect;
	unsigned char *data;
	int blk, blocks_in_map;
	unsigned secsize;
	struct ufs_partition *part;
	struct ufs_driver_desc *md;

	printk("ufs_partition()\n");
	/* Get 0th block and look at the first partition map entry. */
	md = read_part_sector(state, 0, &sect);
	if (!md)
		return -1;

	if (be32_to_cpu(md->signature) != UFS_DRIVER_MAGIC) {
		//can not found the partiton map!
		printk("0x%x\n",be32_to_cpu(md->signature));
		put_dev_sector(sect);
		return 0;
	}

	blocks_in_map = be16_to_cpu(md->drvr_cnt);
	secsize = be16_to_cpu(md->blk_size);

	put_dev_sector(sect);
	data = read_part_sector(state, 1, &sect);
	if (!data)
		return -1;

	part = (struct ufs_partition *)data;
	if (be32_to_cpu(part->signature) != UFS_PARTITION_MAGIC) {
		put_dev_sector(sect);
		return 0;		/* not a ufs disk */
	}
	printk(" [ufs]");
	for (blk = 1; blk <= blocks_in_map; blk++) {
		put_dev_sector(sect);
		data = read_part_sector(state, blk, &sect);
		if (!data)
			return -1;

		part = (struct ufs_partition *)data;
		if (part->signature != UFS_PARTITION_MAGIC)
			break;

		printk("Start_block=%d, block_count=%d\n", be32_to_cpu(part->start_block), be32_to_cpu(part->block_count));
		put_partition(state, slot, be32_to_cpu(part->start_block), be32_to_cpu(part->block_count));
		strcpy(state->parts[slot].info.volname, part->name); /* put parsed partition name into state */

		slot++;
	}

	put_dev_sector(sect);
	printk("\n");
	return 1;
}
