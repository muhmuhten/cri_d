#include <arpa/inet.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void init_mask1(unsigned char *m, uint64_t k) {
	m[0] = k;
	m[1] = k >> 8;
	m[2] = k >> 16;
	m[3] = (k >> 24) - 52;
	m[4] = (k >> 32) + 249;
	m[5] = (k >> 40) ^ 19;
	m[6] = (k >> 48) + 97;
	m[7] = ~m[0];
	m[8] = m[2] + m[1];
	m[9] = m[1] - m[7];
	m[10] = ~m[2];
	m[11] = ~m[1];
	m[12] = m[0]; // m[11] + m[9]
	m[13] = m[8] - m[3];
	m[14] = ~m[13];
	m[15] = m[1] - m[2];
	m[16] = m[2] + m[2];
	m[17] = m[16] ^ m[7];
	m[18] = ~m[15];
	m[19] = m[3] ^ 16;
	m[20] = m[4] - 50;
	m[21] = m[5] + 237;
	m[22] = m[6] ^ 243;
	m[23] = m[19] - m[15];
	m[24] = m[21] + m[7];
	m[25] = 33 - m[19];
	m[26] = m[20] ^ m[23];
	m[27] = m[22] + m[22];
	m[28] = m[23] + 68;
	m[29] = m[3] + m[4];
	m[30] = m[5] - m[22];
	m[31] = m[29] ^ m[19];
}

static inline FILE *arg_fdopen(char **s, const char *m) {
	/* safe: fdopen(-48, m) should just fail */
	FILE *h = fdopen(*++*s-'0', m);
	if (!h)
		err(2, "fdopen %d", **s-'0');
	return h;
}
static inline FILE *arg_open(char ***a, const char *m) {
	char *name = *++*a;
	if (!name)
		errx(2, "no filename");
	FILE *h = fopen(name, m);
	if (!h)
		err(2, "open %s", name);
	return h;
}

int main(int argc, char **argv) {
	uint64_t keynum = 5423778438;
	FILE *in = 0, *aout = 0, *vout = 0;

	for (char **a = argv+1; *a; a++) {
		for (char *s = *a; *s; s++) {
			switch (*s) {
			case '-': break;
			case 'k': keynum = strtoimax(*++a, NULL, 0); break;
			case 'i': in = arg_open(&a, "rb"); break;
			case 'a': aout = arg_open(&a, "wb"); break;
			case 'v': vout = arg_open(&a, "wb"); break;
			case 'I': in = arg_fdopen(&s, "rb"); break;
			case 'A': aout = arg_fdopen(&s, "wb"); break;
			case 'V': vout = arg_fdopen(&s, "wb"); break;
			default: errx(2, "unrecognised option -%c", *s);
			}
		}
	}
	if (!in)
		errx(2, "no input");

	uint64_t video_mask1[4], video_mask2[4], audio_mask1[4];
	init_mask1((unsigned char *)video_mask1, keynum);
	for (int j = 0; j < 4; j++) {
		uint64_t w = ~video_mask1[j];
		video_mask2[j] = w;
		char *c = (char *) &w;
		c[1] = c[5] = 85;
		c[3] = 82;
		c[7] = 67;
		audio_mask1[j] = *(uint64_t *)c;
	}

	/* aligned 64k buffer */
	uint64_t stack_buf[8192];
	size_t buf_len = sizeof stack_buf;
	char *buf = (char *)stack_buf;
	for (;;) {
		uint32_t magic = 0;
		if (fread(&magic, 4, 1, in) == 0)
			return 0;
		magic = htonl(magic);

		switch (magic) {
		case 0x43524944: // CRID
		case 0x40534656: // @SFV
		case 0x40534641: // @SFA
		case 0x40435545: // @CUE
			break;
		case 0x44495243: // DIRC
			errx(2, "host wrong-endian, somehow");
		default:
			errx(2, "unknown magic number: %x", magic);
		}

		uint32_t len;
		if (fread(&len, 4, 1, in) != 1)
			err(1, "short read len");
		len = htonl(len);

		if (len >= buf_len) {
			while (len >= buf_len)
				buf_len <<= 1;
			if (buf != (char *)stack_buf)
				free(buf);
			if (!(buf = malloc(buf_len)))
				err(1, "malloc %zu for %u", buf_len, len);
		}
		if (fread(buf, 1, len, in) != len)
			err(1, "short read data");

		uint16_t off = htons(*(uint16_t *)buf);
		uint16_t pad = htons(*(uint16_t *)(buf+2));
		if (buf[7]) {
			/* metadata we don't care about */
			continue;
		}
		else if (magic == 0x40534641) {
			// @SFA
			if (!aout)
				continue;

			uint64_t *p = (uint64_t *)(buf+off);
			uint32_t s = len-off-pad, z = (s-1) >> 3;
			for (int j = 40; j <= z; j++)
				p[j] ^= audio_mask1[j&3];
			fwrite(p, 1, s, aout);
		}
		else if (magic == 0x40534656) {
			// @SFV
			if (!vout)
				continue;

			uint64_t *p = (uint64_t *)(buf+off);
			uint32_t s = len-off-pad, z = (s-1) >> 3;
			if (s >= 576) {
				uint64_t mask[4];

				memcpy(mask, video_mask2, 32);
				for (int j = 40; j <= z; j++)
					mask[j&3] = (p[j] ^= mask[j&3]) ^ video_mask2[j&3];

				memcpy(mask, video_mask1, 32);
				for (int j = 8; j < 40; j++)
					p[j] ^= mask[j&3] ^= p[j+32];
			}
			fwrite(p, 1, s, vout);
		}
		else
			warnx("unrecognised non-metadata with magic %x", magic);
	}
}
