
default: netlists/sky130_fd_sc_hd__dfxtp_1.spice

netlists/%.spice: schematics/%.sch
	mkdir -p $(dir $@)
	mkdir -p $(dir logs/$*)
	SCHEMATIC=$* xschem --no_x --log logs/$*.spice.log --script scripts/xschem_generate_netlist.tcl

clean:
	rm -rf logs netlists
