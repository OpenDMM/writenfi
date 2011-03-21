/*
 * mywritenand - copies NAND files to flash.
 *   (c) Felix Domke <tmbinc@elitedvb.net>
 *
 * Licensed under the GPL
 *
 * WARNING: this tool writes to your flash. Incorrect usage or bugs
 * can corrupt the OOB-information of your flash, which is not easy
 * to correct. If you don't know what this means and how to correct
 * it, please don't use.
 *
 * Also, this works on 7020 only, because it does not check the NFI1-
 * header. To make this work on other platforms, just ignore the first
 * 32 bytes.
 */

#ifdef HAVE_CONFIG_H
#include "writenfi_config.h"
#endif

#include <fcntl.h>
#include <mtd/mtd-user.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "nand.h"

static bool nand_get_info(struct nand *n)
{
	mtd_info_t info;

	if (ioctl(n->fd, MEMGETINFO, &info) != 0) {
		perror("MEMGETINFO");
		return false;
	}

	if (info.type != MTD_NANDFLASH) {
		fprintf(stderr, "Not a NAND flash\n");
		return false;
	}

	if (!(info.flags & MTD_WRITEABLE)) {
		fprintf(stderr, "NAND is not writeable\n");
		return false;
	}

	n->flash_size = info.size;
	n->erase_block_size = info.erasesize;
	n->sector_size = info.writesize;
	n->spare_size = info.oobsize;

	if (n->sector_size == 512)
		n->bad_block_pos = 5;
	else
		n->bad_block_pos = 0;

	return true;
}

struct nand *nand_open(void)
{
	static struct nand n;

	n.fd = open("/dev/mtd0", O_RDWR);
	if (n.fd < 0) {
		n.fd = open("/dev/mtd/0", O_RDWR);
		if (n.fd < 0) {
			perror("/dev/mtd0 or /dev/mtd/0");
			return NULL;
		}
	}

	if (!nand_get_info(&n)) {
		close(n.fd);
		return NULL;
	}

	return &n;
}

bool nand_erase_page(struct nand *n, unsigned int addr)
{
	erase_info_t erase = {
		.length = n->erase_block_size,
		.start = addr,
	};

	if (ioctl(n->fd, MEMERASE, &erase) != 0) {
		perror("MEMERASE");
		return false;
	}

	return true;
}

bool nand_write_sector(struct nand *n, unsigned int addr, const unsigned char *data)
{
	ssize_t ret;
	struct mtd_oob_buf oob = {
		.start = addr,
		.length = n->spare_size,
		.ptr = (unsigned char *)data + n->sector_size,
	};

	if (ioctl(n->fd, MEMWRITEOOB, &oob) != 0) {
		perror("MEMWRITEOOB");
		return false;
	}

	ret = pwrite(n->fd, data, n->sector_size, addr);
	if (ret < 0) {
		perror("pwrite");
		return false;
	}

	return (ret == n->sector_size);
}

bool nand_read_spare(struct nand *n, unsigned int addr, unsigned char *data)
{
	struct mtd_oob_buf oob = {
		.start = addr,
		.length = n->spare_size,
		.ptr = data,
	};

	if (ioctl(n->fd, MEMREADOOB, &oob) != 0) {
		perror("MEMREADOOB");
		return false;
	}

	return true;
}

bool nand_read_page(struct nand *n, unsigned int addr, unsigned char *data)
{
	ssize_t ret;

	ret = pread(n->fd, data, n->sector_size, addr);
	if (ret < 0) {
		perror("pread");
		return false;
	}

	return (ret == n->sector_size);
}
