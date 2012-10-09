/*
 * fprint_aes2550 driver prototype
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
#include <stdarg.h>
#include <signal.h>
#include <errno.h>
#include <libusb.h>


#define EP_IN (1 | LIBUSB_ENDPOINT_IN)
#define EP_OUT (2 | LIBUSB_ENDPOINT_OUT)
#define BULK_TIMEOUT 4000

#define AES2550_CMD_SET_IDLE_MODE 0x00
#define AES2550_CMD_RUN_FINGER_DETECT 0x01
#define AES2550_CMD_GET_ENROLL_IMAGE 0x02
#define AES2550_CMD_GET_ID 0x07

#define AES2550_REG80 0x80
#define AES2550_REG80_MASTER_RESET (1 << 0)

int aborted = 0;

static void __msg(FILE *stream, const char *msg, va_list args)
{
	vfprintf(stream, msg, args);
	fprintf(stream, "\n");
}

static void die(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	__msg(stderr, msg, args);
	va_end(args);
	exit(1);
}

static void msg(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	__msg(stdout, msg, args);
	va_end(args);
}

static void msg_err(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	__msg(stderr, msg, args);
	va_end(args);
}

static void sighandler(int num)
{
	msg("got signal %d\n", num);
	aborted = 1;
}

static int aes2550_cmd_write(libusb_device_handle *h, unsigned char cmd)
{
	int r;
	int actual_len = 0;

	r = libusb_bulk_transfer(h, EP_OUT, &cmd, 1, &actual_len, BULK_TIMEOUT);

	if (!r && (actual_len != 1))
		return -EIO;

	return r;
}

static int aes2550_reg_write(libusb_device_handle *h, unsigned char reg, unsigned char data)
{
	int r;
	int actual_len = 0;
	unsigned char cmd_data[2];

	cmd_data[0] = reg;
	cmd_data[1] = data;

	r = libusb_bulk_transfer(h, EP_OUT, cmd_data, 2, &actual_len, BULK_TIMEOUT);

	if (!r && (actual_len != 2))
		return -EIO;

	return r;
}

static int aes2550_probe(libusb_device_handle *h)
{
	int r, actual_len = 0;
	unsigned char cmd_res[8192];
	unsigned char init_cmds_2[] = {
		0x80, 0x01, /* Master reset */
		0xa1, 0x00,
		0x80, 0x12,
		0x85, 0x80,
		0xa8, 0x10,
		0xb1, 0x20,
		0x81, 0x04,
	};
	unsigned char init_cmds_3[] = {
		0x80, 0x01, /* Master reset */
		0xdd, 0x00,
		0x06, /* Run calibration */
		0x10, /* Read calibration table */
	};

	/* Init 2 */
	r = libusb_bulk_transfer(h, EP_OUT, init_cmds_2, sizeof(init_cmds_2), &actual_len, BULK_TIMEOUT);
	if ((r < 0) || (actual_len != sizeof(init_cmds_2))) {
		msg_err("Failed to init_2 AES2550/AES2810\n");
		return r;
	}

	/* Receiving one frame */
	r = libusb_bulk_transfer(h, EP_IN, cmd_res, sizeof(cmd_res), &actual_len, BULK_TIMEOUT);
	if (r < 0) {
		msg_err("Failed to receive frame\n");
		return r;
	}

	/* Init 3 (calibration) */
	r = libusb_bulk_transfer(h, EP_OUT, init_cmds_3, sizeof(init_cmds_3), &actual_len, BULK_TIMEOUT);
	if ((r < 0) || (actual_len != sizeof(init_cmds_3))) {
		msg_err("Failed to calibrate AES2550/AES2810\n");
		return r;
	}
	r = libusb_bulk_transfer(h, EP_IN, cmd_res, sizeof(cmd_res), &actual_len, BULK_TIMEOUT);
	if (r < 0) {
		msg_err("Failed to read calibration table\n");
		return r;
	}

	msg("Probed device successfully!\n");

	return 0;
}

