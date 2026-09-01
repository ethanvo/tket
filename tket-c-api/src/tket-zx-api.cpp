extern "C" {
#include "tket-c-api.h"
}

#include <boost/graph/iteration_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "tket/Converters/Converters.hpp"
#include "tket/ZX/Rewrite.hpp"
#include "tket/ZX/ZXGenerator.hpp"

using json = nlohmann::json;
using tket::Circuit;
using tket::Expr;
using tket::zx::QuantumType;
using tket::zx::Rewrite;
using tket::zx::Wire;
using tket::zx::WireProperties;
using tket::zx::ZXDiagram;
using tket::zx::ZXGen;
using tket::zx::ZXGen_ptr;
using tket::zx::ZXGraph;
using tket::zx::ZXType;
using tket::zx::ZXVert;
using tket::zx::ZXWireType;

struct TketZXDiagram {
  ZXDiagram diagram;
};

namespace {

thread_local std::string last_zx_error;

void clear_error() { last_zx_error.clear(); }

void set_error(const std::string& message) { last_zx_error = message; }

char* copy_string(const std::string& value) {
  char* result = static_cast<char*>(std::malloc(value.size() + 1));
  if (result == nullptr) {
    throw std::bad_alloc();
  }
  std::memcpy(result, value.c_str(), value.size() + 1);
  return result;
}

TketError write_string(const std::string& value, char** output) {
  if (output == nullptr) {
    set_error("output string must not be NULL");
    return TKET_ERROR_NULL_POINTER;
  }
  *output = nullptr;
  try {
    *output = copy_string(value);
  } catch (const std::exception& error) {
    set_error(std::string("failed to allocate output string: ") + error.what());
    return TKET_ERROR_TRANSFORM_FAILED;
  }
  return TKET_SUCCESS;
}

const char* zx_type_name(ZXType type) {
  switch (type) {
    case ZXType::Input:
      return "Input";
    case ZXType::Output:
      return "Output";
    case ZXType::Open:
      return "Open";
    case ZXType::ZSpider:
      return "ZSpider";
    case ZXType::XSpider:
      return "XSpider";
    case ZXType::Hbox:
      return "Hbox";
    case ZXType::XY:
      return "XY";
    case ZXType::XZ:
      return "XZ";
    case ZXType::YZ:
      return "YZ";
    case ZXType::PX:
      return "PX";
    case ZXType::PY:
      return "PY";
    case ZXType::PZ:
      return "PZ";
    case ZXType::Triangle:
      return "Triangle";
    case ZXType::ZXBox:
      return "ZXBox";
  }
  throw std::invalid_argument("unknown ZX vertex type");
}

ZXType zx_type_from_name(const std::string& name) {
  static const std::map<std::string, ZXType> names = {
      {"Input", ZXType::Input},       {"Output", ZXType::Output},
      {"Open", ZXType::Open},         {"ZSpider", ZXType::ZSpider},
      {"XSpider", ZXType::XSpider},   {"Hbox", ZXType::Hbox},
      {"XY", ZXType::XY},             {"XZ", ZXType::XZ},
      {"YZ", ZXType::YZ},             {"PX", ZXType::PX},
      {"PY", ZXType::PY},             {"PZ", ZXType::PZ},
      {"Triangle", ZXType::Triangle}, {"ZXBox", ZXType::ZXBox},
  };
  const auto it = names.find(name);
  if (it == names.end()) {
    throw std::invalid_argument("unknown ZX vertex type: " + name);
  }
  return it->second;
}

const char* qtype_name(QuantumType type) {
  switch (type) {
    case QuantumType::Quantum:
      return "Quantum";
    case QuantumType::Classical:
      return "Classical";
  }
  throw std::invalid_argument("unknown ZX quantum type");
}

QuantumType qtype_from_name(const std::string& name) {
  if (name == "Quantum") return QuantumType::Quantum;
  if (name == "Classical") return QuantumType::Classical;
  throw std::invalid_argument("unknown ZX quantum type: " + name);
}

const char* wire_type_name(ZXWireType type) {
  switch (type) {
    case ZXWireType::Basic:
      return "Basic";
    case ZXWireType::H:
      return "H";
  }
  throw std::invalid_argument("unknown ZX wire type");
}

ZXWireType wire_type_from_name(const std::string& name) {
  if (name == "Basic") return ZXWireType::Basic;
  if (name == "H") return ZXWireType::H;
  throw std::invalid_argument("unknown ZX wire type: " + name);
}

json serialize_diagram(const ZXDiagram& diagram);

json serialize_generator(const ZXGen_ptr& generator) {
  const ZXType type = generator->get_type();
  json result;
  result["type"] = zx_type_name(type);
  const auto qtype = generator->get_qtype();
  if (qtype) {
    result["qtype"] = qtype_name(*qtype);
  } else {
    result["qtype"] = nullptr;
  }

  if (tket::zx::is_phase_type(type)) {
    const auto* phased = dynamic_cast<const tket::zx::PhasedGen*>(
        generator.get());
    if (phased == nullptr) {
      throw std::logic_error("phase ZX generator has no phase parameter");
    }
    result["param"] = phased->get_param();
  } else if (tket::zx::is_Clifford_gen_type(type)) {
    const auto* clifford = dynamic_cast<const tket::zx::CliffordGen*>(
        generator.get());
    if (clifford == nullptr) {
      throw std::logic_error("Clifford ZX generator has no Boolean parameter");
    }
    result["param"] = clifford->get_param();
  } else if (type == ZXType::ZXBox) {
    const auto* box = dynamic_cast<const tket::zx::ZXBox*>(generator.get());
    if (box == nullptr) {
      throw std::logic_error("ZXBox generator has no nested diagram");
    }
    result["diagram"] = serialize_diagram(*box->get_diagram());
  }
  return result;
}

json serialize_diagram(const ZXDiagram& diagram) {
  json result;
  result["schema"] = "tket-zx-v1";
  result["scalar"] = diagram.get_scalar();
  result["vertices"] = json::array();
  result["boundary"] = json::array();
  result["wires"] = json::array();

  // ZXDiagram currently exposes only a mutable graph accessor. Serialization
  // is read-only; keep that constness at this API boundary until TKET adds a
  // const overload.
  ZXGraph& graph = *const_cast<ZXDiagram&>(diagram).get_graph();
  std::map<ZXVert, unsigned> ids;
  unsigned next_id = 0;
  BGL_FORALL_VERTICES(vertex, graph, ZXGraph) {
    ids.emplace(vertex, next_id++);
  }

  for (const ZXVert& boundary : diagram.get_boundary()) {
    result["boundary"].push_back(ids.at(boundary));
  }

  BGL_FORALL_VERTICES(vertex, graph, ZXGraph) {
    json vertex_json = serialize_generator(diagram.get_vertex_ZXGen_ptr(vertex));
    vertex_json["id"] = ids.at(vertex);
    result["vertices"].push_back(std::move(vertex_json));
  }

  BGL_FORALL_EDGES(wire, graph, ZXGraph) {
    const WireProperties properties = diagram.get_wire_info(wire);
    json wire_json = {
        {"source", ids.at(diagram.source(wire))},
        {"target", ids.at(diagram.target(wire))},
        {"type", wire_type_name(properties.type)},
        {"qtype", qtype_name(properties.qtype)},
        {"source_port", properties.source_port
                             ? json(*properties.source_port)
                             : json(nullptr)},
        {"target_port", properties.target_port
                             ? json(*properties.target_port)
                             : json(nullptr)},
    };
    result["wires"].push_back(std::move(wire_json));
  }
  return result;
}

std::optional<unsigned> optional_uint(const json& value) {
  if (value.is_null()) return std::nullopt;
  return value.get<unsigned>();
}

ZXGen_ptr generator_from_json(const json& vertex) {
  const ZXType type = zx_type_from_name(vertex.at("type").get<std::string>());
  QuantumType qtype = QuantumType::Quantum;
  if (!vertex.at("qtype").is_null()) {
    qtype = qtype_from_name(vertex.at("qtype").get<std::string>());
  }

  if (type == ZXType::ZXBox) {
    throw std::logic_error("ZXBox generators require a nested diagram");
  }

  if (tket::zx::is_phase_type(type)) {
    return ZXGen::create_gen(type, vertex.at("param").get<Expr>(), qtype);
  }
  if (tket::zx::is_Clifford_gen_type(type)) {
    return ZXGen::create_gen(type, vertex.at("param").get<bool>(), qtype);
  }
  return ZXGen::create_gen(type, qtype);
}

ZXDiagram parse_diagram(const json& input);

ZXGen_ptr generator_from_json_with_boxes(const json& vertex) {
  const ZXType type = zx_type_from_name(vertex.at("type").get<std::string>());
  if (type != ZXType::ZXBox) return generator_from_json(vertex);

  if (!vertex.contains("diagram") || !vertex.at("diagram").is_object()) {
    throw std::invalid_argument("ZXBox vertex is missing its diagram");
  }
  const ZXDiagram nested = parse_diagram(vertex.at("diagram"));
  return std::make_shared<const tket::zx::ZXBox>(nested);
}

ZXDiagram parse_diagram(const json& input) {
  if (!input.is_object() || input.at("schema").get<std::string>() !=
                                "tket-zx-v1") {
    throw std::invalid_argument("expected a tket-zx-v1 diagram object");
  }
  const auto& vertices_json = input.at("vertices");
  const auto& boundary_json = input.at("boundary");
  const auto& wires_json = input.at("wires");
  if (!vertices_json.is_array() || !boundary_json.is_array() ||
      !wires_json.is_array()) {
    throw std::invalid_argument(
        "ZX diagram vertices, boundary, and wires must be arrays");
  }

  std::map<unsigned, const json*> records;
  for (const auto& vertex : vertices_json) {
    const unsigned id = vertex.at("id").get<unsigned>();
    if (!records.emplace(id, &vertex).second) {
      throw std::invalid_argument("duplicate ZX vertex id");
    }
  }

  std::vector<unsigned> boundary_ids;
  std::set<unsigned> boundary_set;
  for (const auto& id_json : boundary_json) {
    const unsigned id = id_json.get<unsigned>();
    if (!boundary_set.insert(id).second || records.find(id) == records.end()) {
      throw std::invalid_argument("invalid or duplicate ZX boundary id");
    }
    const ZXType type =
        zx_type_from_name(records.at(id)->at("type").get<std::string>());
    if (!tket::zx::is_boundary_type(type)) {
      throw std::invalid_argument("ZX boundary id does not name a boundary");
    }
    boundary_ids.push_back(id);
  }

  ZXDiagram diagram;
  std::map<unsigned, ZXVert> vertices;
  const auto add_vertex = [&](unsigned id) {
    if (vertices.find(id) != vertices.end()) return;
    const auto record_it = records.find(id);
    if (record_it == records.end()) {
      throw std::invalid_argument("ZX wire refers to an unknown vertex id");
    }
    const json& record = *record_it->second;
    const ZXVert vertex = diagram.add_vertex(generator_from_json_with_boxes(record));
    vertices.emplace(id, vertex);
  };

  for (const unsigned id : boundary_ids) {
    add_vertex(id);
    diagram.add_boundary(vertices.at(id));
  }
  for (const auto& [id, unused_record] : records) {
    (void)unused_record;
    add_vertex(id);
  }

  for (const auto& wire : wires_json) {
    const unsigned source_id = wire.at("source").get<unsigned>();
    const unsigned target_id = wire.at("target").get<unsigned>();
    const auto source_it = vertices.find(source_id);
    const auto target_it = vertices.find(target_id);
    if (source_it == vertices.end() || target_it == vertices.end()) {
      throw std::invalid_argument("ZX wire refers to an unknown vertex id");
    }
    const WireProperties properties(
        wire_type_from_name(wire.at("type").get<std::string>()),
        qtype_from_name(wire.at("qtype").get<std::string>()),
        optional_uint(wire.at("source_port")),
        optional_uint(wire.at("target_port")));
    diagram.add_wire(
        source_it->second, target_it->second, properties);
  }

  if (input.contains("scalar")) {
    diagram.multiply_scalar(input.at("scalar").get<Expr>());
  }
  diagram.check_validity();
  return diagram;
}

Rewrite rewrite_from_enum(TketZXRewrite rewrite) {
  switch (rewrite) {
    case TKET_ZX_DECOMPOSE_BOXES:
      return Rewrite::decompose_boxes();
    case TKET_ZX_BASIC_WIRES:
      return Rewrite::basic_wires();
    case TKET_ZX_REBASE_TO_ZX:
      return Rewrite::rebase_to_zx();
    case TKET_ZX_REBASE_TO_MBQC:
      return Rewrite::rebase_to_mbqc();
    case TKET_ZX_RED_TO_GREEN:
      return Rewrite::red_to_green();
    case TKET_ZX_SPIDER_FUSION:
      return Rewrite::spider_fusion();
    case TKET_ZX_SELF_LOOP_REMOVAL:
      return Rewrite::self_loop_removal();
    case TKET_ZX_PARALLEL_H_REMOVAL:
      return Rewrite::parallel_h_removal();
    case TKET_ZX_SEPARATE_BOUNDARIES:
      return Rewrite::separate_boundaries();
    case TKET_ZX_IO_EXTENSION:
      return Rewrite::io_extension();
    case TKET_ZX_REMOVE_INTERIOR_CLIFFORDS:
      return Rewrite::remove_interior_cliffords();
    case TKET_ZX_REMOVE_INTERIOR_PAULIS:
      return Rewrite::remove_interior_paulis();
    case TKET_ZX_GADGETISE_INTERIOR_PAULIS:
      return Rewrite::gadgetise_interior_paulis();
    case TKET_ZX_MERGE_GADGETS:
      return Rewrite::merge_gadgets();
    case TKET_ZX_EXTEND_AT_BOUNDARY_PAULIS:
      return Rewrite::extend_at_boundary_paulis();
    case TKET_ZX_EXTEND_FOR_PX_OUTPUTS:
      return Rewrite::extend_for_PX_outputs();
    case TKET_ZX_INTERNALISE_GADGETS:
      return Rewrite::internalise_gadgets();
    case TKET_ZX_TO_GRAPHLIKE_FORM:
      return Rewrite::to_graphlike_form();
    case TKET_ZX_REDUCE_GRAPHLIKE_FORM:
      return Rewrite::reduce_graphlike_form();
    case TKET_ZX_TO_MBQC_DIAG:
      return Rewrite::to_MBQC_diag();
  }
  throw std::invalid_argument("unknown ZX rewrite");
}

const char* rewrite_name(TketZXRewrite rewrite) {
  switch (rewrite) {
    case TKET_ZX_DECOMPOSE_BOXES:
      return "decompose_boxes";
    case TKET_ZX_BASIC_WIRES:
      return "basic_wires";
    case TKET_ZX_REBASE_TO_ZX:
      return "rebase_to_zx";
    case TKET_ZX_REBASE_TO_MBQC:
      return "rebase_to_mbqc";
    case TKET_ZX_RED_TO_GREEN:
      return "red_to_green";
    case TKET_ZX_SPIDER_FUSION:
      return "spider_fusion";
    case TKET_ZX_SELF_LOOP_REMOVAL:
      return "self_loop_removal";
    case TKET_ZX_PARALLEL_H_REMOVAL:
      return "parallel_h_removal";
    case TKET_ZX_SEPARATE_BOUNDARIES:
      return "separate_boundaries";
    case TKET_ZX_IO_EXTENSION:
      return "io_extension";
    case TKET_ZX_REMOVE_INTERIOR_CLIFFORDS:
      return "remove_interior_cliffords";
    case TKET_ZX_REMOVE_INTERIOR_PAULIS:
      return "remove_interior_paulis";
    case TKET_ZX_GADGETISE_INTERIOR_PAULIS:
      return "gadgetise_interior_paulis";
    case TKET_ZX_MERGE_GADGETS:
      return "merge_gadgets";
    case TKET_ZX_EXTEND_AT_BOUNDARY_PAULIS:
      return "extend_at_boundary_paulis";
    case TKET_ZX_EXTEND_FOR_PX_OUTPUTS:
      return "extend_for_px_outputs";
    case TKET_ZX_INTERNALISE_GADGETS:
      return "internalise_gadgets";
    case TKET_ZX_TO_GRAPHLIKE_FORM:
      return "to_graphlike_form";
    case TKET_ZX_REDUCE_GRAPHLIKE_FORM:
      return "reduce_graphlike_form";
    case TKET_ZX_TO_MBQC_DIAG:
      return "to_mbqc_diag";
  }
  return nullptr;
}

}  // namespace

