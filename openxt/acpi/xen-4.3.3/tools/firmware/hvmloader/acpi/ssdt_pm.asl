/*
 * ssdt_pm.asl
 *
 * Copyright (c) 2008  Kamala Narasimhan
 * Copyright (c) 2008  Citrix Systems, Inc.
 * 2015 Ross Philipson <philipsonr@ainfosec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * SSDT for extended power management within HVM guest. Power management beyond
 * S3, S4, S5 is handled by this vACPI layer.  
 *
 * Battery Management Implementation -
 * Xen vACPI layer exposes battery information to guest using CMBattery
 * interface. This virtual firmware CMBattery implementation is very similar to
 * the actual firmware CMBattery implementation.  In fact, a good part of the
 * below is heavily borrowed from the underlying firmware to support
 * pass-through and non-pass-through battery management approaches using the
 * same CMBattery interface implementation. When pass-through approach is used,
 * the battery ports are directly mapped using xc_domain_ioport_mapping thus
 * not relying on qemu battery port handling to intercept port reads/writes to
 * feed relevant battery information to the guest.
 *
 * Following are the battery ports read/written to in order to implement
 * battery support:
 * Battery command port - 0xB4
 * Battery data port    - 0x86
 * Battery commands (written to port 0xB2) -
 * 0x7b - Battery operation init
 * 0x7c - Type of battery operation
 * 0x79 - Get battery data length
 * 0x7d - Get battery data
 *
 * Battery status port 0x88
 * 0x01 - Battery 1 (BAT0) present
 * 0x08 - Battery state changes, needs update
 *
 * Battery status port 0x90
 * 0x01 - Battery 2 (BAT1) present
 * 0x08 - Battery state changes, needs update
 *
 * Battery number port 0xB5 - Which battery? i.e. battery 1 or 2 etc.
 *
 * Also the following ports are used for debugging/logging:
 * 0xB040, 0xB044, 0xB046, 0xB048
 *
 * General ACPI status port 0x9C
 * 0x01 - ACPI PM device model support enabled
 * 0x02 - Lid open
 * 0x04 - AC power on
 *
 * N.B. An undesired divergence from upstream was to move the battery command
 * port off of 0xB2/0xB3. These are the legacy Intel APM ports (see R. Brown
 * D/PORT.A). It is unclear how the IO port selection was done originally for
 * this. Perhaps all the ports should be moved our of the low IO port range.
 */

