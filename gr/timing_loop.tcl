# timing_loop.tcl -- the contest evaluation flow (load the design, NanGate45 setRC.tcl), then for each route
# in ROUTES: read_global_route_segments, estimate_parasitics -global_routing, tns / wns / power; and the
# contest endpoint count (registers + outputs). Run from the evaluation folder (timing_loop.py does).

set DESIGN_NAME $::env(DESIGN)
proc load_design {libs lefs def sdc} {
  foreach libFile $libs {
    read_liberty $libFile
  }

  foreach lef $lefs {
    read_lef $lef
  }

  read_def $def
  read_sdc $sdc

}


set libdir "./NanGate45/lib"
set lefdir "./NanGate45/lef"

set LIB_FILES "
    ${libdir}/NangateOpenCellLibrary_typical.lib \
    ${libdir}/fakeram45_256x16.lib \
    ${libdir}/fakeram45_256x32.lib \
    ${libdir}/fakeram45_256x64.lib \
    ${libdir}/fakeram45_32x32.lib \
    ${libdir}/fakeram45_128x256.lib \
    ${libdir}/fakeram45_128x116.lib \
    ${libdir}/fakeram45_128x32.lib \
    ${libdir}/fakeram45_256x48.lib \
    ${libdir}/fakeram45_512x64.lib \
    ${libdir}/fakeram45_64x62.lib \
    ${libdir}/fakeram45_64x64.lib \
    ${libdir}/fakeram45_64x124.lib \
    "

set LEF_FILES "
    ${lefdir}/NangateOpenCellLibrary.tech.lef \
    ${lefdir}/NangateOpenCellLibrary.macro.mod.lef \
    ${lefdir}/fakeram45_256x16.lef \
    ${lefdir}/fakeram45_256x32.lef \
    ${lefdir}/fakeram45_256x64.lef \
    ${lefdir}/fakeram45_32x32.lef \
    ${lefdir}/fakeram45_128x256.lef \
    ${lefdir}/fakeram45_128x116.lef \
    ${lefdir}/fakeram45_128x32.lef \
    ${lefdir}/fakeram45_256x48.lef \
    ${lefdir}/fakeram45_512x64.lef \
    ${lefdir}/fakeram45_64x62.lef \
    ${lefdir}/fakeram45_64x64.lef \
    ${lefdir}/fakeram45_64x124.lef \
    "

set sta_report_default_digits 8

### SET DESIGN ###
# set DESIGN_NAME ariane
set DESIGN_DIR [expr {[info exists ::env(DESIGN_DIR)] ? $::env(DESIGN_DIR) : "./benchmarks/"}]
set DEF_FILE "${DESIGN_DIR}/${DESIGN_NAME}/${DESIGN_NAME}.def"
set DESIGN_DIR [expr {[info exists ::env(DESIGN_DIR)] ? $::env(DESIGN_DIR) : "./benchmarks/"}]
set DEF_FILE "${DESIGN_DIR}/${DESIGN_NAME}/${DESIGN_NAME}.def"
set SDC_FILE "${DESIGN_DIR}/${DESIGN_NAME}/${DESIGN_NAME}.sdc"
load_design $LIB_FILES $LEF_FILES $DEF_FILE $SDC_FILE
source "./NanGate45/setRC.tcl"
# Part 134: one session, many routes -- each listed route is read, its parasitics estimated, and its
# timing and power reported (ROUTES: space-separated paths)
foreach r $::env(ROUTES) {
  read_global_route_segments $r
  estimate_parasitics -global_routing
  puts "ROUTE $r"
  report_tns
  report_wns
  report_power
}
puts "ENDPOINTS [expr {[llength [all_registers]] + [llength [all_outputs]]}]"
