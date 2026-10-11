#!/bin/sh
# Same as `make test`, for machines without make.
set -e
cd "$(dirname "$0")"
CH=../charger/components/charger
CM=../common/fc_common
${CC:-gcc} -std=c11 -Wall -Wextra -Werror -O1 -g -I$CH/include -I$CM/include -I../cube_patch -o run_tests \
  test_main.c $CM/src/gbt_codec.c $CM/src/fc_link.c ../cube_patch/gbt_tp_tx.c \
  $CH/src/jbd_bms.c $CH/src/fc_supervisor.c $CH/src/flatpack2.c $CH/src/charger_core.c $CH/src/direct_core.c
./run_tests
