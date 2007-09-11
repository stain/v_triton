# -----------------------------------------------------------------------------
#
# OpenWatcom makefile to build and clean all the submodules of the current
# directory.
#
# This file basically stores the dependency information between the different
# parts.
#
# -----------------------------------------------------------------------------

# Change the following if you'll have a new top-level directory, or if the
# dependency (the order in which the modules have to be built) changes!

Build_Order = MPAOUT MPVOUT MPAVIDem MPFile MPMP3Dem MPMPADec MPNulOut MPGBM MPAVCOD

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
 @for %i in ($Build_Order) do @cd %i && wmake -h $(%__BUILD_MODE) && cd ..
