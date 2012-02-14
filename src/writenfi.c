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

#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>

#include <arpa/inet.h>
#include <asm/types.h>
#include <mtd/mtd-user.h>

#include "nand.h"

#define NUM_STAGES 4

// oob layouts to pass into the kernel as default
static struct nand_oobinfo none_oobinfo = {
	.useecc = MTD_NANDECC_OFF,
};

enum input_mode {
	IM_MMAP,
	IM_READ,
};

static char *file_getline(const char *filename)
{
	char *line = NULL;
	size_t n = 0;
	ssize_t ret;
	FILE *f;

	f = fopen(filename, "r");
	if (f == NULL) {
	        perror(filename);
	        return NULL;
	}

	ret = getline(&line, &n, f);

	fclose(f);

	if (ret < 0)
	        return NULL;

	while (ret-- > 0) {
	        if ((line[ret] != '\n') &&
	            (line[ret] != '\r'))
	                break;
	        line[ret] = '\0';
	}

	return line;
}

static bool safe_read(int fd, unsigned char *buf, size_t count)
{
	size_t pos = 0;
	ssize_t ret;

	while (pos < count) {
		errno = 0;
		ret = read(fd, &buf[pos], count - pos);
		if (ret < 0) {
			if (errno != EINTR)
				return false;
		} else {
			pos += ret;
		}
	}

	return true;
}

