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

#define _GNU_SOURCE
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

int flash_fd;

/* also known as writenfi.c. compile without ARCH, with CHECK_MODEL */

// #define ARCH 8000 /* can be autodetected, but only if CHECK_MODEL is 1 */
#define CHECK_MODEL 1

#define NUM_STAGES 4

int nand_read_spare(unsigned char *dst, unsigned int address);
int nand_read_page(unsigned char *dst, unsigned int address);
int nand_write_sector(int addr, unsigned char *data);
int nand_erase_page(int addr);

#ifdef ARCH
#if ARCH == 8000
const int flash_size = 256 * 1024 * 1024;
const int eraseblock_size = 128 * 1024;
const int sector_size = 2048;
const int ecc_size = 64;
const int badblock_pos = 0;
#elif ARCH == 800
const int flash_size = 64 * 1024 * 1024;
const int eraseblock_size = 16 * 1024;
const int sector_size = 512;
const int ecc_size = 16;
const int badblock_pos = 5;
#else
#error "unknown arch!!!"
#endif
#else
#if !CHECK_MODEL
#error need check model if ARCH is not defined!
#endif

int flash_size;
int eraseblock_size;
int sector_size;
int ecc_size;
int badblock_pos;

#endif

// oob layouts to pass into the kernel as default
struct nand_oobinfo none_oobinfo = {
	.useecc = MTD_NANDECC_OFF,
};

int main(int argc, char **argv)
{
	int oobinfochanged = 0;
	int ret = 0;
	struct nand_oobinfo old_oobinfo;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <nfi>\n", *argv);
		return 1;
	}

	flash_fd = open("/dev/mtd/0", O_RDWR);
	if (flash_fd < 0)
	{
		flash_fd = open("/dev/mtd0", O_RDWR);
		if (flash_fd < 0)
		{
			perror("/dev/mtd/0");
			return 1;
		}
	}

	ret = ioctl(flash_fd, MTDFILEMODE, (void *) MTD_MODE_RAW);
	if (ret == 0) {
		oobinfochanged = 2;
	} else {
		switch (errno) {
		case ENOTTY:
			if (ioctl (flash_fd, MEMGETOOBSEL, &old_oobinfo) != 0) {
				perror ("MEMGETOOBSEL");
				close (flash_fd);
				exit (1);
			}
			if (ioctl (flash_fd, MEMSETOOBSEL, &none_oobinfo) != 0) {
				perror ("MEMSETOOBSEL");
				close (flash_fd);
				exit (1);
			}
			oobinfochanged = 1;
			break;
		default:
			perror ("MTDFILEMODE");
			close (flash_fd);
			exit (1);
		}
	}


	printf("*** raw flash write tool. Don't have bad sectors or it won't work.\n");
	printf("*** uncompressing..."); fflush(stdout);
	ret = 0;

#if 0
	static unsigned char dst[1<<25];
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0)
	{
		perror(argv[1]);
		exit(1);
	}
	
	int dstlen;
	dstlen = read(fd, dst, 1<<25);
	printf(" ok!\n");
