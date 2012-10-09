/*
 * fprint_aes2550 driver prototype, dump extractor
 * Copyright (c) 2012 Vasily Khoruzhick <anarsoul@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>

struct frame_header {
	unsigned char type;
	unsigned char size_msb;
	unsigned char size_lsb;
};

int main(int argc, char *argv[])
{
	FILE *in, *out;
	unsigned char buf[0x31e];
	char name[256];
	int frame_size, frame_num = 0, x, y;
	struct frame_header header;

	if (argc != 2) {
		printf("Usage: %s filename\n", argv[0]);
		return 0;
	}

	in = fopen(argv[1], "rb");

	while (!feof(in)) {
		if (!fread(&header, sizeof(header), 1, in))
			break;
		frame_size = header.size_msb * 256 + header.size_lsb;
		printf("Frame type is %.2x, size %.4x\n", (int)header.type, frame_size);
		if (header.type != 0xe0) {
			fseek(in, frame_size, SEEK_CUR);
			continue;
		}
		if (frame_size != 0x31e) {
			printf("Bogus frame size: %.4x\n", frame_size);
		}
		if (!fread(buf, frame_size, 1, in))
			break;
		if (!(buf[12 - 3] & 0x80)) {
			printf("Last frame is %d\n", frame_num);
		}
		snprintf(name, sizeof(name), "frame-%.5d.pnm", frame_num);
		out = fopen(name, "wb");
		fprintf(out, "P2\n");
		fprintf(out, "8 192\n");
		fprintf(out, "15\n");
		for (y = 0; y < 192; y++) {
			for (x = 0; x < 4; x++) {
				fprintf(out, "%.2d %.2d ", (int)(buf[30 + y * 4 + x] >> 4), (int)(buf[30 + y * 4 + x] & 0xf));
			}
			fprintf(out, "\n");
		}
		fclose(out);
		frame_num++;

	}
}
