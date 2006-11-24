# -----------------------------------------------------------------------------
#
# OpenWatcom makefile to build and clean all the current Triton parts
#
# This file basically stores the dependency information between the different
# Triton parts.
#
# -----------------------------------------------------------------------------

# Change the following if you'll have a new top-level Triton directory, or
# if the dependency (the order in which the modules have to be built) changes!

Build_Order = TPL AudMix MMIOCore MMIOPlugins Test

# -----------------------------------------------------------------------------

all: Build_All .symbolic .existsonly
clean: Clean_All .symbolic .existsonly

Build_All: Print_Building Process_Build_List Print_Building_Done .symbolic .existsonly
Clean_All: Print_Cleaning Process_Build_List Print_Cleaning_Done .symbolic .existsonly

Print_Building: .symbolic .existsonly
 @echo === Building Triton ===
 @set __BUILD_MODE=

Print_Building_Done: .symbolic .existsonly
 @echo === Building Done   ===

Print_Cleaning: .symbolic .existsonly
 @echo === Cleaning Triton ===
 @set __BUILD_MODE=clean

Print_Cleaning_Done: .symbolic .existsonly
 @echo === Cleaning Done   ===

Process_Build_List: .symbolic .existsonly
 @for %i in ($Build_Order) do @cd %i && wmake -h $(%__BUILD_MODE) && cd ..