#endif
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0)
	{
		perror(argv[1]);
		ret = 1;
		goto restoreoob;
	}
	
	int dstlen = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	static unsigned char *dst;
	dst = mmap(0, dstlen, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	
	if (!dst)
	{
		printf("alles schlecht.\n");
		ret = 1;
		goto restoreoob;
	}

#if CHECK_MODEL
	else if (strncmp((const char*)dst, "NFI1", 4))
	{
		printf("no NFI header found...abort flashing!\n");
		ret = 1;
		goto restoreoob;
	}
	else
	{
		FILE *f = fopen("/proc/stb/info/model", "r");
		char model[255];
		if (f)
		{
			size_t rd = fread(model, 1, 255, f);
			if (rd)
				model[rd-1] = 0; /* remove \n */
			if (strcmp(model, (const char*)dst+4))
			{
				printf("nfi file not for this platform... abort flashing!\n");
				exit(1);
			}
		}
		else
		{
			printf("/proc/stb/info/model doesn't exist... abort flashing!");
			ret = 1;
			goto restoreoob;
		}
#ifndef ARCH
		if (!strcmp(model, "dm800") || !strcmp(model, "dm500hd"))
		{
			flash_size = 64 * 1024 * 1024;
			eraseblock_size = 16 * 1024;
			sector_size = 512;
			ecc_size = 16;
			badblock_pos = 5;
		} else if (!strcmp(model, "dm7025"))
		{
			flash_size = 32 * 1024 * 1024;
			eraseblock_size = 16 * 1024;
			sector_size = 512;
			ecc_size = 16;
			badblock_pos = 5;
		} else if (!strcmp(model, "dm8000"))
		{
			flash_size = 256 * 1024 * 1024;
			eraseblock_size = 128 * 1024;
			sector_size = 2048;
			ecc_size = 64;
			badblock_pos = 0;
		} else
		{
			printf("unsupported host model!\n");
			ret = 1;
			goto restoreoob;
		}
#endif
	}
#endif

	dst += 32; // skip header

	printf(" ok!\n");
	
	printf("*** FLASH_GEOM: %08x %08x %08x %08x %08x\n", flash_size, eraseblock_size, sector_size, ecc_size, badblock_pos);

	printf("*** CRC check ignored!\n");
	
	int stage_offset[NUM_STAGES], stage_size[NUM_STAGES];
	
	int current_offset = 4;
	unsigned long *part = 0;
	int i;
	for (i=0; i<NUM_STAGES; ++i)
	{
		stage_size[i] = ntohl(*(long*)(dst + current_offset));
		stage_offset[i] = current_offset + 4;
		current_offset += stage_size[i] + 4;
		if (!i)
			part = (unsigned long*)(dst + stage_offset[0]);
			
		printf("*** stage %d: ..%08x | %08x..%08x (%d bytes)\n", i, i ? (ntohl(part[i-1])) : 0, stage_offset[i], stage_offset[i] + stage_size[i], stage_size[i]);
	}
	
	if (current_offset > dstlen)
	{
		printf("!!! paritioning is wrong.\n");
		ret = 1;
		goto restoreoob;
	}
	
	int wr = 0;
	
	int overwrite = 0;
	
	int current_stage = 1;
	int data_ptr = 0;

//	int marker_pos = ntohl(part[1]) - eraseblock_size;
//	printf("marker_pos initial: %08x\n", marker_pos);
	
	int total = 0;
	printf("*** bad block list:"); fflush(stdout);
	for (i=0; i<flash_size; i+=eraseblock_size)
	{
		int have_badblock = 0;
		unsigned char oob[ecc_size];
		if (nand_read_spare(oob, i))
		{
			ret = 1;
			goto restoreoob;
		}
		if (oob[badblock_pos] != 0xFF)
			have_badblock = 1;
		if (nand_read_spare(oob, i + sector_size))
		{
			ret = 1;
			goto restoreoob;
		}
		if (oob[badblock_pos] != 0xFF)
			have_badblock = 1;
		have_badblock = 0;
		if (have_badblock)
		{
			printf(" %08x", i); fflush(stdout);
			++total;
		}
	}
	if (total)
		printf(" (%d blocks, %d kB total)\n", total, total * eraseblock_size/ 1024);
	else
		printf(" none\n");

#if 0	
	printf("marker_pos: %08x\n", marker_pos);

	while (1)
	{
		int have_badblock = 0;
		unsigned char oob[ecc_size];
		nand_read_spare(oob, marker_pos);
		if (oob[badblock_pos] != 0xFF)
			have_badblock = 1;
		nand_read_spare(oob, marker_pos + sector_size);
		if (oob[badblock_pos] != 0xFF)
			have_badblock = 1;
		if (have_badblock)
		{
			marker_pos -= eraseblock_size;
			printf("*** adjusting marker position to %08x because of badblock\n", marker_pos);
		} else
			break;
	}
#endif

	printf("*** writing"); fflush(stdout);

	i = 0;
	while (1)
	{
		int res;

		int have_badblock = 0;

		if (i >= ntohl(part[current_stage-1]))
		{
//			printf("i >= %d (%d)\n", current_stage, part[current_stage-1]);
			if (data_ptr < stage_size[current_stage])
			{
				printf("!!! too much data (or bad sectors) in partition %d (end: %08x, pos: %08x)\n", 
					current_stage, ntohl(part[current_stage-1]), i);
				ret = 1;
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

		unsigned char oob[ecc_size];
				/* erase page */
		if (!(i % eraseblock_size))  /* at each eraseblock */
		{
///			printf("%08x / %08x - %d - %08x / %08x\n", i, part[current_stage-1], current_stage, data_ptr, stage_size[current_stage]);
			if (nand_read_spare(oob, i))
			{
				ret = 1;
				goto restoreoob;
			}

			if ((oob[badblock_pos] != 0xFF) && !overwrite)
				have_badblock = 1;
			if (nand_read_spare(oob, i + sector_size))
			{
				ret = 1;
				goto restoreoob;
			}
			if ((oob[badblock_pos] != 0xFF) && !overwrite)
				have_badblock = 1;

			if (!have_badblock)
			{
				res = nand_erase_page(i);
				if (res & 1)
				{
					printf("\n!!! erase failed at %08x: %02x\n", i, res);
					ret = 1;
					goto restoreoob;
				}
			}
		}
		
		if (have_badblock)
		{
			printf("*"); fflush(stdout);
			i += eraseblock_size; /* skip this eraseblock - it's broken. */
			continue;
		}
		
		unsigned char sector[sector_size + ecc_size];
		
			/* we still have data to write, no need to generate null blocks */
		if (data_ptr < stage_size[current_stage])
		{
			memcpy(sector, dst + stage_offset[current_stage] + data_ptr, sector_size + ecc_size);
			data_ptr += sector_size + ecc_size;
		} else
		{
#if 0
			if (i == marker_pos)   /* last eraseblock of boot partition */
			{
					/* TODO: handle case where this sector is broken */
				printf("M"); fflush(stdout);
				memset(sector, 0xFF, sector_size+8);
				memcpy(sector + sector_size + 8, "\xde\xad\xbe\xef\xff\xff\xff\xff", 8);
			} else 
#endif
			if (current_stage == 1)	 /* generate hevers empty block */
			{
				memset(sector, 0xFF, sector_size+ecc_size);
			} else /* generate jffs2 empty block */
			{
				memset(sector, 0xFF, sector_size+ecc_size);
				if (!((i >> 9) & 31))
					memcpy(sector + sector_size + ((ecc_size == 16) ? 8 : 2), "\x19\x85\x20\x03\x00\x00\x00\x08", 8);
			}
		}
		
		res = nand_write_sector(i, sector);
		if (res & 1)
		{
			printf("\n!!! write failed at %08x: %02x\n", i, res);
			ret = 1;
			goto restoreoob;
		}

/*		unsigned char buffer[sector_size + ecc_size];
		nand_read_page(buffer, i);

		if (memcmp(sector, buffer, sector_size + ecc_size))
		{
			printf("\n!!! verify failed at %08x\n", i);
			exit (1);
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
		
		i += sector_size;
	}
	
	printf(" ok!\n");
	printf("*** done!\n");

restoreoob:
	if (oobinfochanged == 1) {
		if (ioctl (flash_fd, MEMSETOOBSEL, &old_oobinfo) != 0) {
			perror ("MEMSETOOBSEL");
			close (flash_fd);
			ret = 1;
		}
	}

	return ret;
}

int nand_erase_page(int addr)
{
	erase_info_t erase;
	erase.length = eraseblock_size;
	erase.start = addr;
	if (ioctl(flash_fd, MEMERASE, &erase) != 0)
	{
		printf("erase: %08x\n", erase.start);
		perror("MEMERASE");
		return 1;
	}
	return 0;
}

int nand_write_sector(int addr, unsigned char *data)
{
#if 1
	struct mtd_oob_buf oob;
	oob.start = addr;
	oob.length = ecc_size;
	oob.ptr = data + sector_size;
	if (ioctl(flash_fd, MEMWRITEOOB, &oob) != 0)
	{
		perror("MEMWRITEOOB");
		return 1;
	}
	pwrite(flash_fd, data, sector_size, addr);
#endif
	return 0;
}

int nand_read_spare(unsigned char *dst, unsigned int address)
{
	struct mtd_oob_buf oob;
	oob.start = address;
	oob.length = ecc_size;
	oob.ptr = dst;
	if (ioctl(flash_fd, MEMREADOOB, &oob) != 0)
	{
		perror("MEMREADOOB");
		return 1;
	}
	return 0;
}

int nand_read_page(unsigned char *dst, unsigned int address)
{
	if (pread(flash_fd, dst, sector_size, address) != sector_size)
	{
		perror("read");
		return 1;
	}
	return nand_read_spare(dst + sector_size, address);
}
