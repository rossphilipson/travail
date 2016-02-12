#! /bin/sh

function xl_hack_init()
{
    echo "Init xl hacks..."
    if [ "`getenforce`" != "Permissive" ]; then
        echo "SELinux is not in permissive mode, you should fix this first!"
        echo "Edit /etc/selinux/config and set SELINUX=permissive then reboot"
        exit
    fi

    if [ -z "`grep "flask_enforcing=0" /boot/system/grub/grub.cfg`" ]; then
        echo "Xen flask is in enforcing mode, you should fix this first!"
        echo "Edit /boot/system/grub/grub.cfg and set flask_enforcing=0 then reboot"
        exit
    fi

    mount -o remount,rw /
    echo "1" > /tmp/domid

    if [ ! -e /var/lib/xen ]; then
        mkdir /var/lib/xen
    fi

    if [ ! -e /var/log/xen ]; then
        mkdir /var/log/xen
    fi

    if [ -e /usr/lib/xen/bin/qemu-system-i386 ]; then
        chmod a+x /usr/lib/xen/bin/qemu-system-i386
    else
        echo "Missing a /usr/lib/xen/bin/qemu-system-i386 wrapper, copy one there!"
    fi

    echo "Init xl hacks complete"
}

function xl_hack_retap()
{
    echo "Retapping shit for $1"

    local taps=(`tap-ctl list`)
    local count=0
    local pid=0

    if [ ! -e $1 ]; then
        echo "Cannot retap missing file: $1 fix that shit!"
        exit
    fi

    if [ "${1:0:1}" != "/" ]; then
        echo "Cannot retap file: $1 without full path, are you crazy!"
        exit
    fi

    for i in ${taps[@]}; do
        k=$(echo $i | cut -d\= -f1)
        v=$(echo $i | cut -d\= -f2)
        echo "TAP: $i K: $k V: $v C: $count"

        if [ $count -eq 0 ]; then
            pid=$v
        fi

        if [ $count -eq 1 ]; then
            echo "Untapping PID: $pid MINOR: $v"
            tap-ctl destroy -p $pid -m $v
        fi

        ((count+=1))
        if [ $(( $count % 4 )) -eq 0 ]; then
            count=0
        fi
    done

    echo "Retapping tapdev0 to $1"
    tap-ctl create -a "vhd:$1"
    tap-ctl list
}

function xl_hack_netup()
{
    echo "Nethack xl IP: $1"
    brctl addbr xenbr0
    brctl addif xenbr0 eth0
    ifconfig xenbr0 up
    ifconfig xenbr0 $1
    ifconfig eth0 up
}

function xl_hack_domid()
{
    echo "Hack xl domid $1"
    echo $1 > /tmp/domid
}

function xl_hack_tools()
{
    echo "Setup xl tools hack..."
}

function usage()
{
cat <<EOF
Usage:
  -i        Init stuff to make xl happy.
  -r <file> Retap a vhd, use a full path to vhd file.
  -n <ip>   Hack up the xen bridge with an IP.
  -d <id>   Hack a new domid in.
  -t <path> Reinstall Xen tools hack.
EOF
}

getopts "ir:n:d:t:" OPTION

case $OPTION in
    i)
        xl_hack_init
        ;;
    r)
        xl_hack_retap $OPTARG
        ;;
    n)
        xl_hack_netup $OPTARG
        ;;
    d)
        xl_hack_domid $OPTARG
        ;;
    t)
        xl_hack_tools $OPTARG
        ;;
    *)
        usage
        ;;
esac

