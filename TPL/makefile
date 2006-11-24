#
# Makefile for Triton Porting Layer to be compiled for OS/2 with OpenWatcom
#

TPL_OBJS = &
	tpl_cevsem_os2.obj &
	tpl_dloader_os2.obj &
	tpl_evsem_os2.obj &
	tpl_hrtimer_os2.obj &
	tpl_msgq_os2.obj &
	tpl_mtxsem_os2.obj &
	tpl_sched_os2.obj &
	tpl_thread_os2.obj

CFLAGS = -zq -bm -bd -bt=OS2 -5r -fpi87 -fp5 -otexan -wx -DBUILD_TPL

.before:
 @set include=.\include;$(%include);

.extensions:
.extensions: .obj .c .h

all: Print_Module_Info tpl.dll .symbolic .existsonly
	@echo * Make finished, bye!

clean: Print_Module_Info .symbolic
 @if exist *.obj @del *.obj
 @if exist *.lnk @del *.lnk
 @if exist *.map @del *.map
 @if exist *.err @del *.err

Print_Module_Info: .symbolic .existsonly
 @echo --- Triton Porting Layer library (TPL)

tpl.dll: tpl.lnk print_compiling_info $(TPL_OBJS)
 @echo * Linking DLL...
 @wlink @tpl.lnk
 @echo * Creating .lib from .dll...
 @implib tpl.lib tpl.dll >NUL

print_compiling_info: .symbolic .existsonly
	@echo * Compiling sources...

.c : .\src

.c.obj : .AUTODEPEND
	@wcc386 $[* $(cflags)
        
tpl.lnk: makefile
 @echo * Creating linker description file...
 @echo NAME tpl.dll >$^@
 @echo SYSTEM os2v2_dll INITINSTANCE TERMINSTANCE >>$^@
 @for %i in ($(TPL_OBJS)) do @echo FILE %i >>$^@
 @echo OPTION QUIET >>$^@
 @echo OPTION MAP=tpl.map >>$^@
 @echo OPTION DESCRIPTION 'Triton Porting Layer (TPL)' >>$^@
 @echo OPTION ELIMINATE >>$^@
 @echo OPTION MANYAUTODATA >>$^@
 @echo OPTION OSNAME='OS/2 and eComStation' >>$^@
 @echo OPTION SHOWDEAD >>$^@
 @echo * Linker file created.
