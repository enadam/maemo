/*
 * debacle.c -- extract and decode subimages of a FIASCO
 *
 * debacle is like flasher -u, but you can choose which subimage(s)
 * to unpack, and it will also lzo-decompress and resparse the extracted
 * subimage if necessary, making it possible to obtain the rootfs
 * in one go.
 *
 * Synopsis:
 *	debacle <FIASCO> [-t|-] <subimage>...
 *
 * <subimage>s are the names of subimages to extract, like "kernel"
 * or "rootfs".  A preceeding '-' will write to the standard output.
 * -t ("test") makes the program not to write anything anywhere.
 *
 * To build debacle, you'll need libfiasco-dev from flasher sources.
 */

/* Configuration */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS	64

/* Include files */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#include <lzo/lzo1x.h>
#include <fiasco/fiasco.h>
#include <fiasco/fiasco-read.h>

/* Standard definitions */
#define MAGIC_LZO_MAGIC		0xB8C3B410

/* Sparse image constants from sparse-image/resparse.h */
#define SPIMG_MAGIC		0xADAEA9A1
#define SPIMG_MAX_SEGS		1016
#define SPIMG_MAX_NAMELEN	11

/* Private variables */
static int Opt_test;

/* Program code */
/* Utilities */
static void xwrite(int fd, void const *buf, size_t sbuf)
{
	if (sbuf > 0 && !Opt_test)
		assert(write(fd, buf, sbuf) == sbuf);
} /* xwrite */

/* Write $nzeros to $fd, using lseek() if possible. */
static void zwrite(int fd, uint64_t nzeros)
{	/* Assume $seekable $fd by default,
	 * and unset it when we disconver the contrary. */
	static int seekable = 1;

	/* Reset state? */
	if (fd < 0)
	{
		seekable = 1;
		return;
	} else if (!nzeros)
		return;

	/* Try to punch a hole by seeking. */
	if (seekable && lseek64(fd, nzeros, SEEK_CUR) < 0)
	{
		assert(errno == ESPIPE);
		seekable = 0;
	}

	/* Fall back to writing zeroes. */
	if (!seekable)
	{
		lldiv_t div;
		char zeros[4096];

		memset(zeros, 0, sizeof(zeros));
		div = lldiv(nzeros, sizeof(zeros));
		for (; div.quot > 0; div.quot--)
			xwrite(fd, zeros, sizeof(zeros));
		xwrite(fd, zeros, div.rem);
	}
} /* zwrite */

/* Process the next $sbuf-size chunk of a possibly sparse image,
 * and write the (decoded) data to $fd.  If $buf is not from a
 * sparse image, just dump it. */
static void scatter(int fd, unsigned char const *buf, size_t sbuf)
{
	static int issparse = -1;
	static unsigned nsegs;
	static struct
	{
		/* A sparse image has a header followed by a sequence
		 * of data segments.  The decoded image will be a sequence
		 * of $empty zeroes, followed by $sdata data bytes. */
		uint32_t empty, sdata;
	} segs[SPIMG_MAX_SEGS], *seg;

	/* Reset state? */
	if (fd < 0)
	{	/* Verify that all segments have been consumed. */
		assert(!nsegs);
		issparse = -1;
		zwrite(-1, 0);
		return;
	}

	/* If it's out first call, see if we have a sparse image. */
	if (issparse < 0)
	{
		static struct
		{	/* Sparse image header from resparse.h.
			 * Everything is in little-endian. */
			uint32_t magic;
			uint32_t nsegs;
			uint8_t  name[SPIMG_MAX_NAMELEN+1];
			uint8_t  reserved[44];
			struct
			{
				uint32_t off, len;
			} __attribute__((packed)) seg[SPIMG_MAX_SEGS];
		} __attribute__((packed)) hdr;

		assert(sbuf >= sizeof(hdr));
		memcpy(&hdr, buf, sizeof(hdr));
		if (hdr.magic == SPIMG_MAGIC)
		{
			unsigned i, n;
			uint32_t prevend;

			/* $hdr.segs[] -> $segs[]. */
			issparse = 1;
			buf  += sizeof(hdr);
			sbuf -= sizeof(hdr);;

			/* Offsets and lengths are in 512 bytes. */
			n = 512;
			prevend = 0;
			nsegs = hdr.nsegs;
			for (i = 0; i < nsegs; i++)
			{
				assert(hdr.seg[i].off >= prevend);
				segs[i].empty = (hdr.seg[i].off-prevend)*n;
				segs[i].sdata =  hdr.seg[i].len*n;
				prevend = hdr.seg[i].off+hdr.seg[i].len;
			}

			/* Punch the first hole if segs[0] has any. */
			if (nsegs > 0)
			{
				seg = segs;
				zwrite(fd, seg->empty);
			}
		} else
			issparse = 0;
	} /* first call */

	/*
	 * If we have a sparse image $buf may have data from multiple
	 * segments.  We have the following cases:
	 * -- <zeros><buf>		// start of a new segment;
	 * 				// <zeros> can be 0 for the
	 *				// first segment
	 * -- <buf>			// middle or end of a segment
	 * -- <buf>[<zeros><buf>]+	// end of a segment followed
	 *				// by others, of which the last
	 *				// can be unfinished or complete
	 * -- <nothing>			// we must have processed all
	 *				// segemnts
	 *
	 * Write $fd up to but not including the last <buf> fragment.
	 */
	if (issparse)
	{
		/* Write as much complete segments as we can. */
		for (;;)
		{
			/* All segments done? */
			if (!nsegs)
			{
				assert(!sbuf);
				return;
			}

			/* Found an incomplete segment? */
			if (seg->sdata > sbuf)
			{
				seg->sdata -= sbuf;
				break;
			}

			/* Write the remaining segment data. */
			xwrite(fd, buf, seg->sdata);
			buf  += seg->sdata;
			sbuf -= seg->sdata;
			if (--nsegs > 0)
			{	/* Write the next segment's zeroes. */
				seg++;
				zwrite(fd, seg->empty);
			}
		} /* for complete segments */
	} /* write sparse segments */

	/* Dump/write what has remained of $buf. */
	xwrite(fd, buf, sbuf);
} /* scatter */

