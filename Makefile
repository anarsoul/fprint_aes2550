.PHONY: clean all
all: fprint_aes2550 extract_dump

fprint_aes2550: fprint_aes2550.c
	gcc $^ -o $@ `pkg-config --cflags --libs libusb-1.0`

extract_dump: extract_dump.c
	gcc -O0 -ggdb $^ -o $@
clean:
	rm -f fprint_aes2550 extract_dump
