# u-root Building/Testing (with QEMU) #

## Development Machine ##

### Development Pre-Reqs ###

Install standard dev pre-reqs:
```
$ sudo yum install -y gcc git rpmbuild python3-pip ...
$ pip3 install git-archive-all
```

Install golang:
```
$ sudo yum install -y oracle-golang-release-el7
$ sudo yum install -y golang
```

### Fetch u-root and MLE Kernel ###

Fetch u-root:
```
$ go get github.com/u-root/u-root
```
This will install u-root into `$GOPATH/src/github.com/u-root/u-root`
(typically `$GOPATH` is `~/go`)

Fetch Oracle's u-root:
```
$ cd $GOPATH/src/github.com/u-root/u-root
$ git remote add oracle https://linux-git.us.oracle.com/OCI/uroot.git
$ git fetch --all --tags --prune
```

Checkout the latest code (right now that's 6.0.0-10.0-dev)
```
$ git checkout -b ol7/u-root-6.0.0-10.0-dev ol7/u-root-6.0.0-10.0-dev
```

***NOTE:*** This just checkouts the oracle u-root branch, which
includes none of the current development work. To get the latest
development branch, see the "Development Branch" section under
"Development Cycle" below.

Fetch the MLE kernel:
```
$ cd ~
$ git clone 
https://linux-git.us.oracle.com/UEK/linux-rjphilip-public.git linux-mle
```

Get the latest branch (currently securelaunch-1902-u2-0.9.4):
```
$ cd linux-mle
$ git checkout -b securelaunch-1902-u2-0.9.4 origin/securelaunch-1902-
u2-0.9.4
```


### Build u-root and MLE Kernel ###

Build u-root RPM:
```
$ cd $GOPATH/src/github.com/u-root/u-root
$ rm -Rf ~/rpmbuild/BUILD/u-root-6.0.0
$ git-archive-all --prefix u-root-6.0.0 ~/rpmbuild/SOURCES/u-root-
6.0.0.tar.gz
$ rpmbuild -bb buildrpm/uroot.spec
```

(Re)Install u-root RPM:
```
$ sudo yum reinstall -y ~/rpmbuild/RPMS/x86_64/u-root-6.0.0-
10.el7.x86_64.rpm
```

Build MLE kernel:
```
$ cd ~/linux-mle
$ ./scripts/build_kernel_rpm.sh
```

The MLE kernel does its rpmbuild locally (in a subdirectory relative to
the linux-mle directory), so this will result in `~/linux-
mle/rpmbuild/RPMS/x86_64/kernel-uek-SL-4.14.35-1.el7uek.x86_64.rpm`



## Test Machine ##

### Test Pre-Reqs ###

Add v12n repo, edit `/etc/yum.repos.d/oracle-v12n-qa.repo` to contain:
```
[v12n-qa]
name=v12n qa repos
baseurl=
https://yum-qa.oracle.com/repo/OracleLinux/OL7/v12n/qa/latest/$basearch/
gpgcheck=0
enabled=1
EOF
```

Install swtpm and qemu:
```
# yum install -y libtpms swtpm swtpm-libs swtpm-tools OVMF qemu
```

Install RPM tools:
```
# yum install -y rpm cpio
```

Create disk images for VM:
```
$ cd /mnt/vms/images/kvm
$ qemu-img create -f qcow2 ol7-otec.qcow2 20G
$ cp /usr/share/OVMF/OVMF_VARS.pure-efi.fd ./ol7-otec.OVMF_VARS.pure-
efi.fd

```

### Install an Ol7 VM ###

Launch swtpm:
```
# swtpm socket \
	--tpm2 \
	--tpmstate dir=/usr/share/swtpm/vtpm-ol7-otec \
	--ctrl type=unixio,path=/usr/share/swtpm/vtpm-ol7-otec/swtpm-
sock \
	--log file=/var/log/swtpm/ol7-otec.log,level=20 \
	--pid file=${vtpm_data_path}/swtpm.pid \
	--daemon
```

Launch qemu:
```
# qemu-system-x86_64 -name 'ol7-otec' \
	-enable-kvm -accel kvm -machine q35,smm=on -S \
	-cpu host -smp 4 -m 4096 \
	-global ICH9-LPC.disable_s3=1 \
	-global driver=cfi.pflash01,property=secure,value=on \
	-drive file=/usr/share/OVMF/OVMF_CODE.pure-
efi.fd,index=0,if=pflash,format=raw,readonly \
	-drive file=/mnt/vms/images/kvm/ol7-otec.OVMF_VARS.pure-
efi.fd,index=1,if=pflash,format=raw \
	-drive file=/mnt/vms/images/kvm/ol7-
otec.qcow2,format=qcow2,index=0,media=disk \
	-drive file=/mnt/vms/isos/OracleLinux-R7-U8-Server-x86_64-
dvd.iso,format=raw,media=cdrom \
	-net nic,model=virtio,macaddr=52:54:00:77:9d:06 \
	-net tap,ifname=tap0,downscript=no,vhost=on \
	-debugcon file:ovmf_debug.log -global isa-debugcon.iobase=0x402 
\
	-vnc 0.0.0.0:1 -serial telnet:127.0.0.1:4558,server,nowait
-monitor stdio
```

VNC in and go through the setup.

To replicate what I know of otec environment as best I can, I do a
custom partitioning:
 - sda1: /boot/efi [200 MiB] (EFI)
 - sda2: /boot [500 MiB] (ext4)
 - sda3: swap [8 GiB] (swap) - optional: enable encryption
 - sda4: root [remaining space] (xfs/ext4/btrfs) - enable encryption

***NOTE:*** With QEMU, there is an issue with certificate checking on
newer kernels that makes them fail. I've been using 4.14.35-
1902.303.5.3.el7uek.x86_64. Unfortunately, this kernel doesn't seem to
have brtfs support.


### Setup VM ###

The VM should now be installed and booted.

ssh in to the VM to install the kernel:
```
$ wget 
https://yum-qa.oracle.com/repo/OracleLinux/OL7/UEKR5/x86_64/getPackage/kernel-uek-4.14.35-1902.303.5.3.el7uek.x86_64.rpm
$ sudo yum localinstall -y kernel-uek-4.14.35-
1902.303.5.3.el7uek.x86_64.rpm
```

Configure encryption key (only do sda3 if you encrypted swap):
```
$ sudo yum install -y vim-common
$ dd if=/dev/urandom of=lukskey bs=1 count=32
$ xxd lukskey

Example output to expect:
0000000: 46f2 1cd6 f241 4fcf cdc3 ac32 6278 2b73  F....AO....2bx+s
0000010: 1de3 ace0 68b5 3982 dd35 9844 f71f e8bd  ....h.9..5.D....

$ sudo cryptsetup luksAddKey /dev/sda3 lukskey
$ sudo cryptsetup luksAddKey /dev/sda4 lukskey
```

Load the key into the TPM:
```
# yum install -y tpm2-tools
# systemctl enable tpm2-abrmd
# reboot

Once back up and logged in:

# tpm2_nvdefine -x 0x1500001 -a 0x40000001 -s 32 -t
"ownerread|policywrite|ownerwrite"
# tpm2_nvwrite -x 0x1500001 -a 0x40000001 lukskey
# tpm2_nvread -x 0x1500001 -a 0x40000001 | xxd

Example output to expect:
0000000: 46f2 1cd6 f241 4fcf cdc3 ac32 6278 2b73  F....AO....2bx+s
0000010: 1de3 ace0 68b5 3982 dd35 9844 f71f e8bd  ....h.9..5.D....

Compare this with the contents of the file above and make sure they're
the same
```


### Run VM with u-root ###

To run with u-root, instead of launching the VM normally, as above in
the setup section, pass the `-kernel` and `-append` arguments to QEMU
to boot directly from the MLE kernel. This first requires that the MLE
kernel is accessible to the test machine. Any time either u-root or the
MLE kernel has changed and the MLE kernel been rebuilt, the MLE kernel
will have to be copied anew (see the "Development Cycle" section
below). The "bootops=\`...\`" part of `-append` will need to be changed
to match the boot arguments to your VM.

Copy and extract the linux-mle RPM to the test machine:
```
$ scp $DEVBOX:~/rpmbuild/RPMS/x86_64/kernel-uek-SL-4.14.35-
1.el7uek.x86_64.rpm ~/
$ rm -Rf ~/linux-mle-rpm
$ mkdir ~/linux-mle-rpm
$ cd ~/linux-mle-rpm
$ rpm2cpio ~/kernel-uek-SL-4.14.35-1.el7uek.x86_64.rpm | cpio -idmv
```

Copy securelaunch.policy to the guest (first time or if the policy file
has changed):
```
$ scp ~/linux-mle-rpm/boot/securelaunch.policy-4.14.35-
1.el7uek.x86_64.SL root@ol7-otec:/boot/securelaunch.policy
```

Launch swtpm:
```
# swtpm socket \
	--tpm2 \
	--tpmstate dir=/usr/share/swtpm/vtpm-ol7-otec \
	--ctrl type=unixio,path=/usr/share/swtpm/vtpm-ol7-otec/swtpm-
sock \
	--log file=/var/log/swtpm/ol7-otec.log,level=20 \
	--pid file=${vtpm_data_path}/swtpm.pid \
	--daemon
```

Launch qemu:
```
# qemu-system-x86_64 -name 'ol7-otec' \
	-enable-kvm -accel kvm -machine q35,smm=on -S \
	-cpu host -smp 4 -m 4096 \
	-global ICH9-LPC.disable_s3=1 \
	-global driver=cfi.pflash01,property=secure,value=on \
	-drive file=/usr/share/OVMF/OVMF_CODE.pure-
efi.fd,index=0,if=pflash,format=raw,readonly \
	-drive file=/mnt/vms/images/kvm/ol7-otec.OVMF_VARS.pure-
efi.fd,index=1,if=pflash,format=raw \
	-drive file=/mnt/vms/images/kvm/ol7-
otec.qcow2,format=qcow2,index=0,media=disk \
	-drive file=/mnt/vms/isos/OracleLinux-R7-U8-Server-x86_64-
dvd.iso,format=raw,media=cdrom \
	-net nic,model=virtio,macaddr=52:54:00:77:9d:06 \
	-net tap,ifname=tap0,downscript=no,vhost=on \
	-debugcon file:ovmf_debug.log -global isa-debugcon.iobase=0x402 
\
	-vnc 0.0.0.0:1 -serial telnet:127.0.0.1:4558,server,nowait
-monitor stdio \
	-kernel ~/linux-mle-rpm/boot/vmlinuz-4.14.35-1.el7uek.x86_64.SL 
\
	-append "console=ttyS0,115200
sl_policy=sda1:/securelaunch.policy uroot.uinitargs=-d
uroot.initflags=\" initrd=/boot/initramfs-4.14.35-
1902.303.5.3.el7uek.x86_64.img linux=/boot/vmlinuz-4.14.35-
1902.303.5.3.el7uek.x86_64 bootops=\'BOOT_IMAGE=/vmlinuz-4.14.35-
1902.303.5.3.el7uek.x86_64 root=/dev/mapper/ol_ol7--otec-root ro
crashkernel=auto rd.lvm.lv=ol_ol7-otec/root rd.lvm.lv=ol_ol7-otec/swap
ip=eno2:dhcp intel_iommu=on pci=realloc iommu=pt LANG=en_US.UTF-8\'
bootdev_uuid=sdc1\"" \
	-chardev socket,id=chrtpm,path=/usr/share/swtpm/vtpm-ol7-
otec/swtpm-sock \
	-tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-
crb,tpmdev=tpm0

```

## Development Cycle ##

The typical development cycle is to fetch/pull new u-root code, rebuild
u-root, fetch/pull the new MLE kernel, rebuild the MLE kernel, and copy
and extract the MLE kernel on the test machine (see "Build u-root and
MLE Kernel" above). The flow is slightly different, as the repos have
already been cloned.


### Development Branch ###

***NOTE:*** The above steps just checkouts the oracle u-root branch,
which includes none of the current development work.

To get the current developement branch, which also includes "hacks" to
get u-root to work with qemu (such as using PCR16 instead of PCR22 for
collectors and skipping the steps that rely on `SENTER`/`SEXIT`). This
branch is somewhat volatile and may be updated with `git push -f`.

On the dev machine, fetch Patrick's "stable" development branch:
```
$ cd $GOPATH/src/github.com/u-root/u-root
$ git remote add pcolp 
https://linux-git.us.oracle.com/uek/linux-pcolp-public.git
$ git fetch pcolp uroot-stable:refs/remotes/pcolp/uroot-stable
$ git checkout -b uroot-stable pcolp/uroot-stable
```


### Development Machine ###

Only if u-root has been updated:
```
$ cd $GOPATH/src/github.com/u-root/u-root
$ git pull
$ rm -Rf ~/rpmbuild/BUILD/u-root-6.0.0
$ git-archive-all --prefix u-root-6.0.0 ~/rpmbuild/SOURCES/u-root-
6.0.0.tar.gz
$ rpmbuild -bb buildrpm/uroot.spec
$ sudo yum reinstall -y ~/rpmbuild/RPMS/x86_64/u-root-6.0.0-
10.el7.x86_64.rpm
```

If there as a forced push (Patrick's branch), then:
```
$ git fetch pcolp
$ git reset --hard pcolp/uroot-stable
```

Only if MLE kernel has been modified:
```
$ cd ~/linux-mle
$ git pull

or if there's a new branch

$ git checkout -b securelaunch-1902-u2-0.9.x origin/securelaunch-1902-
u2-0.9.x
```

In both cases:
```
$ cd ~/linux-mle
$ ./scripts/build_kernel_rpm.sh
```


### Test Machine ###

Just copy the steps for "Run VM with u-root" under the "Test Machine"
section above.



## References ##

<
https://confluence.oci.oraclecorp.com/pages.viewpage.action?pageId=177427019
>
<
https://confluence.oci.oraclecorp.com/pages.viewpage.action?pageId=177426985
>

