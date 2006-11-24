#
# Makefile for MMIO Core library (MMIOCore.DLL)
# to be compiled for OS/2 with OpenWatcom
#

TARGET_OBJS = &
	MMIOMem.obj &
	MMIOCore.obj

CFLAGS = -zq -bm -bd -bt=OS2 -5r -fpi87 -fp5 -otexan -wx -DBUILD_MMIO

.before:
 @set include=.\include;..\tpl\include;$(%include);

.extensions:
.extensions: .obj .c .h

all: Print_Module_Info MMIOCore.dll .symbolic .existsonly
	@echo * Make finished, bye!

clean: Print_Module_Info .symbolic
 @if exist *.obj @del *.obj
 @if exist *.lnk @del *.lnk
 @if exist *.map @del *.map
 @if exist *.err @del *.err

Print_Module_Info: .symbolic .existsonly
 @echo --- MMIO Core library (MMIOCore)

MMIOCore.dll: MMIOCore.lnk print_compiling_info $(TARGET_OBJS)
 @echo * Linking DLL...
 @wlink @MMIOCore.lnk
 @echo * Creating .lib from .dll...
 @implib MMIOCore.lib MMIOCore.dll >NUL

print_compiling_info: .symbolic .existsonly
	@echo * Compiling sources...

.c : .\src

.c.obj : .AUTODEPEND
	@wcc386 $[* $(cflags)

MMIOCore.lnk: makefile
 @echo * Creating linker description file...
 @echo NAME MMIOCore.dll >$^@
 @echo SYSTEM os2v2_dll INITINSTANCE TERMINSTANCE >>$^@
 @for %i in ($(TARGET_OBJS)) do @echo FILE %i >>$^@
 @echo LIBRARY ..\TPL\tpl.lib >>$^@
 @echo OPTION QUIET >>$^@
 @echo OPTION MAP=MMIOCore.map >>$^@
 @echo OPTION DESCRIPTION 'MMIO Core library' >>$^@
 @echo OPTION ELIMINATE >>$^@
 @echo OPTION MANYAUTODATA >>$^@
 @echo OPTION OSNAME='OS/2 and eComStation' >>$^@
 @echo OPTION SHOWDEAD >>$^@
 @echo * Linker file created.
