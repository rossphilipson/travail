#! /bin/sh

function xl_hack_init()
{
    echo "Init xl hacks..."
}

function xl_hack_retap()
{
    echo "Retapping shit for $1"
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

