#
# Makefile for Audio Mixer Testapp (AudMixT.exe)
# to be compiled for OS/2 with OpenWatcom
#

TARGET_OBJS = &
	AudMixT.obj

CFLAGS = -zq -bm -bt=OS2 -5r -fpi87 -fp5 -otexan -wx

.before:
 @set include=..\AudMixer\include;..\..\tpl\include;$(%include);

.extensions:
.extensions: .obj .c .h

all: Print_Module_Info AudMixT.exe .symbolic .existsonly
	@echo * Make finished, bye!

clean: Print_Module_Info .symbolic
 @if exist *.obj @del *.obj
 @if exist *.lnk @del *.lnk
 @if exist *.map @del *.map
 @if exist *.err @del *.err

Print_Module_Info: .symbolic .existsonly
 @echo --- Audio Mixer Test application (AudMixT)

AudMixT.exe: AudMixT.lnk print_compiling_info $(TARGET_OBJS)
 @echo * Linking EXE...
 @wlink @AudMixT.lnk

print_compiling_info: .symbolic .existsonly
	@echo * Compiling sources...

.c : .\src

.c.obj : .AUTODEPEND
	@wcc386 $[* $(cflags)

AudMixT.lnk: makefile
 @echo * Creating linker description file...
 @echo NAME AudMixT.exe >$^@
 @echo SYSTEM os2v2 >>$^@
 @for %i in ($(TARGET_OBJS)) do @echo FILE %i >>$^@
 @echo LIBRARY ..\AudMixer\AudMixer.lib >>$^@
 @echo LIBRARY ..\..\TPL\tpl.lib >>$^@ 
 @echo OPTION QUIET >>$^@
 @echo OPTION MAP=AudMixT.map >>$^@
 @echo OPTION DESCRIPTION 'Audio Mixer Testapp' >>$^@
 @echo OPTION ELIMINATE >>$^@
 @echo OPTION MANYAUTODATA >>$^@
 @echo OPTION OSNAME='OS/2 and eComStation' >>$^@
 @echo OPTION SHOWDEAD >>$^@
 @echo * Linker file created.
