# Host test of the multiboot state logic (needs a native gcc and zlib).
REPO := ../..

test: mb_host_test
	./mb_host_test

mb_host_test: test.c $(REPO)/driver/mb_flash.c $(REPO)/driver/mb_flash.h
	gcc -O1 -Wall -Wno-unused -no-pie -I stub -I $(REPO) -DGIT_HASH='"abc1234"' -o $@ test.c $(REPO)/driver/mb_flash.c -lz \
		-Wl,--defsym,__mb_ramfunc_end=__mb_ramfunc_start+16

clean:
	rm -f mb_host_test

.PHONY: test clean
