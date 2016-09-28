#! /bin/sh

#UIVM_SWAP_VHD="${DOM0_MOUNT}/storage/uivm/uivm-swap.vhd"
#local UIVM_SWAP_SIZE_IN_MB="256"

create_swap_vhd()
{
    local VHD="$1"
    local SIZE_IN_MB="$2"

    rm -f "${VHD}" 2>/dev/null
    [ ! -e "${VHD}" ] >&2 || return 1

    vhd-util create -n "${VHD}" -s "${SIZE_IN_MB}" -r || return 1

    local DEV=$(tap-ctl create -a "vhd:${VHD}")
    if ! tap-ctl list | grep -q "${VHD}" ; then
        tap-ctl destroy -d "${DEV}" >&2
        rm -f "${VHD}" >&2
        return 1
    fi

    if ! mkswap "${DEV}" >&2 ; then
        tap-ctl destroy -d "${DEV}" >&2
        rm -f "${VHD}" >&2
        return 1
    fi

    sync >&2
    tap-ctl destroy -d "${DEV}" >&2
}

C=0

rm -f "/mnt/dummy.swap.vhd"

while [ $C -lt 100 ]; do
    echo "Go...$C"
    create_swap_vhd "/mnt/dummy.swap.vhd" "256"
    let C=C+1
    rm -f "/mnt/dummy.swap.vhd"
done
