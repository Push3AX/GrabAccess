/**************************************************************
	LZSS.C -- A Data Compression Program
	(tab = 4 spaces)
***************************************************************
	4/6/1989 Haruhiko Okumura
	Use, distribute, and modify this program freely.
	Please send me your improved versions.
		PC-VAN		SCIENCE
		NIFTY-Serve	PAF01022
		CompuServe	74050,1022
**************************************************************/

#include <grub/types.h>
#include <grub/macho.h>

#define N		 4096	/* size of ring buffer */
#define F		   18	/* upper limit for match_length */
#define THRESHOLD	2   /* encode string into position and length
						   if match_length is greater than this */
#define NIL			N	/* index for root of binary search trees */

#define EOF -1
#define getc(file) ((src < srcend) ? *src++ : EOF)
#define putc(c, file) (dst < dstend) ? (*dst++ = (c)) : 0;

grub_size_t
grub_decompress_lzss (grub_uint8_t *dst, grub_uint8_t *dstend,
		      grub_uint8_t *src, grub_uint8_t *srcend)
{
	int  i, j, k, r, c;
	unsigned int  flags;
	static unsigned char text_buf[N + F - 1];
	grub_uint8_t *dst0 = dst;
	
	for (i = 0; i < N - F; i++) text_buf[i] = ' ';
	r = N - F;  flags = 0;
	for ( ; ; ) {
		if (((flags >>= 1) & 256) == 0) {
			if ((c = getc(infile)) == EOF) break;
			flags = c | 0xff00;		/* uses higher byte cleverly */
		}							/* to count eight */
		if (flags & 1) {
			if ((c = getc(infile)) == EOF) break;
			putc(c, outfile);  text_buf[r++] = c;  r &= (N - 1);
		} else {
			if ((i = getc(infile)) == EOF) break;
			if ((j = getc(infile)) == EOF) break;
			i |= ((j & 0xf0) << 4);  j = (j & 0x0f) + THRESHOLD;
			for (k = 0; k <= j; k++) {
				c = text_buf[(i + k) & (N - 1)];
				putc(c, outfile);  text_buf[r++] = c;  r &= (N - 1);
			}
		}
	}
    return dst - dst0;
}
