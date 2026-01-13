// main.cpp - Demo program for xschem_lite library
// Shows how to load a .sch file and generate a SPICE netlist

#include "xschem_lite.h"
#include <iostream>
#include <iomanip>

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " <input.sch> [output.spice] [options]\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  -I <path>           Add symbol search path\n";
    std::cerr << "  --xschemrc <file>   Load symbol paths from xschemrc file\n";
    std::cerr << "  --flat              Generate flat netlist (no .subckt wrapper)\n";
    std::cerr << "  --info              Print schematic info only (no netlist)\n";
    std::cerr << "  -h, --help          Show this help\n\n";
    std::cerr << "Environment variables:\n";
    std::cerr << "  PDK_ROOT            Path to PDK installation (e.g., /home/user/pdk)\n";
    std::cerr << "  PDK                 PDK variant (default: sky130A)\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  " << prog_name << " inverter.sch\n";
    std::cerr << "  " << prog_name << " inverter.sch inverter.spice\n";
    std::cerr << "  " << prog_name << " -I ./symbols top.sch top.spice\n";
    std::cerr << "  " << prog_name << " --xschemrc $PDK_ROOT/sky130A/libs.tech/xschem/xschemrc circuit.sch\n";
}

void print_schematic_info(const xschem::Schematic& sch) {
    std::cout << "=== Schematic Info ===\n";
    std::cout << "File: " << sch.filename << "\n";
    std::cout << "Wires: " << sch.wires.size() << "\n";
    std::cout << "Instances: " << sch.instances.size() << "\n";
    std::cout << "Texts: " << sch.texts.size() << "\n";
    std::cout << "Symbols loaded: " << sch.symbols.size() << "\n";

    std::cout << "\n=== Instances ===\n";
    for (const auto& inst : sch.instances) {
        std::cout << "  " << std::setw(12) << std::left << inst.inst_name
                  << " -> " << inst.symbol_name;

        auto sym_it = sch.symbols.find(inst.symbol_name);
        if (sym_it != sch.symbols.end()) {
            std::cout << " (type: " << sym_it->second.type << ")";
        }
        std::cout << "\n";

        // Print properties
        if (!inst.prop_map.empty()) {
            std::cout << "      props: ";
            bool first = true;
            for (const auto& [key, val] : inst.prop_map) {
                if (!first) std::cout << ", ";
                std::cout << key << "=" << val;
                first = false;
            }
            std::cout << "\n";
        }
    }

    std::cout << "\n=== Wires ===\n";
    for (const auto& wire : sch.wires) {
        std::cout << "  (" << wire.x1 << "," << wire.y1 << ") -> ("
                  << wire.x2 << "," << wire.y2 << ")";
        if (!wire.node.empty()) {
            std::cout << "  [" << wire.node << "]";
        }
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_file;
    std::string output_file;
    std::string xschemrc_file;
    std::vector<std::string> symbol_paths;
    bool subcircuit_mode = true;
    bool info_only = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-I" && i + 1 < argc) {
            symbol_paths.push_back(argv[++i]);
        } else if (arg == "--xschemrc" && i + 1 < argc) {
            xschemrc_file = argv[++i];
        } else if (arg == "--flat") {
            subcircuit_mode = false;
        } else if (arg == "--info") {
            info_only = true;
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        } else if (input_file.empty()) {
            input_file = arg;
        } else if (output_file.empty()) {
            output_file = arg;
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: No input file specified\n";
        print_usage(argv[0]);
        return 1;
    }

    // Load paths from xschemrc if specified
    if (!xschemrc_file.empty()) {
        std::cout << "Loading xschemrc: " << xschemrc_file << "\n";
        auto rc_paths = xschem::parse_xschemrc(xschemrc_file);
        std::cout << "Found " << rc_paths.size() << " symbol paths:\n";
        for (const auto& p : rc_paths) {
            std::cout << "  " << p << "\n";
            symbol_paths.push_back(p);
        }
    }

    // Add default symbol paths (lower priority than xschemrc)
    symbol_paths.push_back(".");
    symbol_paths.push_back("./symbols");
    symbol_paths.push_back("./xschem/xschem_library/devices");
    symbol_paths.push_back("/usr/share/xschem/xschem_library/devices");
    symbol_paths.push_back("/usr/local/share/xschem/xschem_library/devices");

    // Load the schematic
    std::cout << "Loading schematic: " << input_file << "\n";

    xschem::Schematic sch;
    if (!xschem::load_schematic(input_file, sch, symbol_paths)) {
        std::cerr << "Error: Failed to load schematic\n";
        return 1;
    }

    std::cout << "Loaded " << sch.instances.size() << " instances, "
              << sch.wires.size() << " wires\n";

    if (info_only) {
        print_schematic_info(sch);
        return 0;
    }

    // Generate SPICE netlist
    if (output_file.empty()) {
        // Output to stdout
        std::cout << "\n=== SPICE Netlist ===\n";
        if (!xschem::generate_spice_netlist(sch, std::cout, subcircuit_mode)) {
            std::cerr << "Error: Failed to generate netlist\n";
            return 1;
        }
    } else {
        // Output to file
        std::cout << "Generating netlist: " << output_file << "\n";
        if (!xschem::generate_spice_netlist(sch, output_file, subcircuit_mode)) {
            std::cerr << "Error: Failed to generate netlist\n";
            return 1;
        }
        std::cout << "Done.\n";
    }

    return 0;
}
