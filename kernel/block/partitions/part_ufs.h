#ifndef _PART_UFS_H_
#define _PART_UFS_H_

#define UFS_PARTITION_MAGIC	0x55465350	/* "UFSP" */

struct ufs_partition {
	__be32	signature;		/* expected to be UFS_PARTITION_MAGIC   */
	__be32	map_count;		/* blocks in partition map   */
	__be32	start_block;	/* abs. starting block # of partition    */
	__be32	block_count;	/* number of blocks in partition  */
	char	name[32];		/* partition name */
	char	type[32];		/* string type description */
};

#define UFS_DRIVER_MAGIC	0x55465344	/* "UFSD" */

/* Driver descriptor structure, in block 0 */
struct ufs_driver_desc {
	__be32	signature;		/* expected to be UFS_DRIVER_MAGIC  */
	__be16	blk_size;		/* block size of device */
	__be16	pad;			/* reserved */
	__be32	blk_count;		/* number of blocks on device */
	__be16	dev_type;		/* device type */
	__be16	dev_id;			/* device id */
	__be32	data;			/* reserved */
	__be16	version;		/* version number of partition table */
	__be16	drvr_cnt;		/* number of blocks reserved for partition table */
};

int ufs_partition(struct parsed_partitions *state);

#endif
