/*
 * SSDT-DGPU-OFF: power the hidden NVIDIA dGPU (RTX 5050, 10de:2d98, \_SB.PC00.PEG1.PEGP) down at boot.
 *
 * macOS has no driver for it, so left alone it sits powered at default clocks (PCIe Gen5 x8 link up) and heats the chassis.
 * The firmware's own switch is PEG1.PXP._OFF (SSDT "OptRf1"/"Opt1Tabl"), but that starts by reading/writing EC RAM 0x520 through
 * \_SB.PC00.LPCB.EC0.RRAM/WRAM, which are created by a dynamically loaded table and are not available this early (and Apple's EC
 * driver is not up yet), so a call could abort before reaching the hardware. This calls the two real hardware routines directly:
 *   PEG1.DL23 ()  root port into L2/L3 (link down)         [SSDT-4: sets L23E, waits for it to clear]
 *   PEG1.POFF ()  assert PERST (RSTG), drop the power-enable GPIO (PWRG), stop the reference clock (SCLK)   [SSDT-7]
 * the same sequence PXP._OFF uses on its non-GC6 path (HGMD/NVIDIA checks aside), minus the EC step.
 * Guarded by PSTA (reset not asserted = card currently powered), so it is a no-op if the card is already off.
 * Undo: remove the entry from ACPI -> Add (or boot the safe config).
 */
DefinitionBlock ("", "SSDT", 2, "XEGPU ", "DGPUOFF", 0x00001000)
{
    External (_SB_.PC00.PEG1, DeviceObj)
    External (_SB_.PC00.PEG1.PEGP, DeviceObj)
    External (_SB_.PC00.PEG1.PEGP.LTRE, IntObj)
    External (_SB_.PC00.PEG1.LREN, FieldUnitObj)
    External (_SB_.PC00.PEG1.DL23, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.POFF, MethodObj)    // 0 Arguments
    External (_SB_.PC00.PEG1.PSTA, MethodObj)    // 0 Arguments

    Scope (\_SB.PC00.PEG1.PEGP)
    {
        Method (_INI, 0, NotSerialized)
        {
            If (CondRefOf (\_SB.PC00.PEG1.POFF))
            {
                If ((\_SB.PC00.PEG1.PSTA () == One))
                {
                    \_SB.PC00.PEG1.PEGP.LTRE = \_SB.PC00.PEG1.LREN
                    \_SB.PC00.PEG1.DL23 ()
                    \_SB.PC00.PEG1.POFF ()
                }
            }
        }
    }
}
