#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

# Load qcom-dcc module
qcom_dcc_ko="$(find /usr/lib64/modules/$(uname -r)/ -name "qcom-dcc.ko")"
if [ -n "$qcom_dcc_ko" ] && [ -f "$qcom_dcc_ko" ]; then
    modprobe qcom-dcc
    if [ $? -ne 0 ]; then
        echo "Failed to load qcom-dcc module"
        exit 1
    fi
fi

# Assume only one conf file is present in /etc/qcom-dcc/
# This file is used to configure QCOM DCC registers
conf_file=$(find /etc/qcom_dcc/ -name "*.conf")
if expr "X$conf_file" : 'X.*[[:space:]]' >/dev/null; then
    # space inside conf_file var means multiple conf files were found
    # currently only supporting one conf file per target
    echo "QCOM DCC: Support only 1 .conf file"
    exit 1
fi

. "$conf_file"

dcc_dir="${DCC_PATH:?}/${DCC_DEV_NAME:?}"

# Wait for DCC device to appear (handle deferred probing)
count=0
while [ ! -d "$dcc_dir" ] && [ $count -lt 30 ]; do
    sleep 0.1
    count=$((count + 1))
done

# Check if DCC is enabled
if [ ! -d "$dcc_dir" ]; then
    echo "QCOM DCC not enabled"
    exit 1
fi

# Reset config on boot
while [ $NO_LL -gt 0 ]
do
	NO_LL=$(( $NO_LL - 1 ))
	echo 0 > $DCC_PATH/$DCC_DEV_NAME/$NO_LL/enable
done
echo 1 > $DCC_PATH/config_reset

# Program and enable DCC linked-lists
for ll in $LINKED_LISTS; do
    $ll
    ll_no=$(echo -n "$ll" | tail -c 1)
    echo 1 > $DCC_PATH/$DCC_DEV_NAME/$ll_no/enable
done

exit 0
