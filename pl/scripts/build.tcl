# Vivado 2018.3; invoke with run_vivado.ps1 or: -source scripts/build.tcl -tclargs check|synth|bit 4
set root [file normalize [file join [file dirname [info script]] ..]]
set action [lindex $argv 0]
if {$action eq ""} {set action check}
set jobs [lindex $argv 1]
if {$jobs eq ""} {set jobs 4}
if {$action ni {check synth bit}} {error "Expected check, synth, or bit"}
if {[version -short] ne "2018.3"} {error "Use the original Vivado 2018.3 toolchain; do not upgrade the IP"}
cd $root
set ::env(JFM_PATH) $root
set reports [file join $root verification reports]
file mkdir $reports
open_project [file join $root fpai_demo_vivado_fixed.xpr]
update_compile_order -fileset sources_1

# get_files includes virtual members of XCIX ZIP containers. They need not exist on disk.
set containers [glob -nocomplain -directory [file join $root integration_20261023 generated_ip] *.xcix]
set missing {}
set external {}
set virtual_count 0
set audit [open [file join $reports dependencies.tsv] w]
puts $audit "kind\tpath"
foreach f [get_files -all] {
    set p [file normalize $f]
    if {![string equal -nocase "${root}/" [string range $p 0 [string length $root]]]} {lappend external $p}
    set kind file
    if {![file exists $p]} {
        set covered 0
        foreach c $containers {
            set prefix "[file rootname [file normalize $c]]/"
            if {[string equal -nocase $prefix [string range $p 0 [expr {[string length $prefix]-1}]]]} {
                set covered 1
                break
            }
        }
        if {$covered} {set kind xcix_member; incr virtual_count} else {set kind MISSING; lappend missing $p}
    }
    puts $audit "$kind\t[string range $p [expr {[string length $root]+1}] end]"
}
close $audit
foreach pair {{synth_1 STEPS.SYNTH_DESIGN.TCL.PRE} {impl_1 STEPS.OPT_DESIGN.TCL.PRE} {impl_1 STEPS.OPT_DESIGN.TCL.POST} {impl_1 STEPS.ROUTE_DESIGN.TCL.POST} {impl_1 STEPS.WRITE_BITSTREAM.TCL.PRE} {impl_1 STEPS.WRITE_BITSTREAM.TCL.POST}} {
    lassign $pair run prop
    set hook [get_property $prop [get_runs $run]]
    if {$hook eq "" || ![file exists $hook]} {error "Missing required hook: $run $prop $hook"}
    set hook [file normalize $hook]
    if {![string equal -nocase "${root}/" [string range $hook 0 [string length $root]]]} {lappend external $hook}
    puts "HOOK=$run $prop $hook"
}
puts "DEPENDENCIES files=[llength [get_files -all]] xcix_virtual_members=$virtual_count"
if {[llength $missing] || [llength $external]} {error "Not standalone: missing=$missing external=$external"}
report_ip_status -file [file join $reports ip_status.rpt]
report_compile_order -used_in synthesis -file [file join $reports compile_order.rpt]
check_syntax -fileset sources_1
puts "STANDALONE_CHECK_PASS"

if {$action eq "check"} {close_project; exit 0}
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
set synth_status [get_property STATUS [get_runs synth_1]]
puts "SYNTH_STATUS=$synth_status"
if {![string match "*Complete*" $synth_status]} {error "Synthesis failed: $synth_status"}
puts "STANDALONE_SYNTH_PASS"
if {$action eq "synth"} {close_project; exit 0}

if {![info exists ::env(FMSH_PROCISE_PATH)] || ![file exists [file join $::env(FMSH_PROCISE_PATH) bin procise.exe]]} {
    error "Set FMSH_PROCISE_PATH to the installed Procise root directory"
}
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
set impl_status [get_property STATUS [get_runs impl_1]]
puts "IMPL_STATUS=$impl_status"
if {![string match "*Complete*" $impl_status]} {error "Implementation/bitgen failed: $impl_status"}
set run_dir [get_property DIRECTORY [get_runs impl_1]]
set final_bit [file join $run_dir ai7030_edif_top_disable_icap.bit]
if {![file exists $final_bit] || [file size $final_bit] == 0} {error "Procise post-processing did not produce $final_bit"}
set output [file join $root output build]
file mkdir $output
foreach b [glob -directory $run_dir *.bit] {file copy -force $b $output}
open_run impl_1
report_timing_summary -file [file join $reports timing_summary_routed.rpt]
report_route_status -file [file join $reports route_status.rpt]
report_drc -file [file join $reports drc_routed.rpt]
report_utilization -file [file join $reports utilization_routed.rpt]
report_bus_skew -file [file join $reports bus_skew_routed.rpt]
report_methodology -file [file join $reports methodology_routed.rpt]
set result [open [file join $output build_result.txt] w]
puts $result "Tool: [version -short]\nBuilt: [clock format [clock seconds]]\nSynthesis: $synth_status\nImplementation: $impl_status\nFinal BIT: [file tail $final_bit]\nHardware validation: NOT PERFORMED\nTiming and DRC: see verification/reports; successful bitgen is not a timing waiver."
close $result
puts "STANDALONE_BIT_BUILD_PASS"
close_project
exit 0
