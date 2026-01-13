// xschem_lite.cpp - Lightweight xschem .sch parser and SPICE netlister
// Implementation file

#include "xschem_lite.h"
#include <iostream>
#include <cctype>
#include <filesystem>
#include <regex>

namespace xschem {

// ============================================================================
// Utility functions
// ============================================================================

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

std::string get_tok_value(const std::string& props, const std::string& key) {
    if (props.empty() || key.empty()) return "";

    // Simple key=value parser
    // Handles: key=value key="value with spaces" key='value'
    size_t pos = 0;
    while (pos < props.size()) {
        // Skip whitespace
        while (pos < props.size() && std::isspace(props[pos])) pos++;
        if (pos >= props.size()) break;

        // Read key
        size_t key_start = pos;
        while (pos < props.size() && props[pos] != '=' && !std::isspace(props[pos])) pos++;
        std::string current_key = props.substr(key_start, pos - key_start);

        // Skip whitespace around =
        while (pos < props.size() && std::isspace(props[pos])) pos++;
        if (pos >= props.size() || props[pos] != '=') {
            // Key without value, skip
            continue;
        }
        pos++; // skip '='
        while (pos < props.size() && std::isspace(props[pos])) pos++;

        // Read value
        std::string value;
        if (pos < props.size() && (props[pos] == '"' || props[pos] == '\'')) {
            char quote = props[pos++];
            size_t val_start = pos;
            while (pos < props.size() && props[pos] != quote) {
                if (props[pos] == '\\' && pos + 1 < props.size()) pos++;
                pos++;
            }
            value = props.substr(val_start, pos - val_start);
            if (pos < props.size()) pos++; // skip closing quote
        } else {
            size_t val_start = pos;
            while (pos < props.size() && !std::isspace(props[pos])) pos++;
            value = props.substr(val_start, pos - val_start);
        }

        if (current_key == key) {
            return value;
        }
    }
    return "";
}

std::unordered_map<std::string, std::string> parse_props(const std::string& props) {
    std::unordered_map<std::string, std::string> result;
    if (props.empty()) return result;

    size_t pos = 0;
    while (pos < props.size()) {
        // Skip whitespace
        while (pos < props.size() && std::isspace(props[pos])) pos++;
        if (pos >= props.size()) break;

        // Read key
        size_t key_start = pos;
        while (pos < props.size() && props[pos] != '=' && !std::isspace(props[pos])) pos++;
        std::string key = props.substr(key_start, pos - key_start);

        // Skip whitespace
        while (pos < props.size() && std::isspace(props[pos])) pos++;
        if (pos >= props.size() || props[pos] != '=') continue;
        pos++; // skip '='
        while (pos < props.size() && std::isspace(props[pos])) pos++;

        // Read value
        std::string value;
        if (pos < props.size() && (props[pos] == '"' || props[pos] == '\'')) {
            char quote = props[pos++];
            size_t val_start = pos;
            while (pos < props.size() && props[pos] != quote) {
                if (props[pos] == '\\' && pos + 1 < props.size()) pos++;
                pos++;
            }
            value = props.substr(val_start, pos - val_start);
            if (pos < props.size()) pos++;
        } else {
            size_t val_start = pos;
            while (pos < props.size() && !std::isspace(props[pos])) pos++;
            value = props.substr(val_start, pos - val_start);
        }

        if (!key.empty()) {
            result[key] = value;
        }
    }
    return result;
}

// ============================================================================
// SchematicParser implementation
// ============================================================================

std::string SchematicParser::read_braced_string(std::istream& in) {
    std::string result;
    int brace_count = 0;
    char c;

    // Skip whitespace to find opening brace
    while (in.get(c) && std::isspace(c));

    if (c != '{') {
        in.putback(c);
        return "";
    }

    brace_count = 1;
    while (in.get(c) && brace_count > 0) {
        if (c == '{') brace_count++;
        else if (c == '}') brace_count--;

        if (brace_count > 0) result += c;
    }

    return result;
}

void SchematicParser::parse_wire(std::istream& in) {
    Wire w;
    in >> w.x1 >> w.y1 >> w.x2 >> w.y2;
    w.props = read_braced_string(in);
    w.is_bus = (get_tok_value(w.props, "bus") == "true");
    m_sch.wires.push_back(w);
}

void SchematicParser::parse_instance(std::istream& in) {
    Instance inst;
    inst.symbol_name = read_braced_string(in);
    in >> inst.x >> inst.y >> inst.rot >> inst.flip;
    inst.props = read_braced_string(in);
    inst.prop_map = parse_props(inst.props);
    inst.inst_name = get_tok_value(inst.props, "name");
    m_sch.instances.push_back(inst);
}

void SchematicParser::parse_text(std::istream& in) {
    Text t;
    t.text = read_braced_string(in);
    in >> t.x >> t.y >> t.rot >> t.flip >> t.xscale >> t.yscale;
    t.props = read_braced_string(in);
    m_sch.texts.push_back(t);
}

std::string SchematicParser::find_symbol_file(const std::string& symbol_name) const {
    namespace fs = std::filesystem;

    // If it's already an absolute path, use it
    if (fs::path(symbol_name).is_absolute() && fs::exists(symbol_name)) {
        return symbol_name;
    }

    // Search in symbol paths
    for (const auto& base_path : m_symbol_paths) {
        fs::path full_path = fs::path(base_path) / symbol_name;
        if (fs::exists(full_path)) {
            return full_path.string();
        }
        // Try with .sym extension
        if (!symbol_name.ends_with(".sym")) {
            full_path = fs::path(base_path) / (symbol_name + ".sym");
            if (fs::exists(full_path)) {
                return full_path.string();
            }
        }
    }

    // Try relative to schematic file
    if (!m_sch.filename.empty()) {
        fs::path sch_dir = fs::path(m_sch.filename).parent_path();
        fs::path full_path = sch_dir / symbol_name;
        if (fs::exists(full_path)) {
            return full_path.string();
        }
    }

    return "";
}

void SchematicParser::parse_symbol_pin(std::istream& in, Symbol& sym) {
    // B 5 x1 y1 x2 y2 {name=pinname dir=in/out/inout}
    int layer;
    double x1, y1, x2, y2;
    in >> layer >> x1 >> y1 >> x2 >> y2;
    std::string props = read_braced_string(in);

    // Layer 5 is PINLAYER in xschem
    if (layer == 5) {
        Pin pin;
        pin.name = get_tok_value(props, "name");
        pin.direction = get_tok_value(props, "dir");
        if (pin.direction.empty()) pin.direction = "inout";
        pin.x = (x1 + x2) / 2.0;
        pin.y = (y1 + y2) / 2.0;

        // Update symbol bounding box
        if (sym.pins.empty()) {
            sym.minx = sym.maxx = pin.x;
            sym.miny = sym.maxy = pin.y;
        } else {
            sym.minx = std::min(sym.minx, pin.x);
            sym.maxx = std::max(sym.maxx, pin.x);
            sym.miny = std::min(sym.miny, pin.y);
            sym.maxy = std::max(sym.maxy, pin.y);
        }

        sym.pins.push_back(pin);
    }
}

bool SchematicParser::load_symbol(const std::string& symbol_name) {
    // Check if already loaded
    if (m_sch.symbols.count(symbol_name)) {
        return true;
    }

    std::string sym_path = find_symbol_file(symbol_name);
    if (sym_path.empty()) {
        // Create a placeholder symbol for built-in types
        Symbol sym;
        sym.name = symbol_name;

        // Detect common built-in types from symbol name
        std::string base_name = std::filesystem::path(symbol_name).stem().string();
        std::string full_path_lower = symbol_name;
        std::transform(full_path_lower.begin(), full_path_lower.end(),
                      full_path_lower.begin(), ::tolower);

        // Check for FET symbols (MOS transistors) - various naming conventions
        bool is_nfet = (base_name.find("nmos") != std::string::npos ||
                       base_name.find("nfet") != std::string::npos ||
                       full_path_lower.find("nfet") != std::string::npos);
        bool is_pfet = (base_name.find("pmos") != std::string::npos ||
                       base_name.find("pfet") != std::string::npos ||
                       full_path_lower.find("pfet") != std::string::npos);

        if (is_nfet || is_pfet) {
            sym.type = is_pfet ? "pmos" : "nmos";
            // PDK style format: @spiceprefix@name d g s b @model L=@L W=@W ...
            sym.format = "@spiceprefix@name @pinlist @model L=@L W=@W nf=1 ad='int((nf+1)/2) * W/nf * 0.29' as='int((nf+2)/2) * W/nf * 0.29'\n+ pd='2*int((nf+1)/2) * (W/nf + 0.29)' ps='2*int((nf+2)/2) * (W/nf + 0.29)' nrd='0.29 / W' nrs='0.29 / W' sa=0 sb=0 sd=0 mult=1 m=1";
            // Standard 4-pin MOS: D, G, S, B
            sym.pins = {{"D", "inout", 0, -30}, {"G", "in", -20, 0},
                       {"S", "inout", 0, 30}, {"B", "in", 20, 0}};
        } else if (base_name.find("res") != std::string::npos) {
            sym.type = "resistor";
            sym.format = "@name @pinlist @value m=@m";
            sym.pins = {{"P", "inout", 0, -30}, {"M", "inout", 0, 30}};
        } else if (base_name.find("cap") != std::string::npos) {
            sym.type = "capacitor";
            sym.format = "@name @pinlist @value m=@m";
            sym.pins = {{"P", "inout", 0, -30}, {"M", "inout", 0, 30}};
        } else if (base_name.find("ipin") != std::string::npos) {
            sym.type = "ipin";
            sym.pins = {{"p", "in", 20, 0}};
        } else if (base_name.find("opin") != std::string::npos) {
            sym.type = "opin";
            sym.pins = {{"p", "out", -20, 0}};
        } else if (base_name.find("iopin") != std::string::npos) {
            sym.type = "iopin";
            sym.pins = {{"p", "inout", 0, 0}};
        } else if (base_name.find("lab_pin") != std::string::npos ||
                   base_name.find("lab_wire") != std::string::npos) {
            sym.type = "label";
            sym.pins = {{"p", "inout", 0, 0}};
        } else if (base_name.find("vdd") != std::string::npos ||
                   base_name.find("gnd") != std::string::npos ||
                   base_name.find("vss") != std::string::npos) {
            sym.type = "label";
            sym.pins = {{"p", "inout", 0, 0}};
        } else {
            // Default to subcircuit
            sym.type = "subcircuit";
            sym.format = "@spiceprefix@name @pinlist @symname";
        }

        m_sch.symbols[symbol_name] = sym;
        return true;
    }

    std::ifstream file(sym_path);
    if (!file.is_open()) {
        return false;
    }

    Symbol sym;
    sym.name = symbol_name;

    char tag;
    while (file >> tag) {
        switch (tag) {
            case 'v': {
                // Version line
                std::string version = read_braced_string(file);
                break;
            }
            case 'K': {
                // Symbol properties (type, format, template)
                std::string k_props = read_braced_string(file);
                sym.type = get_tok_value(k_props, "type");
                sym.format = get_tok_value(k_props, "format");
                sym.template_str = get_tok_value(k_props, "template");
                sym.props = k_props;
                break;
            }
            case 'G':
            case 'V':
            case 'S':
            case 'E':
                read_braced_string(file);
                break;
            case 'B': {
                // Box - could be a pin (layer 5)
                parse_symbol_pin(file, sym);
                break;
            }
            case 'L':
            case 'A':
            case 'P': {
                // Skip graphical elements
                std::string line;
                std::getline(file, line);
                // Read trailing braced string if any
                while (file.peek() == ' ' || file.peek() == '\t') file.get();
                if (file.peek() == '{') {
                    read_braced_string(file);
                }
                break;
            }
            case 'T': {
                // Text - might contain pin info
                std::string text = read_braced_string(file);
                double x, y;
                int rot, flip;
                double xscale, yscale;
                file >> x >> y >> rot >> flip >> xscale >> yscale;
                std::string props = read_braced_string(file);

                // Check if this is a pin text (contains @#n:net_name pattern)
                if (text.find("@#") != std::string::npos) {
                    // Extract pin index from @#n pattern
                    size_t pos = text.find("@#");
                    if (pos != std::string::npos && pos + 2 < text.size()) {
                        int pin_idx = text[pos + 2] - '0';
                        if (pin_idx >= 0 && pin_idx <= 9) {
                            // This gives us pin order info
                        }
                    }
                }
                break;
            }
            case '#':
                // Comment
                {
                    std::string line;
                    std::getline(file, line);
                }
                break;
            default:
                // Skip unknown
                {
                    std::string line;
                    std::getline(file, line);
                }
                break;
        }
    }

    m_sch.symbols[symbol_name] = sym;
    return true;
}

bool SchematicParser::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return false;
    }

    m_sch.filename = filename;
    m_sch.wires.clear();
    m_sch.instances.clear();
    m_sch.texts.clear();

    char tag;
    while (file >> tag) {
        switch (tag) {
            case 'v': {
                m_sch.version = read_braced_string(file);
                break;
            }
            case 'K':
                m_sch.K_props = read_braced_string(file);
                break;
            case 'G':
                m_sch.G_props = read_braced_string(file);
                break;
            case 'V':
                m_sch.V_props = read_braced_string(file);
                break;
            case 'S':
                m_sch.S_props = read_braced_string(file);
                break;
            case 'E':
                m_sch.E_props = read_braced_string(file);
                break;
            case 'N':
                parse_wire(file);
                break;
            case 'C':
                parse_instance(file);
                break;
            case 'T':
                parse_text(file);
                break;
            case 'L':
            case 'B':
            case 'A':
            case 'P': {
                // Skip graphical elements
                std::string line;
                std::getline(file, line);
                // Read trailing props if any
                while (file.peek() == ' ' || file.peek() == '\t') file.get();
                if (file.peek() == '{') {
                    read_braced_string(file);
                }
                break;
            }
            case '#': {
                // Comment line
                std::string line;
                std::getline(file, line);
                break;
            }
            case '[': {
                // Embedded symbol - skip for now
                int bracket_count = 1;
                char c;
                while (file.get(c) && bracket_count > 0) {
                    if (c == '[') bracket_count++;
                    else if (c == ']') bracket_count--;
                }
                break;
            }
            default:
                // Unknown tag, skip rest of line
                {
                    std::string line;
                    std::getline(file, line);
                }
                break;
        }
    }

    // Load symbols for all instances
    for (const auto& inst : m_sch.instances) {
        load_symbol(inst.symbol_name);
    }

    return true;
}

