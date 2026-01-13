// xschem_lite.h - Lightweight xschem .sch parser and SPICE netlister
// Minimal implementation for parsing .sch files and generating SPICE netlists
// without the full xschem GUI dependencies.

#ifndef XSCHEM_LITE_H
#define XSCHEM_LITE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <memory>

namespace xschem {

// Forward declarations
struct Wire;
struct Instance;
struct Text;
struct Pin;
struct Symbol;
struct Schematic;

// A wire/net segment in the schematic
struct Wire {
    double x1, y1, x2, y2;
    std::string node;      // Assigned net name
    std::string props;     // Property string
    bool is_bus = false;
};

// A text label in the schematic
struct Text {
    std::string text;
    double x, y;
    int rot = 0;
    int flip = 0;
    double xscale = 1.0, yscale = 1.0;
    std::string props;
};

// A pin definition (from symbol files)
struct Pin {
    std::string name;
    std::string direction;  // "in", "out", "inout"
    double x, y;
};

// A component instance
struct Instance {
    std::string symbol_name;     // Symbol file name (e.g., "nmos4.sym")
    std::string inst_name;       // Instance name (e.g., "M1")
    double x, y;
    int rot = 0;
    int flip = 0;
    std::string props;           // Property string
    std::vector<std::string> connected_nets;  // Nets connected to each pin

    // Parsed properties cache
    std::unordered_map<std::string, std::string> prop_map;
};

// Symbol definition (loaded from .sym files)
struct Symbol {
    std::string name;
    std::string type;            // "subcircuit", "primitive", etc.
    std::vector<Pin> pins;
    std::string format;          // SPICE format string
    std::string template_str;    // Default property template
    std::string props;

    // Bounding box
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
};

// A point for connectivity tracking
struct Point {
    double x, y;

    bool operator==(const Point& other) const {
        return std::abs(x - other.x) < 0.01 && std::abs(y - other.y) < 0.01;
    }
};

struct PointHash {
    size_t operator()(const Point& p) const {
        // Round to avoid floating point issues
        long long ix = static_cast<long long>(p.x * 100);
        long long iy = static_cast<long long>(p.y * 100);
        return std::hash<long long>()(ix) ^ (std::hash<long long>()(iy) << 1);
    }
};

// Main schematic container
struct Schematic {
    std::string filename;
    std::string version;

    // Schematic properties
    std::string K_props;  // K {} block - type info
    std::string G_props;  // G {} block
    std::string V_props;  // V {} block
    std::string S_props;  // S {} block
    std::string E_props;  // E {} block

    std::vector<Wire> wires;
    std::vector<Instance> instances;
    std::vector<Text> texts;

    // Loaded symbols
    std::unordered_map<std::string, Symbol> symbols;

    // Net names
    std::unordered_map<std::string, int> net_names;
    int unnamed_net_count = 0;
};

// Utility functions
std::string get_tok_value(const std::string& props, const std::string& key);
std::string trim(const std::string& s);
std::unordered_map<std::string, std::string> parse_props(const std::string& props);

// Main parser class
class SchematicParser {
public:
    SchematicParser() = default;

    // Load a schematic file
    bool load(const std::string& filename);

    // Load a symbol file
    bool load_symbol(const std::string& symbol_name);

    // Get the loaded schematic
    const Schematic& schematic() const { return m_sch; }
    Schematic& schematic() { return m_sch; }

    // Set symbol search paths
    void add_symbol_path(const std::string& path) { m_symbol_paths.push_back(path); }

    // Get the symbol file path
    std::string find_symbol_file(const std::string& symbol_name) const;

private:
    Schematic m_sch;
    std::vector<std::string> m_symbol_paths;

    // Parse helpers
    std::string read_braced_string(std::istream& in);
    void parse_wire(std::istream& in);
    void parse_instance(std::istream& in);
    void parse_text(std::istream& in);
    void parse_symbol_pin(std::istream& in, Symbol& sym);
};

// Net connectivity resolver
class NetResolver {
public:
    explicit NetResolver(Schematic& sch) : m_sch(sch) {}

    // Resolve all net connections
    void resolve();

private:
    Schematic& m_sch;
    std::unordered_map<Point, std::vector<int>, PointHash> m_wire_endpoints;
    std::unordered_map<Point, std::vector<std::pair<int, int>>, PointHash> m_inst_pins;

    // Union-Find for net grouping
    std::vector<int> m_parent;
    int find(int x);
    void unite(int x, int y);

    void collect_connection_points();
    void assign_net_names();
    std::string get_label_at(const Point& p);
};

// SPICE netlist generator
class SpiceNetlister {
public:
    explicit SpiceNetlister(Schematic& sch) : m_sch(sch) {}

    // Generate SPICE netlist
    bool generate(const std::string& output_file);
    bool generate(std::ostream& out);

    // Options
    void set_subcircuit_mode(bool v) { m_subcircuit_mode = v; }
    void set_top_cell_name(const std::string& name) { m_top_cell_name = name; }

private:
    Schematic& m_sch;
    bool m_subcircuit_mode = true;
    std::string m_top_cell_name;

    std::string expand_format(const Instance& inst, const Symbol& sym);
    std::string translate_prop(const Instance& inst, const std::string& prop_name);
    bool is_pin_symbol(const std::string& type) const;
    bool is_label_symbol(const std::string& type) const;
};

// Main API - convenience functions
bool load_schematic(const std::string& filename, Schematic& sch,
                    const std::vector<std::string>& symbol_paths = {});

bool generate_spice_netlist(Schematic& sch, const std::string& output_file,
                            bool subcircuit_mode = true);

bool generate_spice_netlist(Schematic& sch, std::ostream& out,
                            bool subcircuit_mode = true);

// Parse xschemrc file and extract XSCHEM_LIBRARY_PATH entries
// Returns a vector of resolved symbol search paths
std::vector<std::string> parse_xschemrc(const std::string& xschemrc_path);

} // namespace xschem

#endif // XSCHEM_LITE_H
