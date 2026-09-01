extern "C" {
#include "tket-c-api.h"
}

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

#include "tket/Circuit/Circuit.hpp"
#include "tket/Predicates/CompilerPass.hpp"
#include "tket/Predicates/PassLibrary.hpp"
#include "tket/Predicates/PassGenerators.hpp"
#include "tket/Transformations/BasicOptimisation.hpp"
#include "tket/Transformations/CliffordOptimisation.hpp"
#include "tket/Transformations/ContextualReduction.hpp"
#include "tket/Transformations/Decomposition.hpp"
#include "tket/Transformations/OptimisationPass.hpp"
#include "tket/Transformations/PauliOptimisation.hpp"
#include "tket/Transformations/Rebase.hpp"
#include "tket/Transformations/ThreeQubitSquash.hpp"

using namespace tket;
using json = nlohmann::json;

namespace {

thread_local std::string last_error;

void clear_error() { last_error.clear(); }

void set_error(const std::string &message) { last_error = message; }

template <class Exception>
TketError transform_error(const Exception &error) {
  set_error(error.what());
  return TKET_ERROR_TRANSFORM_FAILED;
}

std::optional<OpType> target_gate_type(TketTargetGate target_gate) {
  switch (target_gate) {
    case TKET_TARGET_CX:
      return OpType::CX;
    case TKET_TARGET_TK2:
      return OpType::TK2;
    default:
      return std::nullopt;
  }
}

std::optional<CXConfigType> cx_config_type(TketCXConfig cx_config) {
  switch (cx_config) {
    case TKET_CX_SNAKE:
      return CXConfigType::Snake;
    case TKET_CX_TREE:
      return CXConfigType::Tree;
    case TKET_CX_STAR:
      return CXConfigType::Star;
    case TKET_CX_MULTIQGATE:
      return CXConfigType::MultiQGate;
    default:
      return std::nullopt;
  }
}

std::optional<Transforms::PauliSynthStrat> pauli_strategy_type(
    TketPauliSynthesisStrategy strategy) {
  switch (strategy) {
    case TKET_PAULI_INDIVIDUAL:
      return Transforms::PauliSynthStrat::Individual;
    case TKET_PAULI_PAIRWISE:
      return Transforms::PauliSynthStrat::Pairwise;
    case TKET_PAULI_SETS:
      return Transforms::PauliSynthStrat::Sets;
    case TKET_PAULI_GREEDY:
      return Transforms::PauliSynthStrat::Greedy;
    default:
      return std::nullopt;
  }
}

Transform rebase_for_circuit_api(Transform transform) {
  return transform >> Transforms::rebase_pyzx();
}

void apply_pass(Circuit &circuit, const PassPtr &pass) {
  CompilationUnit cu(circuit);
  pass->apply(cu);
  circuit = cu.get_circ_ref();
}

bool valid_options(const TketOptimizationOptions &options) {
  if (options.version != 1 || options.size < sizeof(options)) {
    set_error("optimization options have an unsupported version or size");
    return false;
  }
  if (!target_gate_type(options.target_gate) ||
      !std::isfinite(options.cx_fidelity) || options.cx_fidelity < 0.0 ||
      options.cx_fidelity > 1.0) {
    set_error("target gate or CX fidelity is invalid");
    return false;
  }
  if (!cx_config_type(options.cx_config) ||
      !pauli_strategy_type(options.pauli_strategy)) {
    set_error("Pauli synthesis strategy or CX configuration is invalid");
    return false;
  }
  if (!std::isfinite(options.discount_rate) || options.discount_rate < 0.0 ||
      !std::isfinite(options.depth_weight) || options.depth_weight < 0.0 ||
      options.max_lookahead == 0 || options.max_tqe_candidates == 0 ||
      options.thread_timeout == 0 || options.trials == 0 ||
      options.round_bits >= 32) {
    set_error("optimization search or rounding options are invalid");
    return false;
  }
  const auto valid_fidelity = [](bool enabled, double fidelity) {
    return !enabled || (std::isfinite(fidelity) && fidelity >= 0.0 &&
                        fidelity <= 1.0);
  };
  if (!valid_fidelity(options.use_cx_fidelity, options.cx_fidelity) ||
      !valid_fidelity(options.use_zzmax_fidelity, options.zzmax_fidelity) ||
      !valid_fidelity(
          options.use_zzphase_fidelity, options.zzphase_fidelity)) {
    set_error("two-qubit gate fidelities must be between zero and one");
    return false;
  }
  return true;
}

}  // namespace