// ============================================================================
// NetResolver implementation
// ============================================================================

int NetResolver::find(int x) {
    if (m_parent[x] != x) {
        m_parent[x] = find(m_parent[x]);
    }
    return m_parent[x];
}

void NetResolver::unite(int x, int y) {
    int px = find(x);
    int py = find(y);
    if (px != py) {
        m_parent[px] = py;
    }
}

// xschem-compatible rotation transformation
// rot: 0=0°, 1=90°, 2=180°, 3=270°
// flip: 0=no flip, 1=horizontal flip
// x0, y0: rotation anchor point (instance position)
// x, y: point to transform (relative to anchor)
// rx, ry: resulting transformed point
static void apply_rotation(int rot, int flip, double x0, double y0,
                          double x, double y, double& rx, double& ry) {
    double xxtmp = flip ? 2 * x0 - x : x;
    if (rot == 0)      { rx = xxtmp;  ry = y; }
    else if (rot == 1) { rx = x0 - y + y0; ry = y0 + xxtmp - x0; }
    else if (rot == 2) { rx = 2 * x0 - xxtmp; ry = 2 * y0 - y; }
    else               { rx = x0 + y - y0; ry = y0 - xxtmp + x0; }
}

void NetResolver::collect_connection_points() {
    // Collect wire endpoints
    for (size_t i = 0; i < m_sch.wires.size(); i++) {
        const auto& w = m_sch.wires[i];
        Point p1{w.x1, w.y1};
        Point p2{w.x2, w.y2};
        m_wire_endpoints[p1].push_back(static_cast<int>(i));
        m_wire_endpoints[p2].push_back(static_cast<int>(i));
    }

    // Collect instance pin locations
    for (size_t i = 0; i < m_sch.instances.size(); i++) {
        const auto& inst = m_sch.instances[i];
        auto sym_it = m_sch.symbols.find(inst.symbol_name);
        if (sym_it == m_sch.symbols.end()) continue;

        const auto& sym = sym_it->second;
        for (size_t p = 0; p < sym.pins.size(); p++) {
            const auto& pin = sym.pins[p];

            // Transform pin coordinates using xschem-compatible rotation
            // Pin coordinates are relative to symbol origin (0,0)
            // Instance position is the anchor point
            double rx, ry;
            apply_rotation(inst.rot, inst.flip, inst.x, inst.y,
                          inst.x + pin.x, inst.y + pin.y, rx, ry);

            Point pt{rx, ry};
            m_inst_pins[pt].push_back({static_cast<int>(i), static_cast<int>(p)});
        }
    }
}

