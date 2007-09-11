#
# Makefile for the multiple formats decoder plugin to be compiled for OS/2 with OpenWatcom
#

PLUGIN_NAME = MPAVCOD
PLUGIN_DESC = Multiple Formats Decoder plugin for MMIO (using libavcodec and libavutil from FFMPEG)

PLUGIN_OBJS = &
	mpavcod.obj

CFLAGS = -zq -bm -bd -bt=OS2 -5r -fpi87 -fp5 -otexan -wx -ecc

.before:
 @set include=..\..\MMIOCore\include;..\..\tpl\include;.\src;.\src\ffmpeg\h;$(%include);

.extensions:
.extensions: .obj .c .h

all: Print_Module_Info $(PLUGIN_NAME).dll .symbolic .existsonly
	@echo * Make finished, bye!

clean: Print_Module_Info .symbolic
 @if exist *.obj @del *.obj
 @if exist *.lnk @del *.lnk
 @if exist *.map @del *.map
 @if exist *.err @del *.err

Print_Module_Info: .symbolic .existsonly
 @echo --- $(PLUGIN_DESC) ($(PLUGIN_NAME))

$(PLUGIN_NAME).dll: $(PLUGIN_NAME).lnk print_compiling_info $(PLUGIN_OBJS)
 @echo * Linking DLL...
 @wlink @$(PLUGIN_NAME).lnk

print_compiling_info: .symbolic .existsonly
	@echo * Compiling sources...

.c : .\src

.c.obj : .AUTODEPEND
	@wcc386 $[* $(cflags)
        
$(PLUGIN_NAME).lnk: makefile
 @echo * Creating linker description file...
 @echo NAME $(PLUGIN_NAME).dll >$^@
 @echo SYSTEM os2v2_dll initinstance terminstance >>$^@
 @for %i in ($(PLUGIN_OBJS)) do @echo FILE %i >>$^@
 @echo LIBRARY ..\..\MMIOCore\MMIOCore.lib >>$^@
 @echo LIBRARY ..\..\TPL\tpl.lib >>$^@
 @echo LIBRARY src\ffmpeg\ffmpeg.lib >>$^@
 @echo OPTION QUIET >>$^@
 @echo OPTION MAP=$(PLUGIN_NAME).map >>$^@
 @echo OPTION DESCRIPTION '$(PLUGIN_DESC)' >>$^@
 @echo OPTION ELIMINATE >>$^@
 @echo OPTION MANYAUTODATA >>$^@
 @echo OPTION OSNAME='OS/2 and eComStation' >>$^@
 @echo OPTION SHOWDEAD >>$^@
 @echo * Linker file created.