/* Dump an uncompressed but possibly sparse FIASCO subimage into $fd. */
static void dump(int fd, struct fiasco_subimage *img)
{
	lldiv_t div;
	uint64_t off;
	unsigned toread;
	unsigned char buf[65536];

	toread = sizeof(buf);
	div = lldiv(img->total_size, sizeof(buf));
	for (off = 0; off < img->total_size; off += toread)
	{
		if (!div.quot)
			toread = div.rem;
		else
			div.quot--;
		assert(!fiasco_read_bytes(img, off, buf, toread));
		scatter(fd, buf, toread);
	}
	scatter(-1, NULL, 0);
}

/* Like dump(), but decompress before scattering. */
static void unlzo(int fd, struct fiasco_subimage *img)
{
	uint64_t off;
	unsigned char *buf;

	off = 0;
	buf = NULL;
	for (;;)
	{
		/* The input is a sequence of $bhdr:s
		 * followed by input data. */
		struct
		{
			uint64_t magic;
			uint32_t iscomp;
			uint32_t sin, sex;
		} __attribute__((packed)) const bhdr;

		/* Get $bhdr. */
		assert(off + sizeof(bhdr) <= img->total_size);
		assert(!fiasco_read_bytes(img, off,
			(void*)&bhdr, sizeof(bhdr)));
		assert(bhdr.magic == MAGIC_LZO_MAGIC);
		off += sizeof(bhdr);

		/* End of input? */
		if (!bhdr.sin)
			break;

		if (!bhdr.iscomp)
		{	/* Just dump the input data. */
			assert(bhdr.sin == bhdr.sex);
			assert((buf = realloc(buf, bhdr.sex)) != NULL);
			assert(!fiasco_read_bytes(img, off,
				buf, bhdr.sin));
		} else
		{
			lzo_uint sbuf;
			unsigned char *inbuf;

			/* Decompress $buf in-place. */
			assert(bhdr.sin <= bhdr.sex);
			sbuf = bhdr.sex + bhdr.sex/16 + 67;
			assert(sbuf >= bhdr.sin);
			assert((buf = realloc(buf, sbuf)) != NULL);
			inbuf = &buf[sbuf-bhdr.sin];

			assert(!fiasco_read_bytes(img, off,
				inbuf, bhdr.sin));
			assert(lzo1x_decompress(inbuf, bhdr.sin,
				buf, &sbuf, NULL) == LZO_E_OK);
			assert(sbuf == bhdr.sex);
		}

		scatter(fd, buf, bhdr.sex);
		off += bhdr.sin;
	} /* for all input */

	free(buf);
	scatter(-1, NULL, 0);
} /* unlzo */

/* The main function */
int main(int argc, char const *argv[])
{
	int tostdout;
	unsigned i, nsubs;
	struct fiasco_image *img;
	struct fiasco_subimage *sub;

	/* Help? */
	if (!argv[i = 1])
	{
		puts("Usage: debacle <FIASCO> [-t|-] <subimage>...");
		return 0;
	}

	/* Init */
	img = fiasco_new_image();
	assert(!fiasco_read_image(img, argv[i++]));

	/* Dump to stdout? */
	if (argv[i] && !strcmp(argv[i], "-"))
	{
		tostdout = 1;
		i++;
	} else if (argv[i] && !strcmp(argv[i], "-t"))
	{
		Opt_test = 1;
		tostdout = 1;
		i++;
	} else
		tostdout = 0;

	/* Run */
	nsubs = argc - i;
	for (sub = img->si_list; nsubs > 0; sub = sub->next)
	{
		int hout;
		unsigned o;
		uint64_t lzo_magic;

		/* Are we interested in this subimage? */
		assert(sub != NULL);
		for (o = i; argv[o] && strcmp(sub->name, argv[o]); o++)
			;
		if (!argv[o])
			continue;
		nsubs--;

		/* Open $hout. */
		if (!tostdout)
			assert((hout = open(sub->name,
				O_CREAT|O_TRUNC|O_WRONLY, 0666)) >= 0);
		else
			hout = STDOUT_FILENO;

		if (sub->total_size >= sizeof(lzo_magic))
		{
			fiasco_read_bytes(sub, 0,
				(unsigned char *)&lzo_magic,
				sizeof(lzo_magic));
			if (lzo_magic == MAGIC_LZO_MAGIC)
			{
				unlzo(hout, sub);
				goto done;
			}
		}

		dump(hout, sub);
done:
		if (!tostdout)
			assert(close(hout) == 0);
	} /* for subimages */

	/* Done */
	return 0;
} /* main */

/* End of debacle.c */
