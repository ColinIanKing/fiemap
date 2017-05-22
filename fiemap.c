/*
 * Copyright (C) 2010-2017 Canonical
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

/*
 *  Author Colin Ian King,  colin.king@canonical.com
 *
 *  Add support of multi-chunk fiemaps. Oleksandr Suvorov, cryosay@gmail.com
 */



#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/fs.h>
#include <linux/fiemap.h>

void syntax(char **argv)
{
	fprintf(stderr, "%s [filename]...\n",argv[0]);
}

struct fiemap *read_fiemap(int fd)
{
	struct fiemap *fiemap = NULL;
	struct fiemap *result_fiemap = NULL;
	struct fiemap *fm_tmp; /* need to store pointer on realloc */
	int extents_size;
	struct stat statinfo;
	__u32 result_extents = 0;
	__u64 fiemap_start = 0, fiemap_length;

	if (fstat(fd, &statinfo) != 0) {
		fprintf(stderr, "Can't determine file size [%d] (%s)\n",
				errno, strerror(errno));
		return NULL;
	}
	fiemap_length = statinfo.st_size;

	fiemap = malloc(sizeof(struct fiemap));
	if (fiemap == NULL) {
		fprintf(stderr, "Out of memory allocating fiemap\n");
		return NULL;
	}

	result_fiemap = malloc(sizeof(struct fiemap));
	if (result_fiemap == NULL) {
		fprintf(stderr, "Out of memory allocating fiemap\n");
		goto fail_cleanup;
	}

	/*  XFS filesystem has incorrect implementation of fiemap ioctl and
	 *  returns extents for only one block-group at a time, so we need
	 *  to handle it manually, starting the next fiemap call from the end
	 *  of the last extent
	 */
	while (fiemap_start < fiemap_length) {
		fprintf(stderr, "DEBUG: %llu -> %llu\n", fiemap_start, fiemap_length);

		memset(fiemap, 0, sizeof(struct fiemap));

		fiemap->fm_start = fiemap_start;
		fiemap->fm_length = fiemap_length;
		fiemap->fm_flags = FIEMAP_FLAG_SYNC;

		/* Find out how many extents there are */
		if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) {
			fprintf(stderr, "fiemap ioctl() failed\n");
			goto fail_cleanup;
		}

		/* Nothing to process */
		if (fiemap->fm_mapped_extents == 0)
			break;

		/* Result fiemap have to hold all the extents for the hole file
		 */

		/* Read in the extents */
		extents_size = sizeof(struct fiemap_extent) *
		                      (fiemap->fm_mapped_extents);

		/* Resize fiemap to allow us to read in the extents */
		fm_tmp = realloc(fiemap,
				 sizeof(struct fiemap) + extents_size);
		if (!fm_tmp) {
			fprintf(stderr, "Out of memory allocating fiemap\n");
			goto fail_cleanup;
		}
		fiemap = fm_tmp;

		memset(fiemap->fm_extents, 0, extents_size);
		fiemap->fm_extent_count = fiemap->fm_mapped_extents;
		fiemap->fm_mapped_extents = 0;

		if (ioctl(fd, FS_IOC_FIEMAP, fiemap) < 0) {
			fprintf(stderr, "fiemap ioctl() failed\n");
			goto fail_cleanup;
		}

		extents_size = sizeof(struct fiemap_extent) *
		                      (result_extents +
				       fiemap->fm_mapped_extents);

		/* Resize result_fiemap to allow us to read in the extents */
		fm_tmp = realloc(result_fiemap,
				 sizeof(struct fiemap) + extents_size);
		if (!fm_tmp) {
			fprintf(stderr, "Out of memory allocating fiemap\n");
			goto fail_cleanup;
		}
		result_fiemap = fm_tmp;

		memcpy(result_fiemap->fm_extents + result_extents,
		       fiemap->fm_extents,
		       sizeof(struct fiemap_extent) *
		       fiemap->fm_mapped_extents);

		result_extents += fiemap->fm_mapped_extents;

		fiemap_start = fiemap->fm_extents[fiemap->fm_mapped_extents -
						  1].fe_logical +
			       fiemap->fm_extents[fiemap->fm_mapped_extents -
						  1].fe_length;

		if (fiemap->fm_extents[fiemap->fm_mapped_extents - 1].fe_flags &
		    FIEMAP_EXTENT_LAST)
			break;
	}

	result_fiemap->fm_mapped_extents = result_extents;
	free(fiemap);

	return result_fiemap;

fail_cleanup:
	if (result_fiemap)
		free(result_fiemap);

	if (fiemap)
		free(fiemap);

	return NULL;
}

void dump_fiemap(struct fiemap *fiemap, char *filename)
{
	int i;

	printf("File %s has %d extents:\n",filename, fiemap->fm_mapped_extents);

	printf("#\tLogical          Physical         Length           Flags\n");
	for (i = 0; i < fiemap->fm_mapped_extents; i++) {
		printf("%d:\t%-16.16llx %-16.16llx %-16.16llx %-4.4x\n",
			i,
			fiemap->fm_extents[i].fe_logical,
			fiemap->fm_extents[i].fe_physical,
			fiemap->fm_extents[i].fe_length,
			fiemap->fm_extents[i].fe_flags);
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	int i;

	if (argc < 2) {
		syntax(argv);
		exit(EXIT_FAILURE);
	}

	for (i = 1; i < argc; i++) {
		int fd;

		if ((fd = open(argv[i], O_RDONLY)) < 0) {
			fprintf(stderr, "Cannot open file %s\n", argv[i]);
		}
		else {
			struct fiemap *fiemap;

			if ((fiemap = read_fiemap(fd)) != NULL)
				dump_fiemap(fiemap, argv[i]);
			close(fd);
		}
	}
	exit(EXIT_SUCCESS);
}