std::string NetResolver::get_label_at(const Point& p) {
    // Check for label instances at this point
    for (size_t i = 0; i < m_sch.instances.size(); i++) {
        const auto& inst = m_sch.instances[i];
        auto sym_it = m_sch.symbols.find(inst.symbol_name);
        if (sym_it == m_sch.symbols.end()) continue;

        const auto& sym = sym_it->second;

        // Check if this is a label-type symbol
        bool is_label = (sym.type == "label" ||
                        inst.symbol_name.find("lab_pin") != std::string::npos ||
                        inst.symbol_name.find("lab_wire") != std::string::npos ||
                        inst.symbol_name.find("vdd") != std::string::npos ||
                        inst.symbol_name.find("gnd") != std::string::npos ||
                        inst.symbol_name.find("vss") != std::string::npos);

        if (!is_label) continue;

        // Check if instance pin is at this point
        for (const auto& pin : sym.pins) {
            // Transform pin position using xschem-compatible rotation
            double px, py;
            apply_rotation(inst.rot, inst.flip, inst.x, inst.y,
                          inst.x + pin.x, inst.y + pin.y, px, py);

            if (std::abs(px - p.x) < 0.01 && std::abs(py - p.y) < 0.01) {
                std::string label = get_tok_value(inst.props, "lab");
                if (!label.empty()) return label;
            }
        }
    }

    // Check for wire labels
    for (const auto& w : m_sch.wires) {
        if ((std::abs(w.x1 - p.x) < 0.01 && std::abs(w.y1 - p.y) < 0.01) ||
            (std::abs(w.x2 - p.x) < 0.01 && std::abs(w.y2 - p.y) < 0.01)) {
            std::string label = get_tok_value(w.props, "lab");
            if (!label.empty()) return label;
        }
    }

    return "";
}

