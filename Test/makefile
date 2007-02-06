# -----------------------------------------------------------------------------
#
# OpenWatcom makefile to build and clean all the sub-makefiles of the current
# directory.
#
# -----------------------------------------------------------------------------

Build_Order = mmioinit mmiotest imagetest urlinfo

# -----------------------------------------------------------------------------

all: Build_All .symbolic .existsonly
clean: Clean_All .symbolic .existsonly

Build_All: Set_Building Process_Build_List .symbolic .existsonly
Clean_All: Set_Cleaning Process_Build_List .symbolic .existsonly

Set_Building: .symbolic .existsonly
 @set __BUILD_MODE=

Set_Cleaning: .symbolic .existsonly
 @set __BUILD_MODE=clean

Process_Build_List: .symbolic .existsonly
 @for %i in ($Build_Order) do @wmake -h -f makefile.%i $(%__BUILD_MODE)
