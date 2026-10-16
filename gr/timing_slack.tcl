# timing_slack.tcl -- the contest evaluation flow (load the design, NanGate45 setRC.tcl), then one route
# scored and its failing-path nets' routed slack written out (gr/timing_loop.py, stage 3). Run from the
# evaluation folder.


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
# Part 134 (stage 3 of gr/timing_loop.py): one route -- its tns / wns / power, then every net on the
# worst path of each failing endpoint with the worst slack (ns) among those paths, to SLACK_OUT
# ("<net name> <slack>" per line). ROUTE, SLACK_OUT from the environment.
read_global_route_segments $::env(ROUTE)
estimate_parasitics -global_routing
puts "ROUTE $::env(ROUTE)"
report_tns
report_wns
report_power
set worst [dict create]
foreach p [find_timing_paths -path_delay max -group_count 100000000 -endpoint_count 1 -slack_max 0.0] {
  set s [get_property $p slack]
  foreach pt [get_property $p points] {
    set n [get_nets -quiet -of_objects [get_property $pt pin]]
    if {$n eq ""} { continue }
    set nm [get_full_name $n]
    if {![dict exists $worst $nm] || $s < [dict get $worst $nm]} { dict set worst $nm $s }
  }
}
set f [open $::env(SLACK_OUT) w]
dict for {nm s} $worst { puts $f "$nm $s" }
close $f
puts "SLACKNETS [dict size $worst]"