void NetResolver::assign_net_names() {
    // Initialize union-find
    size_t total_wires = m_sch.wires.size();
    m_parent.resize(total_wires);
    for (size_t i = 0; i < total_wires; i++) {
        m_parent[i] = static_cast<int>(i);
    }

    // Unite wires that share endpoints
    for (const auto& [point, wire_indices] : m_wire_endpoints) {
        if (wire_indices.size() > 1) {
            for (size_t i = 1; i < wire_indices.size(); i++) {
                unite(wire_indices[0], wire_indices[i]);
            }
        }
    }

    // Collect all connection points (wires + instance pins)
    // and unite wires that connect through instance pins or labels
    for (const auto& [point, inst_pin_list] : m_inst_pins) {
        auto wire_it = m_wire_endpoints.find(point);
        if (wire_it != m_wire_endpoints.end() && !wire_it->second.empty()) {
            // Unite all wires at this point
            for (size_t i = 1; i < wire_it->second.size(); i++) {
                unite(wire_it->second[0], wire_it->second[i]);
            }
        }
    }

    // Assign names to wire groups
    std::unordered_map<int, std::string> group_names;

    // First pass: collect explicit labels
    for (size_t i = 0; i < m_sch.wires.size(); i++) {
        const auto& w = m_sch.wires[i];
        int group = find(static_cast<int>(i));

        // Check wire's own label
        std::string label = get_tok_value(w.props, "lab");
        if (!label.empty()) {
            group_names[group] = label;
        }

        // Check labels at endpoints
        if (group_names.find(group) == group_names.end()) {
            Point p1{w.x1, w.y1};
            Point p2{w.x2, w.y2};
            label = get_label_at(p1);
            if (!label.empty()) {
                group_names[group] = label;
            } else {
                label = get_label_at(p2);
                if (!label.empty()) {
                    group_names[group] = label;
                }
            }
        }
    }

    // Second pass: assign names to all wires in each group
    for (size_t i = 0; i < m_sch.wires.size(); i++) {
        int group = find(static_cast<int>(i));
        auto name_it = group_names.find(group);
        if (name_it != group_names.end()) {
            m_sch.wires[i].node = name_it->second;
        } else {
            // Generate unnamed net
            std::string net_name = "net" + std::to_string(m_sch.unnamed_net_count++);
            group_names[group] = net_name;
            m_sch.wires[i].node = net_name;
        }
    }

    // Create map for pin-to-pin connections (pins at same location share a net)
    // Key: point location, Value: assigned net name
    std::unordered_map<Point, std::string, PointHash> point_net_names;

    // First, assign net names to points that have wires
    for (const auto& [point, wire_indices] : m_wire_endpoints) {
        if (!wire_indices.empty()) {
            point_net_names[point] = m_sch.wires[wire_indices[0]].node;
        }
    }

    // Then, assign net names to points that only have pin-to-pin connections
    // (multiple pins at same location without any wire)
    for (const auto& [point, inst_pin_list] : m_inst_pins) {
        if (point_net_names.find(point) == point_net_names.end()) {
            // Check for label at this point first
            std::string label = get_label_at(point);
            if (!label.empty()) {
                point_net_names[point] = label;
            } else if (inst_pin_list.size() > 1) {
                // Multiple pins at same point without wire - create a shared net
                std::string net_name = "net" + std::to_string(m_sch.unnamed_net_count++);
                point_net_names[point] = net_name;
            }
            // If only one pin at this point and no wire/label, leave unassigned
            // (will be marked NC later)
        }
    }

    // Assign nets to instance pins
    for (auto& inst : m_sch.instances) {
        auto sym_it = m_sch.symbols.find(inst.symbol_name);
        if (sym_it == m_sch.symbols.end()) continue;

        const auto& sym = sym_it->second;
        inst.connected_nets.resize(sym.pins.size());

        for (size_t p = 0; p < sym.pins.size(); p++) {
            const auto& pin = sym.pins[p];

            // Transform pin position using xschem-compatible rotation
            double px, py;
            apply_rotation(inst.rot, inst.flip, inst.x, inst.y,
                          inst.x + pin.x, inst.y + pin.y, px, py);

            Point pt{px, py};

            // Check if this point has an assigned net name
            auto net_it = point_net_names.find(pt);
            if (net_it != point_net_names.end()) {
                inst.connected_nets[p] = net_it->second;
            } else {
                // Check if there's a label at this point
                std::string label = get_label_at(pt);
                if (!label.empty()) {
                    inst.connected_nets[p] = label;
                } else {
                    // Unconnected pin - create unique net
                    inst.connected_nets[p] = "NC_" + inst.inst_name + "_" + pin.name;
                }
            }
        }
    }
}

