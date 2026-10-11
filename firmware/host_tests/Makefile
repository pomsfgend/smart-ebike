# Host unit tests for the pure C modules (no ESP-IDF needed): make && ./run_tests
CC ?= gcc
CFLAGS += -std=c11 -Wall -Wextra -Werror -O1 -g -I../components/fastcharge/include
SRC = ../components/fastcharge/src/gbt_codec.c ../components/fastcharge/src/gbt_tp_tx.c \
      ../components/fastcharge/src/jbd_bms.c ../components/fastcharge/src/fc_supervisor.c \
      ../components/fastcharge/src/fc_core.c
run_tests: test_main.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^
test: run_tests
	./run_tests
clean:
	rm -f run_tests
