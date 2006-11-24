#
# Makefile for AudMixer.dll to be compiled for OS/2 with OpenWatcom
#

TARGET_OBJS = &
	AudMixer.obj

CFLAGS = -zq -bm -bd -bt=OS2 -5r -fpi87 -fp5 -otexan -wx -DBUILD_AUDMIXER

.before:
 @set include=.\include;..\..\tpl\include;$(%include);

.extensions:
.extensions: .obj .c .h

all: Print_Module_Info AudMixer.dll .symbolic .existsonly
	@echo * Make finished, bye!

clean: Print_Module_Info .symbolic
 @if exist *.obj @del *.obj
 @if exist *.lnk @del *.lnk
 @if exist *.map @del *.map
 @if exist *.err @del *.err

Print_Module_Info: .symbolic .existsonly
 @echo --- Audio Mixer Library (AudMixer)

AudMixer.dll: AudMixer.lnk print_compiling_info $(TARGET_OBJS)
 @echo * Linking DLL...
 @wlink @AudMixer.lnk
 @echo * Creating .lib from .dll...
 @implib AudMixer.lib AudMixer.dll >NUL

print_compiling_info: .symbolic .existsonly
	@echo * Compiling sources...

.c : .\src

.c.obj : .AUTODEPEND
	@wcc386 $[* $(cflags)
        
AudMixer.lnk: makefile
 @echo * Creating linker description file...
 @echo NAME AudMixer.dll >$^@
 @echo SYSTEM os2v2_dll INITINSTANCE TERMINSTANCE >>$^@
 @for %i in ($(TARGET_OBJS)) do @echo FILE %i >>$^@
 @echo LIBRARY ..\..\TPL\tpl.lib >>$^@
 @echo LIB mmpm2.lib >>$^@
 @echo LIBPATH %os2tk%\lib >>$^@
 @echo OPTION QUIET >>$^@
 @echo OPTION MAP=AudMixer.map >>$^@
 @echo OPTION DESCRIPTION 'Audio Mixer API' >>$^@
 @echo OPTION ELIMINATE >>$^@
 @echo OPTION MANYAUTODATA >>$^@
 @echo OPTION OSNAME='OS/2 and eComStation' >>$^@
 @echo OPTION SHOWDEAD >>$^@
 @echo SEGMENT GLOBAL_DATA_SEG SHARED >>$^@
 @echo * Linker file created.
