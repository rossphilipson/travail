#! /bin/sh
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

rm -r tpm.h
rm -r early_tpm.h
rm -r early_tpm.c
cp tpm_template.h tpm.h
cp early_tpm_template.h early_tpm.h
cp early_tpm_template.c early_tpm.c

echo "/*** tpm.h ***/" >> tpm.h
./process_file.awk -v header=1 ../include/tpm.h >> tpm.h
echo "/*** tpmbuff.h ***/" >> tpm.h
./process_file.awk -v header=1 ../include/tpmbuff.h >> tpm.h
echo "" >> tpm.h
echo "#endif" >> tpm.h
echo "Finished tpm.h"

echo "/*** tpm_common.h ***/" >> early_tpm.h
./process_file.awk -v header=1 ../tpm_common.h >> early_tpm.h
echo "/*** tis.h ***/" >> early_tpm.h
./process_file.awk -v header=1 ../tis.h >> early_tpm.h
echo "/*** crb.h ***/" >> early_tpm.h
./process_file.awk -v header=1 ../crb.h >> early_tpm.h
echo "/*** tpm1.h ***/" >> early_tpm.h
./process_file.awk -v header=1 ../tpm1.h >> early_tpm.h
echo "/*** tpm2.h ***/" >> early_tpm.h
./process_file.awk -v header=1 ../tpm2.h >> early_tpm.h
echo "/*** tpm2_constants.h ***/" >> early_tpm.h
./process_file.awk -v header=1 ../tpm2_constants.h >> early_tpm.h
echo "/*** tpm2_auth.h ***/" >> early_tpm.h
./process_file.awk -v header=1 ../tpm2_auth.h >> early_tpm.h
echo "" >> tpm.h
echo "#endif" >> early_tpm.h
echo "Finished early_tpm.h"

echo "/*** tpm_buff.c ***/" >> early_tpm.c
./process_file.awk ../tpm_buff.c >> early_tpm.c
echo "" >> early_tpm.c
echo "/*** tpmio.c ***/" >> early_tpm.c
./process_file.awk tpmio.c >> early_tpm.c
echo "" >> early_tpm.c
echo "/*** tis.c ***/" >> early_tpm.c
./process_file.awk ../tis.c >> early_tpm.c
echo "" >> early_tpm.c
echo "/*** crb.c ***/" >> early_tpm.c
./process_file.awk ../crb.c >> early_tpm.c
echo "" >> early_tpm.c
echo "/*** tpm1_cmds.c ***/" >> early_tpm.c
./process_file.awk ../tpm1_cmds.c >> early_tpm.c
echo "" >> early_tpm.c
echo "/*** tpm2_auth.c ***/" >> early_tpm.c
./process_file.awk ../tpm2_auth.c >> early_tpm.c
echo "" >> early_tpm.c
echo "/*** tpm2_cmds.c ***/" >> early_tpm.c
./process_file.awk ../tpm2_cmds.c >> early_tpm.c
echo "" >> early_tpm.c
echo "/*** tpm.c ***/" >> early_tpm.c
./process_file.awk ../tpm.c >> early_tpm.c
echo "Finished early_tpm.c"