void NetResolver::resolve() {
    collect_connection_points();
    assign_net_names();
}

// ============================================================================
// SpiceNetlister implementation
// ============================================================================

bool SpiceNetlister::is_pin_symbol(const std::string& type) const {
    return type == "ipin" || type == "opin" || type == "iopin";
}

bool SpiceNetlister::is_label_symbol(const std::string& type) const {
    return type == "label" || type == "netlabel" || type == "net_name";
}

std::string SpiceNetlister::translate_prop(const Instance& inst, const std::string& prop_name) {
    // Handle @prop syntax
    if (prop_name.empty()) return "";

    auto it = inst.prop_map.find(prop_name);
    if (it != inst.prop_map.end()) {
        return it->second;
    }

    // Check symbol template for default
    auto sym_it = m_sch.symbols.find(inst.symbol_name);
    if (sym_it != m_sch.symbols.end()) {
        std::string val = get_tok_value(sym_it->second.template_str, prop_name);
        if (!val.empty()) return val;
    }

    return "";
}

std::string SpiceNetlister::expand_format(const Instance& inst, const Symbol& sym) {
    std::string format = sym.format;
    if (format.empty()) {
        // Default format for different types
        if (sym.type == "subcircuit") {
            format = "@name @pinlist @symname";
        } else if (sym.type == "nmos" || sym.type == "pmos") {
            format = "@spiceprefix@name @pinlist @model w=@w l=@l m=@m";
        } else if (sym.type == "resistor") {
            format = "@name @pinlist @value m=@m";
        } else if (sym.type == "capacitor") {
            format = "@name @pinlist @value m=@m";
        } else {
            format = "@name @pinlist @value";
        }
    }

    std::string result;
    size_t pos = 0;

    while (pos < format.size()) {
        if (format[pos] == '@') {
            // Property substitution
            pos++;
            size_t start = pos;
            while (pos < format.size() &&
                   (std::isalnum(format[pos]) || format[pos] == '_' || format[pos] == '#' || format[pos] == ':')) {
                pos++;
            }
            std::string prop_name = format.substr(start, pos - start);

            if (prop_name == "name") {
                result += inst.inst_name;
            } else if (prop_name == "pinlist") {
                // Output connected nets in pin order
                for (size_t i = 0; i < inst.connected_nets.size(); i++) {
                    if (i > 0) result += " ";
                    result += inst.connected_nets[i];
                }
            } else if (prop_name == "symname") {
                // Extract symbol name without path and extension
                std::string sym_name = std::filesystem::path(sym.name).stem().string();
                result += sym_name;
            } else if (prop_name == "spiceprefix") {
                // Spice prefix - usually empty for most elements
                std::string val = translate_prop(inst, "spiceprefix");
                result += val;  // May be empty, that's OK
            } else if (prop_name == "extra") {
                // Extra parameters - skip if empty
                std::string val = translate_prop(inst, "extra");
                if (!val.empty()) {
                    result += val;
                }
            } else {
                std::string val = translate_prop(inst, prop_name);
                if (!val.empty()) {
                    result += val;
                }
            }
        } else {
            result += format[pos++];
        }
    }

    // Clean up multiple spaces
    std::string cleaned;
    bool last_was_space = false;
    for (char c : result) {
        if (c == ' ') {
            if (!last_was_space) {
                cleaned += c;
                last_was_space = true;
            }
        } else {
            cleaned += c;
            last_was_space = false;
        }
    }

    return cleaned;
}

