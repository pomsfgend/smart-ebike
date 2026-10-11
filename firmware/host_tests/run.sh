#!/bin/sh
# Same as `make test`, for machines without make.
set -e
cd "$(dirname "$0")"
S=../components/fastcharge/src
${CC:-gcc} -std=c11 -Wall -Wextra -Werror -O1 -g -I../components/fastcharge/include -o run_tests \
  test_main.c $S/gbt_codec.c $S/gbt_tp_tx.c $S/jbd_bms.c $S/fc_supervisor.c $S/fc_core.c
./run_tests