struct TketCircuit {
  Circuit circuit;
};

struct TketPass {
  PassPtr pass;
};

TketCircuit *tket_circuit_from_json(const char *json_str) {
  clear_error();
  if (!json_str) {
    set_error("JSON string must not be NULL");
    return nullptr;
  }

  TketCircuit *tc = nullptr;
  try {
    tc = new TketCircuit;
    tc->circuit = json::parse(json_str);
  } catch (const std::exception &error) {
    set_error(std::string("Invalid circuit JSON: ") + error.what());
    if (tc) tket_free_circuit(tc);
    tc = nullptr;
  }
  return tc;
}

TketError tket_circuit_to_json(const TketCircuit *tc, char **json_str) {
  clear_error();
  if (!tc || !json_str) return TKET_ERROR_NULL_POINTER;
  *json_str = nullptr;

  std::string serialized;
  try {
    serialized = json(tc->circuit).dump();
  } catch (const std::exception &error) {
    set_error(std::string("Invalid circuit: ") + error.what());
    return TKET_ERROR_CIRCUIT_INVALID;
  }

  *json_str = static_cast<char *>(std::malloc(serialized.size() + 1));
  if (!*json_str) {
    set_error("Could not allocate serialized circuit");
    return TKET_ERROR_CIRCUIT_INVALID;
  }
  std::memcpy(*json_str, serialized.c_str(), serialized.size() + 1);
  return TKET_SUCCESS;
}

TketPass *tket_pass_from_json(const char *json_str) {
  clear_error();
  if (!json_str) {
    set_error("JSON string must not be NULL");
    return nullptr;
  }

  TketPass *tp = nullptr;
  try {
    const json j = json::parse(json_str);
    tp = new TketPass;
    tp->pass = deserialise(j);
  } catch (const std::exception &error) {
    set_error(std::string("Invalid pass JSON: ") + error.what());
    if (tp) tket_free_pass(tp);
    tp = nullptr;
  }
  return tp;
}

TketError tket_apply_pass(TketCircuit *tc, const TketPass *tp) {
  if (!tc || !tp) return TKET_ERROR_NULL_POINTER;
  try {
    CompilationUnit cu(tc->circuit);
    tp->pass->apply(cu);
    tc->circuit = cu.get_circ_ref();
    clear_error();
    return TKET_SUCCESS;
  } catch (const std::exception &error) {
    return transform_error(error);
  }
}

void tket_free_circuit(TketCircuit *tc) { delete tc; }

void tket_free_pass(TketPass *tp) { delete tp; }

void tket_free_string(char *str) { std::free(str); }

TketError tket_two_qubit_squash(
    TketCircuit *tc, TketTargetGate target_gate, double cx_fidelity,
    bool allow_swaps) {
  if (!tc) return TKET_ERROR_NULL_POINTER;
  const auto target = target_gate_type(target_gate);
  if (!target || !std::isfinite(cx_fidelity) || cx_fidelity < 0.0 ||
      cx_fidelity > 1.0) {
    set_error("target gate or CX fidelity is invalid");
    return TKET_ERROR_INVALID_ARGUMENT;
  }
  try {
    Transforms::two_qubit_squash(*target, cx_fidelity, allow_swaps)
        .apply(tc->circuit);
    clear_error();
    return TKET_SUCCESS;
  } catch (const std::exception &error) {
    return transform_error(error);
  }
}

TketError tket_clifford_simp(
    TketCircuit *tc, TketTargetGate target_gate, bool allow_swaps) {
  if (!tc) return TKET_ERROR_NULL_POINTER;
  const auto target = target_gate_type(target_gate);
  if (!target) {
    set_error("target gate is invalid");
    return TKET_ERROR_INVALID_ARGUMENT;
  }
  try {
    Transforms::clifford_simp(allow_swaps, *target).apply(tc->circuit);
    clear_error();
    return TKET_SUCCESS;
  } catch (const std::exception &error) {
    return transform_error(error);
  }
}

TketError tket_squash_phasedx_rz(TketCircuit *tc) {
  if (!tc) return TKET_ERROR_NULL_POINTER;
  try {
    Transforms::squash_1qb_to_Rz_PhasedX().apply(tc->circuit);
    clear_error();
    return TKET_SUCCESS;
  } catch (const std::exception &error) {
    return transform_error(error);
  }
}

