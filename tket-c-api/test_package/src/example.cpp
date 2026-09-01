#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "tket-c-api.h"

int main() {
  std::string circ_json =
      R"({"bits": [], "commands": [{"args": [["q", [0]], ["q", [1]]], "op": {"type": "CX"}}, {"args": [["q", [1]], ["q", [0]]], "op": {"type": "CX"}}], "created_qubits": [], "discarded_qubits": [], "implicit_permutation": [[["q", [0]], ["q", [0]]], [["q", [1]], ["q", [1]]]], "phase": "0.0", "qubits": [["q", [0]], ["q", [1]]]})";
  TketCircuit *circ = tket_circuit_from_json(circ_json.c_str());
  assert(circ != nullptr);

  std::string pass_json =
      R"({"StandardPass": {"allow_swaps": false, "basis_allowed": ["H", "Rz", "CZ"], "name": "AutoRebase"}, "pass_class": "StandardPass"})";
  TketPass *pass = tket_pass_from_json(pass_json.c_str());
  assert(pass != nullptr);

  TketError rv = tket_apply_pass(circ, pass);
  if (rv != TKET_SUCCESS) {
    std::cerr << "Error applying pass: " << rv << std::endl;
    return -1;
  }

  rv = tket_apply_optimization(
      circ, TKET_OPT_REMOVE_REDUNDANCIES, TKET_TARGET_CX, 1.0, false);
  assert(rv == TKET_SUCCESS);
  assert(std::string(tket_optimization_method_name(
             TKET_OPT_REMOVE_REDUNDANCIES)) == "remove_redundancies");

  TketOptimizationOptions options;
  tket_optimization_options_init(&options);
  options.pauli_strategy = TKET_PAULI_PAIRWISE;
  options.cx_config = TKET_CX_TREE;
  rv = tket_apply_optimization_with_options(
      circ, TKET_OPT_PAIRWISE_PAULI_GADGETS, &options);
  assert(rv == TKET_SUCCESS);
  assert(std::string(tket_optimization_method_name(
             TKET_OPT_PAIRWISE_PAULI_GADGETS)) == "pairwise_pauli_gadgets");

  const TketOptimizationMethod additional_methods[] = {
      TKET_OPT_GUIDED_PAULI_SIMP,
      TKET_OPT_MULTIQ_CLIFFORD_REPLACEMENT,
      TKET_OPT_COPY_PI_THROUGH_CX,
      TKET_OPT_KAK_DECOMPOSITION,
      TKET_OPT_DECOMPOSE_TK2,
      TKET_OPT_NORMALISE_TK2,
      TKET_OPT_SYNTHESISE_UMD,
      TKET_OPT_CONTEXTUAL,
      TKET_OPT_SIMPLIFY_INITIAL,
      TKET_OPT_PUSH_CLIFFORDS_THROUGH_MEASURES};
  for (const TketOptimizationMethod method : additional_methods) {
    assert(tket_optimization_method_name(method) != nullptr);
  }

  const std::string zx_json = R"({
    "schema": "tket-zx-v1",
    "scalar": "1",
    "boundary": [0, 3],
    "vertices": [
      {"id": 0, "type": "Input", "qtype": "Quantum"},
      {"id": 1, "type": "ZSpider", "qtype": "Quantum", "param": "0"},
      {"id": 2, "type": "ZSpider", "qtype": "Quantum", "param": "0"},
      {"id": 3, "type": "Output", "qtype": "Quantum"}
    ],
    "wires": [
      {"source": 0, "target": 1, "type": "Basic", "qtype": "Quantum", "source_port": null, "target_port": null},
      {"source": 1, "target": 2, "type": "Basic", "qtype": "Quantum", "source_port": null, "target_port": null},
      {"source": 2, "target": 3, "type": "Basic", "qtype": "Quantum", "source_port": null, "target_port": null}
    ]
  })";
  TketZXDiagram *zx = tket_zx_diagram_from_json(zx_json.c_str());
  assert(zx != nullptr);
  char *zx_metrics = nullptr;
  assert(tket_zx_diagram_metrics_json(zx, &zx_metrics) == TKET_SUCCESS);
  assert(strstr(zx_metrics, "n_vertices") != nullptr);
  tket_free_string(zx_metrics);
  assert(tket_zx_apply_rewrite(zx, TKET_ZX_SPIDER_FUSION) == TKET_SUCCESS);
  assert(tket_zx_diagram_to_json(zx, &zx_metrics) == TKET_SUCCESS);
  assert(strstr(zx_metrics, "ZSpider") != nullptr);
  tket_free_string(zx_metrics);
  tket_free_zx_diagram(zx);

  char *circ1_json = nullptr;
  tket_circuit_to_json(circ, &circ1_json);
  assert(strstr(circ1_json, "CZ"));

  tket_free_circuit(circ);
  tket_free_pass(pass);
  free(circ1_json);

  std::cout << "Success" << std::endl;
  return 0;
}