int main(int argc, char **argv)
{
	int oobinfochanged = 0;
	struct nand_oobinfo old_oobinfo;
	enum input_mode input_mode = IM_MMAP;
	unsigned char *mem = MAP_FAILED;
	int ret = EXIT_FAILURE;
	struct nand *n = NULL;
	char *model = NULL;
	const char *filename;
	unsigned char *dst;
	struct stat st;
	int fd;
	int hw_ecc=0; // 0 = no hw ecc, 1 = hw ecc not for 2nd stage, 2 = hw ecc for all

	if (argc != 2) {
		fprintf(stderr, "usage: %s <filename.nfi>\n", argv[0]);
		return EXIT_FAILURE;
	}

	filename = argv[1];

	printf("*** raw flash write tool. Don't have unknown bad sectors or it won't work.\n");
	printf("*** uncompressing..."); fflush(stdout);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		return EXIT_FAILURE;
	}

	if (fstat(fd, &st) < 0) {
		perror(filename);
		goto err;
	}

	if (st.st_size < 32) {
		fprintf(stderr, "invalid input file size\n");
		goto err;
	}

	mem = mmap(0, st.st_size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (mem == MAP_FAILED) {
		fprintf(stderr, "mmap failed, falling back to read()\n");
		input_mode = IM_READ;
		mem = malloc(st.st_size);
		if (mem == NULL) {
			perror("malloc");
			goto err;
		}
		if (!safe_read(fd, mem, st.st_size)) {
			perror("read");
			goto err;
		}
	}

	n = nand_open();
	if (n == NULL) {
		fprintf(stderr, "Could not open NAND\n");
		goto err;
	}

	if (memcmp(mem, "NFI", 3)) {
		fprintf(stderr, "no NFI header found... abort flashing!\n");
		goto err;
	}

	model = file_getline("/proc/stb/info/model");
	if (model == NULL) {
		fprintf(stderr, "/proc/stb/info/model doesn't exist... abort flashing!");
		goto err;
	}

	if (!strcmp(model, "dm800") || !strcmp(model, "dm500hd") || !strcmp(model, "dm800se"))
		hw_ecc = 1;
	else if (!strcmp(model, "dm7025") || !strcmp(model, "dm8000"))
		;
	else if (!strcmp(model, "dm7020hd"))
		hw_ecc = 2;
	else {
		fprintf(stderr, "unsupported model %s!\n", model);
		goto err;
	}

	if (strncmp(model, (const char *)(mem + 4), 28)) {
		fprintf(stderr, "nfi file not for this platform... abort flashing!\n");
		goto err;
	}

	printf(" ok!\n");

	/*
	 * check if this model supports this image
	 * (DM7020HD only supports images with NFI2 header)
	 *
	 */
	if (!strncmp((const char *)mem, "NFI1", 4) && hw_ecc != 2)
		hw_ecc = 0; // NFI1 images not use hw ecc
	else if (strncmp((const char*)mem, "NFI2", 4) || hw_ecc < 1) {
		fprintf(stderr, "%c%c%c%c is no valid header for %s ...abort flashing!\n",
			mem[0], mem[1], mem[2], mem[3], model);
		goto err;
	}

	if (ioctl(n->fd, MTDFILEMODE, (void *) MTD_MODE_RAW) == 0) {
		oobinfochanged = 2;
	} else {
		switch (errno) {
		case ENOTTY:
			if (ioctl(n->fd, MEMGETOOBSEL, &old_oobinfo) != 0) {
				perror("MEMGETOOBSEL");
				close(n->fd);
				return EXIT_FAILURE;
			}
			if (ioctl(n->fd, MEMSETOOBSEL, &none_oobinfo) != 0) {
				perror("MEMSETOOBSEL");
				close(n->fd);
				return EXIT_FAILURE;
			}
			oobinfochanged = 1;
			break;
		default:
			perror("MTDFILEMODE");
			close(n->fd);
			return EXIT_FAILURE;
		}
	}

	dst = mem + 32; // skip header

	printf("*** FLASH_GEOM: %#zx %#zx %#zx %#zx %#zx\n", n->flash_size, n->erase_block_size, n->sector_size, n->spare_size, n->bad_block_pos);
	printf("*** CRC check ignored!\n");
	
	unsigned int stage_offset[NUM_STAGES], stage_size[NUM_STAGES];
	unsigned int current_offset = 4;
	unsigned long *part = NULL;
	unsigned int i;

	for (i=0; i<NUM_STAGES; ++i)
	{
		stage_size[i] = ntohl(*(long*)(dst + current_offset));
		stage_offset[i] = current_offset + 4;
		current_offset += stage_size[i] + 4;
		if (!i)
			part = (unsigned long*)(dst + stage_offset[0]);
			
		printf("*** stage %d: ..%08x | %08x..%08x (%d bytes)\n", i, i ? (ntohl(part[i-1])) : 0, stage_offset[i], stage_offset[i] + stage_size[i], stage_size[i]);
	}
	
	if (current_offset > st.st_size)
	{
		printf("!!! paritioning is wrong.\n");
		goto restoreoob;
	}
	
	int wr = 0;
	int current_stage = 1;
	int data_ptr = 0;

//	int marker_pos = ntohl(part[1]) - n->erase_block_size;
//	printf("marker_pos initial: %08x\n", marker_pos);
	
	size_t total = 0;
	printf("*** bad block list:"); fflush(stdout);
	for (i = 0; i < n->flash_size; i += n->erase_block_size)
	{
		int have_badblock = 0;
		unsigned char oob[n->spare_size];
		if (!nand_read_spare(n, i, oob))
			goto restoreoob;
		if (oob[n->bad_block_pos] != 0xFF)
			have_badblock = 1;
		if (!nand_read_spare(n, i + n->sector_size, oob))
			goto restoreoob;
		if (oob[n->bad_block_pos] != 0xFF)
			have_badblock = 1;
		if (have_badblock)
		{
			printf(" %08x", i); fflush(stdout);
			++total;
		}
	}
	if (total)
		printf(" (%zd blocks, %zd kB total)\n", total, total * n->erase_block_size/ 1024);
	else
		printf(" none\n");

	printf("*** writing"); fflush(stdout);

	i = 0;
	while (1)
	{
		int have_badblock = 0;

		int do_write = 1;

		if (i >= ntohl(part[current_stage-1]))
		{
//			printf("i >= %d (%d)\n", current_stage, part[current_stage-1]);
			if (data_ptr < stage_size[current_stage])
			{
				printf("!!! too much data (or bad sectors) in partition %d (end: %08x, pos: %08x)\n", 
					current_stage, ntohl(part[current_stage-1]), i);
				goto restoreoob;
			} else
			{
				current_stage++;
				if (!ntohl(part[current_stage-1]))
					break;
				if (current_stage == NUM_STAGES)
					break;
				data_ptr = 0;
				printf("\n*** partition %d: ", current_stage - 1);
				wr = 13;
			}
		}

		unsigned char oob[n->spare_size];
				/* erase page */
		if (!(i % n->erase_block_size))  /* at each eraseblock */
		{
///			printf("%08x / %08x - %d - %08x / %08x\n", i, part[current_stage-1], current_stage, data_ptr, stage_size[current_stage]);
			if (!nand_read_spare(n, i, oob))
				goto restoreoob;

			if (oob[n->bad_block_pos] != 0xFF)
				have_badblock = 1;
			if (!nand_read_spare(n, i + n->sector_size, oob))
				goto restoreoob;
			if (oob[n->bad_block_pos] != 0xFF)
				have_badblock = 1;

			if (!have_badblock)
			{
				if (!nand_erase_page(n, i)) {
					printf("\n!!! erase failed at %08x\n", i);
					goto restoreoob;
				}
			}
		}
		
		if (have_badblock)
		{
			printf("*"); fflush(stdout);
			i += n->erase_block_size; /* skip this eraseblock - it's broken. */
			continue;
		}
		
		unsigned char sector[n->sector_size + n->spare_size];
		
			/* we still have data to write, no need to generate null blocks */
		if (data_ptr < stage_size[current_stage]) {
			memcpy(sector, dst + stage_offset[current_stage] + data_ptr, n->sector_size + n->spare_size);
			data_ptr += n->sector_size + n->spare_size;
			if (hw_ecc > 1 || (current_stage > 1 && hw_ecc))
			{
				int x=0;
				for (; x < n->spare_size; x += 16)
				{
					/*
					 * force hw ecc bytes always to 0xFF
					 * this only works for broadcom hamming hw ECC
					 * this is needed on 7020hd because buildimage calcs for the 2nd stage loader
					 * a soft ecc... but the 7020hd use hardware ecc for all
					 *
					 */

/*					if (sector[n->sector_size+x+6] != 0xFF || sector[n->sector_size+x+7] != 0xFF || sector[n->sector_size+x+8] != 0xFF)
					{
						fprintf(stdout, "ECC bytes wrong at addr %08x/%d -> %02x %02x %02x\n", i, x,
							sector[n->sector_size+x+6], sector[n->sector_size+x+7], sector[n->sector_size+x+8]);
					}*/

					sector[n->sector_size+x+6] = 0xFF;
					sector[n->sector_size+x+7] = 0xFF;
					sector[n->sector_size+x+8] = 0xFF;
				}
			}
		} else {
			memset(sector, 0xFF, n->sector_size+n->spare_size);
			if (current_stage != 1)
				do_write = 0; // its not needed to write jffs2 emtpy blocks...
		}

		if (do_write && !nand_write_sector(n, i, sector)) {
			printf("\n!!! write failed at %08x\n", i);
			goto restoreoob;
		}

/*		unsigned char buffer[n->sector_size + n->spare_size];
		nand_read_page(buffer, i);

		if (memcmp(sector, buffer, n->sector_size + n->spare_size))
		{
			printf("\n!!! verify failed at %08x\n", i);
			return EXIT_FAILURE;
		}*/

		if (!(i&0x1FFFF))
		{
			printf("."); fflush(stdout);
			wr++;
			if (wr == 59)
			{
				wr = 0;
				printf("\n           ");
			}
		}
		
		i += n->sector_size;
	}
	
	printf(" ok!\n");
	printf("*** done!\n");
	ret = EXIT_SUCCESS;

restoreoob:
	if (oobinfochanged == 1) {
		if (ioctl(n->fd, MEMSETOOBSEL, &old_oobinfo) != 0) {
			perror("MEMSETOOBSEL");
			close(n->fd);
		}
	}

err:
	if (input_mode == IM_MMAP) {
		if (mem != MAP_FAILED)
			munmap(mem, st.st_size);
	} else if (input_mode == IM_READ) {
		if (mem != NULL)
			free(mem);
	}

	if (fd != -1)
		close(fd);

	if (model != NULL)
		free(model);

	return ret;
}
