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

    taps=(`tap-ctl list`)
    count=0

    for i in ${taps[@]}; do
        k=$(echo $i | cut -d\= -f1)
        v=$(echo $i | cut -d\= -f2)
        echo "TAP: $i K: $k V: $v C: $count"
        ((count+=1))

        if [ $(( $count % 4 )) -eq 0 ]; then
            count=0
        fi
    done
}

function xl_hack_netup()
{
    echo "Nethack xl IP: $1"
}

function xl_hack_domid()
{
    echo "Hack xl domid $1"
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
  -r <file> Retap a vhd.
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

