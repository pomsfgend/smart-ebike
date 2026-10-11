# Host unit tests for the pure C modules (no ESP-IDF needed): make && ./run_tests
CC ?= gcc
CH = ../charger/components/charger
CM = ../common/fc_common
CFLAGS += -std=c11 -Wall -Wextra -Werror -O1 -g -I$(CH)/include -I$(CM)/include -I../cube_patch
SRC = $(CM)/src/gbt_codec.c $(CM)/src/fc_link.c ../cube_patch/gbt_tp_tx.c \
      $(CH)/src/jbd_bms.c $(CH)/src/fc_supervisor.c $(CH)/src/flatpack2.c $(CH)/src/charger_core.c $(CH)/src/direct_core.c
run_tests: test_main.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^
test: run_tests
	./run_tests
clean:
	rm -f run_tests