bool SpiceNetlister::generate(std::ostream& out) {
    // Resolve nets if not already done
    NetResolver resolver(m_sch);
    resolver.resolve();

    // Get cell name
    std::string cell_name = m_top_cell_name;
    if (cell_name.empty()) {
        cell_name = std::filesystem::path(m_sch.filename).stem().string();
    }

    // Header
    out << "** sch_path: " << m_sch.filename << "\n";

    // Collect pins for subcircuit header
    std::vector<std::string> io_pins;
    for (const auto& inst : m_sch.instances) {
        auto sym_it = m_sch.symbols.find(inst.symbol_name);
        if (sym_it == m_sch.symbols.end()) continue;

        if (is_pin_symbol(sym_it->second.type)) {
            std::string lab = get_tok_value(inst.props, "lab");
            if (!lab.empty()) {
                io_pins.push_back(lab);
            }
        }
    }

    // Subcircuit header
    if (m_subcircuit_mode) {
        out << ".subckt " << cell_name;
        for (const auto& pin : io_pins) {
            out << " " << pin;
        }
        out << "\n";

        // Pin info comment
        if (!io_pins.empty()) {
            out << "*.PININFO";
            for (const auto& inst : m_sch.instances) {
                auto sym_it = m_sch.symbols.find(inst.symbol_name);
                if (sym_it == m_sch.symbols.end()) continue;

                const auto& sym = sym_it->second;
                if (is_pin_symbol(sym.type)) {
                    std::string lab = get_tok_value(inst.props, "lab");
                    char dir = 'B';
                    if (sym.type == "ipin") dir = 'I';
                    else if (sym.type == "opin") dir = 'O';
                    out << " " << lab << ":" << dir;
                }
            }
            out << "\n";
        }
    } else {
        out << "** " << cell_name << "\n";
    }

    // Output instances
    for (const auto& inst : m_sch.instances) {
        auto sym_it = m_sch.symbols.find(inst.symbol_name);
        if (sym_it == m_sch.symbols.end()) continue;

        const auto& sym = sym_it->second;

        // Skip pin and label symbols
        if (is_pin_symbol(sym.type) || is_label_symbol(sym.type)) {
            continue;
        }

        // Skip graphical/annotation symbols
        if (sym.type == "title" || sym.type == "logo" || sym.type == "graphic" ||
            inst.symbol_name.find("title") != std::string::npos ||
            inst.symbol_name.find("ammeter") != std::string::npos) {
            continue;
        }

        // Generate SPICE line
        std::string spice_line = expand_format(inst, sym);
        spice_line = trim(spice_line);
        if (!spice_line.empty()) {
            out << spice_line << "\n";
        }
    }

    // Close subcircuit
    if (m_subcircuit_mode) {
        out << ".ends\n";
    }

    out << ".end\n";

    return true;
}