static int aes2550_get_enroll_image(libusb_device_handle *h)
{
	unsigned char init_cmds_fd[] = {
		0x80, 0x01,
		0x95, 0x18,
		0xa1, 0x00,
		0x8a, 0x07,
		0xad, 0x00,
		0xbd, 0x00,
		0xbe, 0x00,
		0xcf, 0x01,
		0xdd, 0x00,
		0x70, 0x00, 0x01, 0x00, /* Heart beat cmd, 3 * 16 cycles without sending image */
		0x01,
	};
	unsigned char init_cmds_img[] = {
		0x80, 0x01,
		0x80, 0x18,
		0x85, 0x00,
		0x8f, 0x0c,
		0x9c, 0xbf,
		0x9d, 0x46,
		0x9e, 0x71,
		0x9f, 0x23,
		0xa2, 0x00,
		0xb1, 0x00,
		0xbf, 0x0b,
		0xcf, 0x32,
		0xdc, 0x01,
		0xdd, 0x00,
		0x70, 0x00, 0x01, 0x03, /* Heart beat cmd, 3 * 16 cycles without sending image */
		0x02,
	};
	FILE *dump;

	int r, actual_len = 0;
	unsigned char cmd_res[8192];

	do {
		r = libusb_bulk_transfer(h, EP_OUT, init_cmds_fd, sizeof(init_cmds_fd), &actual_len, BULK_TIMEOUT);
		if ((r < 0) || (actual_len != sizeof(init_cmds_fd))) {
			msg_err("Failed to init_fd AES2550/AES2810\n");
			return r;
		}
		r = libusb_bulk_transfer(h, EP_IN, cmd_res, 8192, &actual_len, BULK_TIMEOUT);
		if (r < 0) {
			msg_err("Failed to read back fd value!\n");
			return r;
		}
	} while (!(cmd_res[1] & 0x80));

	r = libusb_bulk_transfer(h, EP_OUT, init_cmds_img, sizeof(init_cmds_img), &actual_len, BULK_TIMEOUT);
	if ((r < 0) || (actual_len != sizeof(init_cmds_img))) {
		msg_err("Failed to init_img AES2550/AES2810\n");
		return r;
	}
	msg("Waiting for img...\n");

	dump = fopen("finger.dump", "wb");
	if (!dump)
		return -EIO;

	do {
		r = libusb_bulk_transfer(h, EP_IN, cmd_res, 8192, &actual_len, BULK_TIMEOUT);
		if (r < 0)
			break;
		msg("Got buffer of size %d\n", actual_len);
		fwrite(cmd_res, 1, actual_len, dump);
#if 0
		if (actual_len < 8192)
			break;
#endif
	} while (r == 0 && !aborted);
	r = aes2550_reg_write(h, 0x80, 0x01);

	fclose(dump);
	return 0;
}

static void aes2550_set_idle_state(libusb_device_handle *h)
{
	aes2550_cmd_write(h, AES2550_CMD_SET_IDLE_MODE);
}

int main(int argc, char *argv[])
{
	int r;
	libusb_device_handle *h;
	libusb_context *ctx;

	struct sigaction sigact;

        sigact.sa_handler = sighandler;
        sigemptyset(&sigact.sa_mask);
        sigact.sa_flags = 0;
        sigaction(SIGINT, &sigact, NULL);
        sigaction(SIGTERM, &sigact, NULL);
        sigaction(SIGQUIT, &sigact, NULL);

	libusb_init(&ctx);

	h = libusb_open_device_with_vid_pid(ctx, 0x08ff, 0x2810);
	if (!h) {
		msg_err("Can't open aes2550 device!\n");
		return 0;
	}

	r = libusb_claim_interface(h, 0);
	if (r < 0) {
		msg_err("Failed to claim interface 0\n");
		goto exit_closelibusb;
	}

	r = aes2550_probe(h);
	if (r < 0) {
		msg_err("Failed to probe aes2550\n");
		goto exit_closelibusb;
	}

	aes2550_get_enroll_image(h);

exit_closelibusb:
	libusb_close(h);
	libusb_exit(ctx);

	return 0;

}
