#! /bin/sh

function xl_hack_init()
{
    echo "Init xl hacks..."
    if [ -z "`grep "flask_enforcing=0" /boot/system/grub/grub.cfg`" ]; then
        echo "Xen flask is in enforcing mode, we should fix this bugger first and reboot!"
        cat /boot/system/grub/grub.cfg | sed 's/flask_enforcing=[[:digit:]]\+ \?//g' > /boot/system/grub/tmp.cfg
        cat /boot/system/grub/tmp.cfg | sed "s/XEN_COMMON_CMD=\"/XEN_COMMON_CMD=\"flask_enforcing=0 /" > /boot/system/grub/grub.cfg
        reboot
        exit
    fi

    if [ "`getenforce`" != "Permissive" ]; then
        echo "SELinux is not in permissive mode, you need to fix this first!"
        echo "do this:"
        echo "$ nr"
        echo "$ setenforce 0"
        exit
    fi

    # Always update the SELinux config so we don't end up with it on again.
    mount -o remount,rw /
    cat /etc/selinux/config | sed 's/SELINUX=.*//g' > /tmp/config
    cat /tmp/config | sed "1s/^/SELINUX=permissive/" > /etc/selinux/config

    echo "1" > /tmp/domid

    if [ ! -e /var/lib/xen ]; then
        mkdir /var/lib/xen
    fi

    if [ ! -e /var/run/xen ]; then
        mkdir /var/run/xen
    fi

    if [ ! -e /var/log/xen ]; then
        mkdir /var/log/xen
    fi

    cp qemu-system-i386 /usr/lib/xen/bin/
    chmod a+x /usr/lib/xen/bin/qemu-system-i386
    cp qemu-ifup /usr/lib/xen/bin/

    cp _vimrc /root/.vimrc

    sync
    mount -o remount,ro /
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

    if [ ! -e $1 ]; then
        echo "No tools dir $1 WTF!"
        exit
    fi

    if [ ! -d $1 ]; then
        echo "Yo $1 is not a dir!"
        exit
    fi

    if [ "${1:0:1}" != "/" ]; then
        echo "Cannot reinstall from: $1 without full path, pay attention!"
        exit
    fi

    if [ ! -e $1/usr/lib/libxenlight.so.4.3.0 ]; then
        echo "Not finding my files in $1!"
        exit
    fi

    mount -o remount,rw /

    if [ ! -e /storage/xen-tools-orig ]; then
        mkdir /storage/xen-tools-orig
        cp /usr/lib/libblktapctl.so.0.1.0 /storage/xen-tools-orig
        cp /usr/lib/libvhd.so.1.0.0 /storage/xen-tools-orig
        cp /usr/lib/libxenctrl.so.4.3.0 /storage/xen-tools-orig
        cp /usr/lib/libxenguest.so.4.3.0 /storage/xen-tools-orig
        cp /usr/lib/libxenhvm.so.1.0.0 /storage/xen-tools-orig
        cp /usr/sbin/tap-ctl /storage/xen-tools-orig
        cp /usr/sbin/tapdisk-diff /storage/xen-tools-orig
        cp /usr/sbin/tapdisk-stream /storage/xen-tools-orig
        cp /usr/sbin/tapdisk2 /storage/xen-tools-orig
        cp /usr/sbin/td-util /storage/xen-tools-orig
        cp /usr/sbin/vhd-update /storage/xen-tools-orig
        cp /usr/sbin/vhd-util /storage/xen-tools-orig
        cp /usr/bin/qemu-system-i386 /storage/xen-tools-orig
    fi

    rm /usr/lib/libblktapctl.so.0.1.0

    cp $1/usr/lib/libblktapctl.so.1.0.0 /usr/lib
    cp $1/usr/lib/libvhd.so.1.0.0 /usr/lib
    cp $1/usr/lib/libxenctrl.so.4.3.0 /usr/lib
    cp $1/usr/lib/libxenguest.so.4.3.0 /usr/lib
    cp $1/usr/lib/libxenhvm.so.1.0.0 /usr/lib
    cp $1/usr/lib/libxenlight.so.4.3.0 /usr/lib
    cp $1/usr/lib/libxlutil.so.4.3.0 /usr/lib
    cp $1/usr/sbin/tap-ctl /usr/sbin
    cp $1/usr/sbin/tapdisk-client /usr/sbin
    cp $1/usr/sbin/tapdisk-diff /usr/sbin
    cp $1/usr/sbin/tapdisk-stream /usr/sbin
    cp $1/usr/sbin/tapdisk2 /usr/sbin
    cp $1/usr/sbin/td-util /usr/sbin
    cp $1/usr/sbin/vhd-update /usr/sbin
    cp $1/usr/sbin/vhd-util /usr/sbin
    cp $1/usr/sbin/xl /usr/sbin
    if [ -e $1/usr/bin/qemu-system-i386 ]; then
        cp $1/usr/bin/qemu-system-i386 /usr/bin
    fi


    pushd /usr/lib
    ln -fs libblktapctl.so.1.0.0 libblktapctl.so.1.0
    ln -fs libxenlight.so.4.3.0 libxenlight.so.4.3
    ln -fs libxlutil.so.4.3.0 libxlutil.so.4.3
    popd
}

function xl_hack_stage()
{
    local dstpath=${PWD}/image

    echo "Stage xl and qemu hack..."

    if [ -e dstpath ]; then
        rm -rf dstpath
    fi

    if [ ! -e /storage/image.tar.gz ]; then
        echo "No /storage/image.tar.gz - you are wasting my time!"
        exit
    fi

    mv /storage/image.tar.gz .
    tar -xzf image.tar.gz

    if [ -e /storage/qemu-system-i386 ]; then
        mkdir -p dstpath/usr/bin
        mv /storage/qemu-system-i386 dstpath/usr/bin
    fi
}

function usage()
{
cat <<EOF
Usage:
  -i        Init stuff to make xl happy.
  -r <file> Retap a vhd, use a full path to vhd file.
  -n <ip>   Hack up the xen bridge with an IP.
  -d <id>   Hack a new domid in.
  -s        Stage new Xen tools for install.
  -t <path> Reinstall Xen tools hack, use full path to image base dir.
EOF
}

getopts "ir:n:d:st:" OPTION

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
    s)
        xl_hack_stage
        ;;
    t)
        xl_hack_tools $OPTARG
        ;;
    *)
        usage
        ;;
esac