bool SpiceNetlister::generate(const std::string& output_file) {
    std::ofstream out(output_file);
    if (!out.is_open()) {
        std::cerr << "Error: Cannot open output file: " << output_file << std::endl;
        return false;
    }
    return generate(out);
}

// ============================================================================
// Convenience API
// ============================================================================

bool load_schematic(const std::string& filename, Schematic& sch,
                    const std::vector<std::string>& symbol_paths) {
    SchematicParser parser;
    for (const auto& path : symbol_paths) {
        parser.add_symbol_path(path);
    }
    if (!parser.load(filename)) {
        return false;
    }
    sch = std::move(parser.schematic());
    return true;
}

bool generate_spice_netlist(Schematic& sch, const std::string& output_file,
                            bool subcircuit_mode) {
    SpiceNetlister netlister(sch);
    netlister.set_subcircuit_mode(subcircuit_mode);
    return netlister.generate(output_file);
}

bool generate_spice_netlist(Schematic& sch, std::ostream& out, bool subcircuit_mode) {
    SpiceNetlister netlister(sch);
    netlister.set_subcircuit_mode(subcircuit_mode);
    return netlister.generate(out);
}

// ============================================================================
// xschemrc parser
// ============================================================================

// Helper to expand environment variables in a string
static std::string expand_env_vars(const std::string& input) {
    std::string result = input;

    // Expand $env(VAR) syntax (Tcl style)
    std::regex env_tcl_regex(R"(\$env\(([^)]+)\))");
    std::smatch match;
    while (std::regex_search(result, match, env_tcl_regex)) {
        std::string var_name = match[1].str();
        const char* env_val = std::getenv(var_name.c_str());
        std::string replacement = env_val ? env_val : "";
        result = match.prefix().str() + replacement + match.suffix().str();
    }

    // Expand ${VAR} syntax
    std::regex env_brace_regex(R"(\$\{([^}]+)\})");
    while (std::regex_search(result, match, env_brace_regex)) {
        std::string var_name = match[1].str();
        const char* env_val = std::getenv(var_name.c_str());
        std::string replacement = env_val ? env_val : "";
        result = match.prefix().str() + replacement + match.suffix().str();
    }

    // Expand $VAR syntax (simple)
    std::regex env_simple_regex(R"(\$([A-Za-z_][A-Za-z0-9_]*))");
    while (std::regex_search(result, match, env_simple_regex)) {
        std::string var_name = match[1].str();
        const char* env_val = std::getenv(var_name.c_str());
        std::string replacement = env_val ? env_val : "";
        result = match.prefix().str() + replacement + match.suffix().str();
    }

    return result;
}

