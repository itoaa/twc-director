SRC = ../components/twc_director
CC ?= gcc
CFLAGS = -std=c99 -Wall -Wextra -Wno-unused-parameter -I$(SRC)

test: test_twc
	./test_twc

test_twc: test_twc.c $(SRC)/twc_core.c $(SRC)/twc_device.c $(SRC)/twc_protocol.c $(SRC)/twc_frame.c
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -f test_twc
