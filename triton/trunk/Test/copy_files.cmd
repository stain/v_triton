@echo off
md .MMIO

copy ..\MMIOPlugins\MPAOUT\*.dll .MMIO
copy ..\MMIOPlugins\MPAVIDem\*.dll .MMIO
copy ..\MMIOPlugins\MPFile\*.dll .MMIO
copy ..\MMIOPlugins\MPGBM\*.dll .MMIO
copy ..\MMIOPlugins\MPMP3Dem\*.dll .MMIO
copy ..\MMIOPlugins\MPMPADec\*.dll .MMIO
copy ..\MMIOPlugins\MPNulOut\*.dll .MMIO

copy ..\MMIOCore\MMIOCore.dll
copy ..\TPL\TPL.DLL
copy ..\audmix\audmixer\audmixer.dll