std::vector<std::string> parse_xschemrc(const std::string& xschemrc_path) {
    std::vector<std::string> paths;

    std::ifstream file(xschemrc_path);
    if (!file.is_open()) {
        std::cerr << "Warning: Cannot open xschemrc: " << xschemrc_path << std::endl;
        return paths;
    }

    // Get directory containing xschemrc for relative path resolution
    std::filesystem::path rc_dir = std::filesystem::path(xschemrc_path).parent_path();

    // Variables we track (simplified Tcl variable handling)
    std::unordered_map<std::string, std::string> tcl_vars;

    // Initialize with common defaults
    const char* home = std::getenv("HOME");
    if (home) tcl_vars["env(HOME)"] = home;

    const char* pdk_root = std::getenv("PDK_ROOT");
    if (pdk_root) {
        tcl_vars["PDK_ROOT"] = pdk_root;
        tcl_vars["env(PDK_ROOT)"] = pdk_root;
    }

    const char* pdk = std::getenv("PDK");
    tcl_vars["PDK"] = pdk ? pdk : "sky130A";

    // XSCHEM_SHAREDIR - try to find it
    const char* xschem_share = std::getenv("XSCHEM_SHAREDIR");
    if (xschem_share) {
        tcl_vars["XSCHEM_SHAREDIR"] = xschem_share;
    } else {
        // Common locations
        std::vector<std::string> share_candidates = {
            "/usr/share/xschem",
            "/usr/local/share/xschem",
            home ? std::string(home) + "/share/xschem" : ""
        };
        for (const auto& candidate : share_candidates) {
            if (!candidate.empty() && std::filesystem::exists(candidate)) {
                tcl_vars["XSCHEM_SHAREDIR"] = candidate;
                break;
            }
        }
    }

    std::string line;
    std::string xschem_library_path;

    while (std::getline(file, line)) {
        // Skip comments and empty lines
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        // Look for: set XSCHEM_LIBRARY_PATH ...
        // and: append XSCHEM_LIBRARY_PATH :...

        std::regex set_regex(R"(^\s*set\s+XSCHEM_LIBRARY_PATH\s+\{\s*\}\s*$)");
        std::regex append_regex(R"(^\s*append\s+XSCHEM_LIBRARY_PATH\s+:(.+)$)");
        std::regex set_var_regex(R"(^\s*set\s+(\w+)\s+(.+)$)");

        std::smatch match;

        if (std::regex_match(trimmed, set_regex)) {
            // Reset path
            xschem_library_path.clear();
        } else if (std::regex_search(trimmed, match, append_regex)) {
            // Append to path
            std::string path_part = match[1].str();

            // Handle [file dirname [info script]] - means directory of xschemrc
            if (path_part.find("[file dirname [info script]]") != std::string::npos) {
                path_part = rc_dir.string();
            }

            // Substitute Tcl variables
            for (const auto& [var, val] : tcl_vars) {
                std::string var_pattern = "${" + var + "}";
                size_t pos;
                while ((pos = path_part.find(var_pattern)) != std::string::npos) {
                    path_part.replace(pos, var_pattern.length(), val);
                }
                // Also try $VAR format
                var_pattern = "$" + var;
                while ((pos = path_part.find(var_pattern)) != std::string::npos) {
                    // Make sure it's not part of a longer variable name
                    size_t end_pos = pos + var_pattern.length();
                    if (end_pos >= path_part.length() ||
                        (!std::isalnum(path_part[end_pos]) && path_part[end_pos] != '_')) {
                        path_part.replace(pos, var_pattern.length(), val);
                    } else {
                        break;
                    }
                }
            }

            // Expand any remaining environment variables
            path_part = expand_env_vars(path_part);

            if (!xschem_library_path.empty()) {
                xschem_library_path += ":";
            }
            xschem_library_path += path_part;
        } else if (std::regex_search(trimmed, match, set_var_regex)) {
            // Track variable assignments
            std::string var_name = match[1].str();
            std::string var_value = match[2].str();

            // Remove braces if present
            if (!var_value.empty() && var_value.front() == '{' && var_value.back() == '}') {
                var_value = var_value.substr(1, var_value.length() - 2);
            }

            // Substitute existing variables
            for (const auto& [var, val] : tcl_vars) {
                std::string var_pattern = "${" + var + "}";
                size_t pos;
                while ((pos = var_value.find(var_pattern)) != std::string::npos) {
                    var_value.replace(pos, var_pattern.length(), val);
                }
                var_pattern = "$" + var;
                while ((pos = var_value.find(var_pattern)) != std::string::npos) {
                    size_t end_pos = pos + var_pattern.length();
                    if (end_pos >= var_value.length() ||
                        (!std::isalnum(var_value[end_pos]) && var_value[end_pos] != '_')) {
                        var_value.replace(pos, var_pattern.length(), val);
                    } else {
                        break;
                    }
                }
            }

            var_value = expand_env_vars(var_value);
            tcl_vars[var_name] = var_value;
        }
    }

    // Split the path by colons and add valid directories
    std::stringstream ss(xschem_library_path);
    std::string path_entry;
    while (std::getline(ss, path_entry, ':')) {
        path_entry = trim(path_entry);
        if (!path_entry.empty()) {
            // Resolve to absolute path
            std::filesystem::path abs_path;
            if (std::filesystem::path(path_entry).is_relative()) {
                abs_path = rc_dir / path_entry;
            } else {
                abs_path = path_entry;
            }

            // Check if directory exists
            if (std::filesystem::exists(abs_path) && std::filesystem::is_directory(abs_path)) {
                paths.push_back(std::filesystem::canonical(abs_path).string());
            } else if (std::filesystem::exists(abs_path)) {
                paths.push_back(abs_path.string());
            }
            // Silently skip non-existent paths
        }
    }

    return paths;
}

} // namespace xschem