DefinitionBlock ("SSDT_PM.aml", "SSDT", 2, "Xen", "HVM", 0)
{
    Scope (\_SB)
    {
        OperationRegion (DBGA, SystemIO, 0xB040, 0x01)
        Field (DBGA, ByteAcc, NoLock, Preserve)
        {
            DBG1,   8,
        }

        OperationRegion (DBGB, SystemIO, 0xB044, 0x01)
        Field (DBGB, ByteAcc, NoLock, Preserve)
        {
            DBG2,   8,
        }

        OperationRegion (DBGC, SystemIO, 0xB046, 0x01)
        Field (DBGC, ByteAcc, NoLock, Preserve)
        {
            DBG3,   8,
        }

        OperationRegion (DBGD, SystemIO, 0xB048, 0x01)
        Field (DBGD, ByteAcc, NoLock, Preserve)
        {
            DBG4,   8,
        }

        OperationRegion (PRT1, SystemIO, 0xB4, 0x01)
        Field (PRT1, ByteAcc, NoLock, Preserve)
        {
            PB4,   8,
        }

        OperationRegion (PRT2, SystemIO, 0x86, 0x01)
        Field (PRT2, ByteAcc, NoLock, Preserve)
        {
            P86,   8
        }

        OperationRegion (PRT3, SystemIO, 0x88, 0x01)
        Field (PRT3, ByteAcc, NoLock, Preserve)
        {
            P88,  8
        }

        OperationRegion (PRT4, SystemIO, 0x90, 0x01)
        Field (PRT4, ByteAcc, NoLock, Preserve)
        {
            P90,  8
        }

        OperationRegion (PRT5, SystemIO, 0xB5, 0x01)
        Field (PRT5, ByteAcc, NoLock, Preserve)
        {
            PB5,   8,
        }

        OperationRegion (PSTS, SystemIO, 0x9C, 0x01)
        Field (PSTS, ByteAcc, NoLock, Preserve)
        {
            P9C,   8,
        }

        /* OperationRegion for Power Button */
        OperationRegion (PBOP, SystemIO, 0x200, 0x01)
        Field (PBOP, ByteAcc, NoLock, WriteAsZeros)
        {
            SLP, 1,
            WAK, 1
        }

        Mutex (SYNC, 0x01)
        Name (BUF0, Buffer (0x0100) {})
        Name (BUF1, Buffer (0x08) {})
        CreateWordField (BUF1, 0x00, BUFA)
        CreateWordField (BUF1, 0x04, BUFB)
        Method (ACQR, 0, NotSerialized)
        {
            Acquire (SYNC, 0xFFFF)
            Store (0x00, BUFA)
        }

        /*
         * Initialize relevant buffer to indicate what type of
         * information is being queried and by what object (e.g.
         * by battery device 0 or 1).
         */
        Method (INIT, 1, NotSerialized)
        {
            Store (BUFA, Local0)
            Increment (Local0)
            If (LLessEqual (Local0, SizeOf (BUF0)))
            {
                CreateByteField (BUF0, BUFA, TMP1)
                Store (Arg0, TMP1)
                Store (Local0, BUFA)
            }
        }

        /*
         * Write to battery port 0xb2 indicating the type of information
         * to request, initialize battery data port 0x86 and then return 
         * value provided through data port 0x86.
         */
        Method (WPRT, 2, NotSerialized)
        {
            Store (Arg1, \_SB.P86)
            Store (Arg0, \_SB.PB4)
            Store (Arg0, \_SB.DBG2)
            Store (Arg1, \_SB.DBG4)
            Store (\_SB.PB4, Local0)
            While (LNotEqual (Local0, 0x00))
            {
                Store (\_SB.PB4, Local0)
            }

            Store (\_SB.P86, Local1)
            Store (Local1, \_SB.DBG3)
            Return (Local1)
        }

        /*
         * Helper method 1 to write to battery command and data port. 
         * 0x7c written to port 0xb2 indicating battery info type command.
         * Value 1 or 2 written to port 0x86.  1 for BIF (batterry info) and 2 
         * for BST (battery status).
         */
        Method (HLP1, 2, NotSerialized)
        {
            If (LLess (Arg1, SizeOf (Arg0)))
            {
                CreateByteField (Arg0, Arg1, TMP1)
                WPRT (0x7C, TMP1)
            }
        }

        /*
         * Helper method 2.  Value 0x7b written to battery command port 0xb2
         * indicating battery info initialization request.  First thing written
         * to battery port before querying for further information pertaining
         * to the battery.
         */
        Method (HLP2, 0, NotSerialized)
        {
            WPRT (0x7B, 0x00)
            Store (0x00, Local0)
            While (LLess (Local0, BUFA))
            {
                HLP1 (BUF0, Local0)
                Increment (Local0)
            }
        }

        /*
         * Helper method 3. 0x7d written to battery command port 0xb2
         * indicating request of battery data returned through battery data
         * port 0x86.
         */
        Method (HLP3, 2, NotSerialized)
        {
            If (LLess (Arg1, SizeOf (Arg0)))
            {
                CreateByteField (Arg0, Arg1, TMP1)
                Store (WPRT (0x7D, 0x00), TMP1)
            }
        }

        /*
         * Helper method 4 to indirectly get battery data and store it in a
         * local buffer.
         */
        Method (HLP4, 0, NotSerialized)
        {
            Store (0x00, Local0)
            While (LLess (Local0, BUFB))
            {
                Add (BUFA, Local0, Local1)
                HLP3 (BUF0, Local1)
                Increment (Local0)
            }
        }

        /*
         * Helper method 5 to indirectly initialize battery port and get
         * battery data. Also get battery data length by writing 0x79 to
         * battery command port and receiving battery data length in port 0x86.
         */
        Method (HLP5, 0, NotSerialized)
        {
            HLP2 ()
            Store (WPRT (0x79, 0x00), BUFB)
            Add (BUFA, BUFB, Local0)
            If (LLess (SizeOf (BUF0), Local0))
            {
                Store (SizeOf (BUF0), Local0)
                Subtract (Local0, BUFA, Local0)
                Store (Local0, BUFB)
            }

            HLP4 ()
        }

        /* Helper method for local buffer housekeeping... */
        Method (HLP6, 0, NotSerialized)
        {
            Store (BUFA, Local0)
            Increment (Local0)
            If (LLessEqual (Local0, SizeOf (BUF0)))
            {
                CreateByteField (BUF0, BUFA, TMP1)
                Store (Local0, BUFA)
                Return (TMP1)
            }

            Return (0x00)
        }

        /* Helper methods to help store battery data retrieved through
         * battery data port 0x86. */

        Method (HLP7, 0, NotSerialized)
        {
            Store (BUFA, Local0)
            Add (Local0, 0x04, Local0)
            If (LLessEqual (Local0, SizeOf (BUF0)))
            {
                CreateDWordField (BUF0, BUFA, SX22)
                Store (Local0, BUFA)
                Return (SX22)
            }

            Return (0x00)
        }

        Method (HLP8, 2, NotSerialized)
        {
            If (LLess (Arg1, SizeOf (Arg0)))
            {
                CreateByteField (Arg0, Arg1, TMP1)
                Store (HLP6 (), TMP1)
            }
        }

        Method (HLP9, 2, NotSerialized)
        {
            Store (0x00, Local0)
            While (LLess (Local0, Arg1))
            {
                HLP8 (Arg0, Local0)
                Increment (Local0)
            }
        }

        Method (HLPA, 0, NotSerialized)
        {
            Store (HLP6 (), Local0)
            Name (TMP, Buffer (Local0) {})
            HLP9 (TMP, Local0)
            Return (TMP)
        }

        Method (REL, 0, NotSerialized)
        {
            Release (SYNC)
        }

        Method (E05, 0, NotSerialized)
        {
            If (\_SB.SLP)
            {
                Store (One, \_SB.SLP)
                Notify (\_SB.SLPB, 0x80)
            }

            if (\_SB.WAK)
            {
                Store (One, \_SB.WAK)
                Notify (\_SB.SLPB, 0x2)
            }
        }

        Method (E06, 0, NotSerialized)
        {
            If (\_SB.SLP)
            {
                Store (One, \_SB.SLP)
                Notify (\_SB.PBTN, 0x80)
            }

            if (\_SB.WAK)
            {
                Store (One, \_SB.WAK)
                Notify (\_SB.PBTN, 0x2)
            }
        }

        Method (E07, 0, NotSerialized)
        {
            Notify (\_SB.LID, 0x80)
        }

        Method (E0C, 0, NotSerialized)
        {
            Notify (\_SB.AC, 0x80)
        }

        Method (E0D, 0, NotSerialized)
        {
            Notify(\_SB.BAT0, 0x80)
            Notify(\_SB.BAT1, 0x80)
        }

        Method (E0E, 0, NotSerialized)
        {
            Notify(\_SB.BAT0, 0x81)
            Notify(\_SB.BAT1, 0x81)
        }

        Device (LID)
        {
            Name (_HID, EisaId ("PNP0C0D"))
            Method (_LID, 0, NotSerialized)
            {
                Store (\_SB.P9C, Local0)
                If (And (Local0, 0x2))
                {
                    Return (0x1)
                }

                Return (0x0)
            }

            Method (_PRW, 0, NotSerialized)
            {
                Return (Package (0x02)
                {
                    0x07,
                    0x03
                })
            }

            Method (_PSW, 1, NotSerialized)
            {
                Store (\_SB.P9C, Local0)
                If (And (Local0, 0x2))
                {
                    Return (0x1)
                }
                Return (0x0)
            }
        }

        Device (PBTN)
        {
            Name (_HID, EisaId ("PNP0C0C"))

            Name (_PRW, Package (0x02)
            {
                0x01,
                0x04
            })
        }

        Device (SLPB)
        {
            Name (_HID, EisaId ("PNP0C0E"))

            Name (_PRW, Package (0x02)
            {
                0x01,
                0x04
            })
        }

        Device (AC)
        {
            Name (_HID, "ACPI0003")
            Name (_PCL, Package (0x03)
            {
                \_SB,
                BAT0,
                BAT1 
            })
            Method (_PSR, 0, NotSerialized)
            {
                Store (\_SB.P9C, Local0)
                If (And (Local0, 0x4))
                {
                    Return (0x1)
                }

                Return (0x0)
            }

            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }

        /* Main battery information helper method. */
        Name (BIFP, Package (0x0D) {})
        Method (BIF, 1, NotSerialized)
        {
            ACQR ()
            INIT (0x01)
            INIT (Arg0)
            Store (Arg0, PB5)
            HLP5 ()
            Store (HLP7 (), Index (BIFP, 0x00))
            Store (HLP7 (), Index (BIFP, 0x01))
            Store (HLP7 (), Index (BIFP, 0x02))
            Store (HLP7 (), Index (BIFP, 0x03))
            Store (HLP7 (), Index (BIFP, 0x04))
            Store (HLP7 (), Index (BIFP, 0x05))
            Store (HLP7 (), Index (BIFP, 0x06))
            Store (HLP7 (), Index (BIFP, 0x07))
            Store (HLP7 (), Index (BIFP, 0x08))
            Store (HLPA (), Index (BIFP, 0x09))
            Store (HLPA (), Index (BIFP, 0x0A))
            Store (HLPA (), Index (BIFP, 0x0B))
            Store (HLPA (), Index (BIFP, 0x0C))
            REL ()
            Return (BIFP)
        }

        /* Helper routines to get status and notify on changes */
        Method (STA1, 0, NotSerialized)
        {
            Store (\_SB.P88, Local0)
            /* Check for battery changed indication */
            If (And(Local0, 0x80))
            {
                /* Generate event for BAT0 */
                Notify (\_SB.BAT0, 0x81)
            }
        }

        Method (STA2, 0, NotSerialized)
        {
            Store (\_SB.P90, Local0)
            /* Check for battery changed indication */
            If (And(Local0, 0x80))
            {
                /* Generate event for BAT1 */
                Notify (\_SB.BAT1, 0x81)
            }
        }

        /* Battery object 0 */
        Device (BAT0)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, 0x01)
            Name (_PCL, Package (0x01)
            {
                \_SB
            })

            Method (_STA, 0, NotSerialized)
            {
                Store (\_SB.P88, Local0)
                If (And (Local0, 0x1))
                {
                    Return (0x1F)
                }

                Return (0x0F)
	    }

            /* Battery generic info: design capacity, voltage, model # etc. */
            Method (_BIF, 0, NotSerialized)
            {
                //Store (1, \_SB.DBG1)
                Store(BIF ( 0x01 ), Local0)
                //Store (2, \_SB.DBG1)
                Return( Local0 )
            }

            /* Battery status including battery charging/discharging rate. */
            Method (_BST, 0, NotSerialized)
            {
                Store (1, \_SB.DBG1)
                STA1 ()
                ACQR ()
                INIT (0x02)
                INIT (0x01)
                Store (0x01, PB5)
                HLP5 ()
                Name (BST0, Package (0x04) {})
                Store (HLP7 (), Index (BST0, 0x00))
                Store (HLP7 (), Index (BST0, 0x01))
                Store (HLP7 (), Index (BST0, 0x02))
                Store (HLP7 (), Index (BST0, 0x03))
                REL ()
                Store (2, \_SB.DBG1)
                Return (BST0)
            }
        }

        /* Battery object 1 */
        Device (BAT1)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, 0x02)
            Name (_PCL, Package (0x01)
            {
                \_SB
            })
            Method (_STA, 0, NotSerialized)
            {
                Store (\_SB.P90, Local0)
                If (And (Local0, 0x1))
                {
                    Return (0x1F)
                }

                Return (0x0F)
            }

            Method (_BIF, 0, NotSerialized)
            {
                Store (BIF (0x02), Local0)
                Return( Local0 )
            }

            Method (_BST, 0, NotSerialized)
            {
	        /* Check for BIF changes */
                STA2 ()
                ACQR ()
                INIT (0x02)
                INIT (0x02)
                Store (0x02, PB5)
                HLP5 ()
                Name (BST1, Package (0x04) {})
                Store (HLP7 (), Index (BST1, 0x00))
                Store (HLP7 (), Index (BST1, 0x01))
                Store (HLP7 (), Index (BST1, 0x02))
                Store (HLP7 (), Index (BST1, 0x03))
                REL ()
                Return (BST1)
            }
        }
    }

    /*  Wire GPE events to notify power state
     *  changes like ac power to battery use etc.
     */
    Scope (\_GPE)
    {
        /* Note:  If we run out of level event handlers or there should be a conflict
         *        in future, we could consolidate the below under one handler and use
         *        an io port to get the type of event.
         */

        Method (_L05, 0, NotSerialized)
        {
            \_SB.E05()
        }

        Method (_L06, 0, NotSerialized)
        {
            \_SB.E06()
        }

        Method (_L07, 0, NotSerialized)
        {
            \_SB.E07()
        }

        Method (_L0C, 0, NotSerialized)
        {
            \_SB.E0C()
        }

        Method (_L0D, 0, NotSerialized)
        {
            \_SB.E0D()
        }

        Method (_L0E, 0, NotSerialized)
        {
            \_SB.E0E()
        }
    }
}