void tket_optimization_options_init(TketOptimizationOptions *options) {
  if (!options) return;
  *options = TketOptimizationOptions{
      1,
      sizeof(TketOptimizationOptions),
      TKET_TARGET_CX,
      1.0,
      false,
      TKET_PAULI_SETS,
      TKET_CX_SNAKE,
      0.7,
      0.3,
      500,
      500,
      0,
      false,
      100,
      false,
      1,
      0,
      false,
      false,
      false,
      false,
      false,
      1.0,
      1.0,
      true,
      false};
}

TketError tket_apply_optimization_with_options(
    TketCircuit *tc, TketOptimizationMethod method,
    const TketOptimizationOptions *options) {
  if (!tc || !options) return TKET_ERROR_NULL_POINTER;
  if (!valid_options(*options)) {
    return TKET_ERROR_INVALID_ARGUMENT;
  }
  const auto target = target_gate_type(options->target_gate);
  const auto cx_config = cx_config_type(options->cx_config);
  const auto pauli_strategy = pauli_strategy_type(options->pauli_strategy);

  try {
    switch (method) {
      case TKET_OPT_REMOVE_REDUNDANCIES:
        rebase_for_circuit_api(Transforms::remove_redundancies())
            .apply(tc->circuit);
        break;
      case TKET_OPT_SQUASH_SINGLES:
        rebase_for_circuit_api(Transforms::squash_1qb_to_tk1())
            .apply(tc->circuit);
        break;
      case TKET_OPT_COMMUTE_THROUGH_MULTIS:
        rebase_for_circuit_api(Transforms::commute_through_multis())
            .apply(tc->circuit);
        break;
      case TKET_OPT_SQUASH_RZ_PHASEDX:
        rebase_for_circuit_api(Transforms::squash_1qb_to_Rz_PhasedX())
            .apply(tc->circuit);
        break;
      case TKET_OPT_TWO_QUBIT_SQUASH:
        rebase_for_circuit_api(
            Transforms::two_qubit_squash(
                *target, options->cx_fidelity, options->allow_swaps))
            .apply(tc->circuit);
        break;
      case TKET_OPT_PEEPHOLE_2Q:
        rebase_for_circuit_api(
            Transforms::peephole_optimise_2q(options->allow_swaps))
            .apply(tc->circuit);
        break;
      case TKET_OPT_FULL_PEEPHOLE:
        rebase_for_circuit_api(
            Transforms::full_peephole_optimise(
                options->allow_swaps, *target))
            .apply(tc->circuit);
        break;
      case TKET_OPT_SYNTHESISE_TK:
        rebase_for_circuit_api(Transforms::synthesise_tk())
            .apply(tc->circuit);
        break;
      case TKET_OPT_SYNTHESISE_TKET:
        rebase_for_circuit_api(Transforms::synthesise_tket())
            .apply(tc->circuit);
        break;
      case TKET_OPT_CLIFFORD_SIMP:
        rebase_for_circuit_api(
            Transforms::clifford_simp(options->allow_swaps, *target))
            .apply(tc->circuit);
        break;
      case TKET_OPT_PHASE_GADGETS:
        rebase_for_circuit_api(
            Transforms::optimise_via_PhaseGadget(*cx_config))
            .apply(tc->circuit);
        break;
      case TKET_OPT_ZX_GRAPHLIKE: {
        Transforms::rebase_pyzx().apply(tc->circuit);
        CompilationUnit cu(tc->circuit);
        ZXGraphlikeOptimisation(options->allow_swaps)->apply(cu);
        tc->circuit = cu.get_circ_ref();
        Transforms::rebase_pyzx().apply(tc->circuit);
        break;
      }
      case TKET_OPT_THREE_QUBIT_SQUASH:
        rebase_for_circuit_api(Transforms::three_qubit_squash(*target))
            .apply(tc->circuit);
        break;
      case TKET_OPT_PAIRWISE_PAULI_GADGETS:
        Transforms::rebase_pyzx().apply(tc->circuit);
        rebase_for_circuit_api(
            Transforms::pairwise_pauli_gadgets(*cx_config))
            .apply(tc->circuit);
        break;
      case TKET_OPT_PAULI_SIMP:
        Transforms::rebase_pyzx().apply(tc->circuit);
        apply_pass(
            tc->circuit,
            gen_synthesise_pauli_graph(*pauli_strategy, *cx_config));
        Transforms::rebase_pyzx().apply(tc->circuit);
        break;
      case TKET_OPT_PAULI_SQUASH:
        Transforms::rebase_pyzx().apply(tc->circuit);
        apply_pass(tc->circuit, PauliSquash(*pauli_strategy, *cx_config));
        Transforms::rebase_pyzx().apply(tc->circuit);
        break;
      case TKET_OPT_CLIFFORD_RESYNTHESIS:
        apply_pass(
            tc->circuit,
            gen_clifford_resynthesis_pass(std::nullopt, options->allow_swaps));
        Transforms::rebase_pyzx().apply(tc->circuit);
        break;
      case TKET_OPT_GREEDY_PAULI_SIMP:
        Transforms::rebase_pyzx().apply(tc->circuit);
        apply_pass(
            tc->circuit,
            gen_greedy_pauli_simp(
                options->discount_rate, options->depth_weight,
                options->max_lookahead, options->max_tqe_candidates,
                options->seed, options->allow_zzphase,
                options->thread_timeout, options->only_reduce,
                options->trials));
        Transforms::rebase_pyzx().apply(tc->circuit);
        break;
      case TKET_OPT_ROUND_ANGLES:
        apply_pass(
            tc->circuit,
            RoundAngles(options->round_bits, options->only_zeros));
        Transforms::rebase_pyzx().apply(tc->circuit);
        break;
      case TKET_OPT_GUIDED_PAULI_SIMP:
        Transforms::rebase_pyzx().apply(tc->circuit);
        apply_pass(
            tc->circuit,
            gen_special_UCC_synthesis(*pauli_strategy, *cx_config));
        Transforms::rebase_pyzx().apply(tc->circuit);
        break;
      case TKET_OPT_MULTIQ_CLIFFORD_REPLACEMENT:
        rebase_for_circuit_api(
            Transforms::multiq_clifford_replacement(options->allow_swaps))
            .apply(tc->circuit);
        break;
      case TKET_OPT_COPY_PI_THROUGH_CX:
        rebase_for_circuit_api(Transforms::copy_pi_through_CX())
            .apply(tc->circuit);
        break;
      case TKET_OPT_KAK_DECOMPOSITION:
        rebase_for_circuit_api(
            Transforms::two_qubit_squash(
                *target, options->cx_fidelity, options->allow_swaps))
            .apply(tc->circuit);
        break;
      case TKET_OPT_DECOMPOSE_TK2: {
        Transforms::TwoQbFidelities fidelities;
        if (options->use_cx_fidelity) {
          fidelities.CX_fidelity = options->cx_fidelity;
        }
        if (options->use_zzmax_fidelity) {
          fidelities.ZZMax_fidelity = options->zzmax_fidelity;
        }
        if (options->use_zzphase_fidelity) {
          fidelities.ZZPhase_fidelity = options->zzphase_fidelity;
        }
        rebase_for_circuit_api(
            Transforms::normalise_TK2() >>
            Transforms::decompose_TK2(fidelities, options->allow_swaps))
            .apply(tc->circuit);
        break;
      }
      case TKET_OPT_NORMALISE_TK2:
        Transforms::normalise_TK2().apply(tc->circuit);
        break;
      case TKET_OPT_SYNTHESISE_UMD:
        rebase_for_circuit_api(Transforms::synthesise_UMD())
            .apply(tc->circuit);
        break;
      case TKET_OPT_CONTEXTUAL:
        apply_pass(
            tc->circuit,
            gen_contextual_pass(
                options->allow_classical ? Transforms::AllowClassical::Yes
                                         : Transforms::AllowClassical::No));
        break;
      case TKET_OPT_SIMPLIFY_INITIAL:
        Transforms::simplify_initial(
                options->allow_classical ? Transforms::AllowClassical::Yes
                                          : Transforms::AllowClassical::No,
                options->create_all_qubits ? Transforms::CreateAllQubits::Yes
                                            : Transforms::CreateAllQubits::No)
            .apply(tc->circuit);
        break;
      case TKET_OPT_PUSH_CLIFFORDS_THROUGH_MEASURES:
        Transforms::push_cliffords_through_measures().apply(tc->circuit);
        break;
      default:
        set_error("optimization method is invalid");
        return TKET_ERROR_UNSUPPORTED_METHOD;
    }
    clear_error();
    return TKET_SUCCESS;
  } catch (const std::exception &error) {
    return transform_error(error);
  }
}