extern "C" {

TketZXDiagram* tket_zx_diagram_from_json(const char* json_str) {
  clear_error();
  if (json_str == nullptr) {
    set_error("JSON string must not be NULL");
    return nullptr;
  }
  try {
    auto* result = new TketZXDiagram;
    result->diagram = parse_diagram(json::parse(json_str));
    return result;
  } catch (const std::exception& error) {
    set_error(std::string("invalid ZX diagram JSON: ") + error.what());
    return nullptr;
  }
}

TketError tket_zx_diagram_to_json(
    const TketZXDiagram* diagram, char** json_str) {
  clear_error();
  if (diagram == nullptr) {
    set_error("ZX diagram must not be NULL");
    return TKET_ERROR_NULL_POINTER;
  }
  try {
    return write_string(serialize_diagram(diagram->diagram).dump(), json_str);
  } catch (const std::exception& error) {
    set_error(std::string("failed to serialize ZX diagram: ") + error.what());
    return TKET_ERROR_TRANSFORM_FAILED;
  }
}

TketZXDiagram* tket_zx_diagram_from_circuit_json(const char* json_str) {
  clear_error();
  if (json_str == nullptr) {
    set_error("circuit JSON must not be NULL");
    return nullptr;
  }
  try {
    const Circuit circuit = json::parse(json_str);
    auto* result = new TketZXDiagram;
    result->diagram = tket::circuit_to_zx(circuit).first;
    return result;
  } catch (const std::exception& error) {
    set_error(std::string("invalid circuit JSON for ZX conversion: ") +
              error.what());
    return nullptr;
  }
}

TketError tket_zx_diagram_to_circuit_json(
    const TketZXDiagram* diagram, char** json_str) {
  clear_error();
  if (diagram == nullptr) {
    set_error("ZX diagram must not be NULL");
    return TKET_ERROR_NULL_POINTER;
  }
  try {
    const Circuit circuit = tket::zx_to_circuit(diagram->diagram);
    return write_string(json(circuit).dump(), json_str);
  } catch (const std::exception& error) {
    set_error(std::string("failed to extract a circuit from ZX diagram: ") +
              error.what());
    return TKET_ERROR_TRANSFORM_FAILED;
  }
}

TketError tket_zx_diagram_to_graphviz(
    const TketZXDiagram* diagram, char** graphviz_str) {
  clear_error();
  if (diagram == nullptr) {
    set_error("ZX diagram must not be NULL");
    return TKET_ERROR_NULL_POINTER;
  }
  try {
    return write_string(diagram->diagram.to_graphviz_str(), graphviz_str);
  } catch (const std::exception& error) {
    set_error(std::string("failed to render ZX diagram: ") + error.what());
    return TKET_ERROR_TRANSFORM_FAILED;
  }
}

TketError tket_zx_diagram_metrics_json(
    const TketZXDiagram* diagram, char** json_str) {
  clear_error();
  if (diagram == nullptr) {
    set_error("ZX diagram must not be NULL");
    return TKET_ERROR_NULL_POINTER;
  }
  try {
    json metrics = {
        {"schema", "tket-zx-metrics-v1"},
        {"n_vertices", diagram->diagram.n_vertices()},
        {"n_wires", diagram->diagram.n_wires()},
        {"n_basic_wires", diagram->diagram.count_wires(ZXWireType::Basic)},
        {"n_h_wires", diagram->diagram.count_wires(ZXWireType::H)},
        {"is_graphlike", diagram->diagram.is_graphlike()},
        {"is_mbqc", diagram->diagram.is_MBQC()},
        {"scalar", diagram->diagram.get_scalar()},
        {"vertex_counts", json::object()},
    };
    for (int value = static_cast<int>(ZXType::Input);
         value <= static_cast<int>(ZXType::ZXBox); ++value) {
      const ZXType type = static_cast<ZXType>(value);
      metrics["vertex_counts"][zx_type_name(type)] =
          diagram->diagram.count_vertices(type);
    }
    return write_string(metrics.dump(), json_str);
  } catch (const std::exception& error) {
    set_error(std::string("failed to compute ZX metrics: ") + error.what());
    return TKET_ERROR_TRANSFORM_FAILED;
  }
}

TketError tket_zx_apply_rewrite(
    TketZXDiagram* diagram, TketZXRewrite rewrite) {
  clear_error();
  if (diagram == nullptr) {
    set_error("ZX diagram must not be NULL");
    return TKET_ERROR_NULL_POINTER;
  }
  try {
    rewrite_from_enum(rewrite).apply(diagram->diagram);
    return TKET_SUCCESS;
  } catch (const std::exception& error) {
    set_error(std::string("ZX rewrite failed: ") + error.what());
    return TKET_ERROR_TRANSFORM_FAILED;
  }
}

TketError tket_zx_apply_rewrite_sequence(
    TketZXDiagram* diagram, const TketZXRewrite* rewrites,
    uint32_t rewrite_count, bool repeat, uint32_t max_iterations) {
  clear_error();
  if (diagram == nullptr) {
    set_error("ZX diagram must not be NULL");
    return TKET_ERROR_NULL_POINTER;
  }
  if (rewrite_count != 0 && rewrites == nullptr) {
    set_error("rewrite array must not be NULL when rewrite_count is nonzero");
    return TKET_ERROR_NULL_POINTER;
  }
  try {
    std::vector<Rewrite> sequence;
    sequence.reserve(rewrite_count);
    for (uint32_t index = 0; index < rewrite_count; ++index) {
      sequence.push_back(rewrite_from_enum(rewrites[index]));
    }
    const auto apply_once = [&]() {
      bool changed = false;
      for (const Rewrite& rewrite : sequence) {
        changed = rewrite.apply(diagram->diagram) || changed;
      }
      return changed;
    };
    if (!repeat) {
      apply_once();
      return TKET_SUCCESS;
    }
    const uint32_t iterations = max_iterations == 0 ? 1024 : max_iterations;
    for (uint32_t index = 0; index < iterations; ++index) {
      if (!apply_once()) return TKET_SUCCESS;
    }
    set_error("ZX rewrite sequence reached max_iterations before convergence");
    return TKET_ERROR_TRANSFORM_FAILED;
  } catch (const std::exception& error) {
    set_error(std::string("ZX rewrite sequence failed: ") + error.what());
    return TKET_ERROR_TRANSFORM_FAILED;
  }
}

const char* tket_zx_rewrite_name(TketZXRewrite rewrite) {
  return rewrite_name(rewrite);
}

bool tket_zx_rewrite_from_name(
    const char* name, TketZXRewrite* rewrite) {
  if (name == nullptr || rewrite == nullptr) return false;
  for (int value = static_cast<int>(TKET_ZX_DECOMPOSE_BOXES);
       value <= static_cast<int>(TKET_ZX_TO_MBQC_DIAG); ++value) {
    const auto candidate = static_cast<TketZXRewrite>(value);
    const char* candidate_name = rewrite_name(candidate);
    if (candidate_name != nullptr && std::strcmp(name, candidate_name) == 0) {
      *rewrite = candidate;
      return true;
    }
  }
  return false;
}

const char* tket_zx_last_error(void) { return last_zx_error.c_str(); }

void tket_free_zx_diagram(TketZXDiagram* diagram) { delete diagram; }

}  // extern "C"
