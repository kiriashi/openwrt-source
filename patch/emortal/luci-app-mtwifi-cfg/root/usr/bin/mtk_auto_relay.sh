#!/bin/sh

do_connect() {
    local dev="$1"
    iwpriv "$dev" set ApCliEnable=0
    sleep 1
    iwpriv "$dev" set SiteSurvey=1
    sleep 2
    iwpriv "$dev" set ApCliEnable=1
    iwpriv "$dev" set ApCliAutoConnect=1
}

while true; do
    idx=0
    while :; do
        sec="wireless.@wifi-iface[$idx]"
        mode=$(uci -q get "$sec.mode")
        [ -z "$mode" ] && break

        if [ "$mode" = "sta" ]; then
            d_orig=$(uci -q get "$sec.device")
            [ "$d_orig" = "MT7981_1_2" ] && d="apclix0" || d="apcli0"

            check_valid=$(iwconfig "$d" 2>/dev/null | grep "Access Point" | grep -vE "Not-Associated|00:00:00:00:00:00")
            if [ -z "$check_valid" ]; then
                do_connect "$d"
                sleep 15
            fi
        fi
        idx=$((idx+1))
    done
    sleep 30
done