TketError tket_apply_optimization(
    TketCircuit *tc, TketOptimizationMethod method,
    TketTargetGate target_gate, double cx_fidelity, bool allow_swaps) {
  if (!tc) return TKET_ERROR_NULL_POINTER;
  TketOptimizationOptions options;
  tket_optimization_options_init(&options);
  options.target_gate = target_gate;
  options.cx_fidelity = cx_fidelity;
  options.allow_swaps = allow_swaps;
  return tket_apply_optimization_with_options(tc, method, &options);
}

const char *tket_optimization_method_name(TketOptimizationMethod method) {
  switch (method) {
    case TKET_OPT_REMOVE_REDUNDANCIES:
      return "remove_redundancies";
    case TKET_OPT_SQUASH_SINGLES:
      return "squash_singles";
    case TKET_OPT_COMMUTE_THROUGH_MULTIS:
      return "commute_through_multis";
    case TKET_OPT_SQUASH_RZ_PHASEDX:
      return "squash_rz_phasedx";
    case TKET_OPT_TWO_QUBIT_SQUASH:
      return "two_qubit_squash";
    case TKET_OPT_PEEPHOLE_2Q:
      return "peephole_2q";
    case TKET_OPT_FULL_PEEPHOLE:
      return "full_peephole";
    case TKET_OPT_SYNTHESISE_TK:
      return "synthesise_tk";
    case TKET_OPT_SYNTHESISE_TKET:
      return "synthesise_tket";
    case TKET_OPT_CLIFFORD_SIMP:
      return "clifford_simp";
    case TKET_OPT_PHASE_GADGETS:
      return "phase_gadgets";
    case TKET_OPT_ZX_GRAPHLIKE:
      return "zx_graphlike";
    case TKET_OPT_THREE_QUBIT_SQUASH:
      return "three_qubit_squash";
    case TKET_OPT_PAIRWISE_PAULI_GADGETS:
      return "pairwise_pauli_gadgets";
    case TKET_OPT_PAULI_SIMP:
      return "pauli_simp";
    case TKET_OPT_PAULI_SQUASH:
      return "pauli_squash";
    case TKET_OPT_CLIFFORD_RESYNTHESIS:
      return "clifford_resynthesis";
    case TKET_OPT_GREEDY_PAULI_SIMP:
      return "greedy_pauli_simp";
    case TKET_OPT_ROUND_ANGLES:
      return "round_angles";
    case TKET_OPT_GUIDED_PAULI_SIMP:
      return "guided_pauli_simp";
    case TKET_OPT_MULTIQ_CLIFFORD_REPLACEMENT:
      return "multiq_clifford_replacement";
    case TKET_OPT_COPY_PI_THROUGH_CX:
      return "copy_pi_through_cx";
    case TKET_OPT_KAK_DECOMPOSITION:
      return "kak_decomposition";
    case TKET_OPT_DECOMPOSE_TK2:
      return "decompose_tk2";
    case TKET_OPT_NORMALISE_TK2:
      return "normalise_tk2";
    case TKET_OPT_SYNTHESISE_UMD:
      return "synthesise_umd";
    case TKET_OPT_CONTEXTUAL:
      return "contextual";
    case TKET_OPT_SIMPLIFY_INITIAL:
      return "simplify_initial";
    case TKET_OPT_PUSH_CLIFFORDS_THROUGH_MEASURES:
      return "push_cliffords_through_measures";
    default:
      return nullptr;
  }
}

const char *tket_error_string(TketError error) {
  switch (error) {
    case TKET_SUCCESS:
      return "Success";
    case TKET_ERROR_NULL_POINTER:
      return "Invalid NULL pointer in arguments";
    case TKET_ERROR_CIRCUIT_INVALID:
      return "Invalid circuit: could not convert to JSON";
    case TKET_ERROR_INVALID_ARGUMENT:
      return "Invalid argument";
    case TKET_ERROR_UNSUPPORTED_METHOD:
      return "Unsupported optimization method";
    case TKET_ERROR_TRANSFORM_FAILED:
      return "Optimization transform failed";
    default:
      return "Unknown error";
  }
}

const char *tket_last_error(void) { return last_error.c_str(); }
