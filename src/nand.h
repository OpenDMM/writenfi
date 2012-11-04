#ifndef _NAND_H_
#define _NAND_H_ 1

#include <stdbool.h>

struct nand {
	int fd;
	size_t bad_block_pos;
	size_t spare_size;
	size_t erase_block_size;
	size_t flash_size;
	size_t sector_size;
};

struct nand *nand_open(int device);
bool nand_erase_page(struct nand *n, unsigned int addr);
bool nand_write_sector(struct nand *n, unsigned int addr, const unsigned char *data);
bool nand_read_spare(struct nand *n, unsigned int addr, unsigned char *data);
bool nand_read_page(struct nand *n, unsigned int addr, unsigned char *data);

#endif
