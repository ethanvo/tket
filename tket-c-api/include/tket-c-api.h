#ifndef TKET_C_API_H
#define TKET_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// Opaque handle to tket Circuit object
typedef struct TketCircuit TketCircuit;

// Opaque handle to a tket ZXDiagram object
typedef struct TketZXDiagram TketZXDiagram;

// Opaque handle to tket BasePass object
typedef struct TketPass TketPass;

// Error handling
typedef enum {
  TKET_SUCCESS = 0,
  TKET_ERROR_NULL_POINTER = 1,
  TKET_ERROR_CIRCUIT_INVALID = 2,
  TKET_ERROR_INVALID_ARGUMENT = 3,
  TKET_ERROR_UNSUPPORTED_METHOD = 4,
  TKET_ERROR_TRANSFORM_FAILED = 5,
} TketError;

// Target gate types for two_qubit_squash
typedef enum { TKET_TARGET_CX = 0, TKET_TARGET_TK2 = 1 } TketTargetGate;

// Named optimization transforms exposed by the C-facing API. The
// implementation remains in TKET's C++ core; callers depend only on this ABI.
typedef enum {
  TKET_OPT_REMOVE_REDUNDANCIES = 0,
  TKET_OPT_SQUASH_SINGLES = 1,
  TKET_OPT_COMMUTE_THROUGH_MULTIS = 2,
  TKET_OPT_SQUASH_RZ_PHASEDX = 3,
  TKET_OPT_TWO_QUBIT_SQUASH = 4,
  TKET_OPT_PEEPHOLE_2Q = 5,
  TKET_OPT_FULL_PEEPHOLE = 6,
  TKET_OPT_SYNTHESISE_TK = 7,
  TKET_OPT_SYNTHESISE_TKET = 8,
  TKET_OPT_CLIFFORD_SIMP = 9,
  TKET_OPT_PHASE_GADGETS = 10,
  TKET_OPT_ZX_GRAPHLIKE = 11,
  TKET_OPT_THREE_QUBIT_SQUASH = 12,
  TKET_OPT_PAIRWISE_PAULI_GADGETS = 13,
  TKET_OPT_PAULI_SIMP = 14,
  TKET_OPT_PAULI_SQUASH = 15,
  TKET_OPT_CLIFFORD_RESYNTHESIS = 16,
  TKET_OPT_GREEDY_PAULI_SIMP = 17,
  TKET_OPT_ROUND_ANGLES = 18,
  TKET_OPT_GUIDED_PAULI_SIMP = 19,
  TKET_OPT_MULTIQ_CLIFFORD_REPLACEMENT = 20,
  TKET_OPT_COPY_PI_THROUGH_CX = 21,
  TKET_OPT_KAK_DECOMPOSITION = 22,
  TKET_OPT_DECOMPOSE_TK2 = 23,
  TKET_OPT_NORMALISE_TK2 = 24,
  TKET_OPT_SYNTHESISE_UMD = 25,
  TKET_OPT_CONTEXTUAL = 26,
  TKET_OPT_SIMPLIFY_INITIAL = 27,
  TKET_OPT_PUSH_CLIFFORDS_THROUGH_MEASURES = 28,
} TketOptimizationMethod;

typedef enum {
  TKET_PAULI_INDIVIDUAL = 0,
  TKET_PAULI_PAIRWISE = 1,
  TKET_PAULI_SETS = 2,
  TKET_PAULI_GREEDY = 3,
} TketPauliSynthesisStrategy;

typedef enum {
  TKET_CX_SNAKE = 0,
  TKET_CX_TREE = 1,
  TKET_CX_STAR = 2,
  TKET_CX_MULTIQGATE = 3,
} TketCXConfig;

// Named rewrites exposed by the tket ZXDiagram implementation.
typedef enum {
  TKET_ZX_DECOMPOSE_BOXES = 0,
  TKET_ZX_BASIC_WIRES = 1,
  TKET_ZX_REBASE_TO_ZX = 2,
  TKET_ZX_REBASE_TO_MBQC = 3,
  TKET_ZX_RED_TO_GREEN = 4,
  TKET_ZX_SPIDER_FUSION = 5,
  TKET_ZX_SELF_LOOP_REMOVAL = 6,
  TKET_ZX_PARALLEL_H_REMOVAL = 7,
  TKET_ZX_SEPARATE_BOUNDARIES = 8,
  TKET_ZX_IO_EXTENSION = 9,
  TKET_ZX_REMOVE_INTERIOR_CLIFFORDS = 10,
  TKET_ZX_REMOVE_INTERIOR_PAULIS = 11,
  TKET_ZX_GADGETISE_INTERIOR_PAULIS = 12,
  TKET_ZX_MERGE_GADGETS = 13,
  TKET_ZX_EXTEND_AT_BOUNDARY_PAULIS = 14,
  TKET_ZX_EXTEND_FOR_PX_OUTPUTS = 15,
  TKET_ZX_INTERNALISE_GADGETS = 16,
  TKET_ZX_TO_GRAPHLIKE_FORM = 17,
  TKET_ZX_REDUCE_GRAPHLIKE_FORM = 18,
  TKET_ZX_TO_MBQC_DIAG = 19,
} TketZXRewrite;

/*
 * Versioned options for optimization methods with tunable synthesis or
 * approximation policies. Initialize with tket_optimization_options_init()
 * before changing individual fields.
 */
typedef struct {
  uint32_t version;
  uint32_t size;
  TketTargetGate target_gate;
  double cx_fidelity;
  bool allow_swaps;
  TketPauliSynthesisStrategy pauli_strategy;
  TketCXConfig cx_config;
  double discount_rate;
  double depth_weight;
  uint32_t max_lookahead;
  uint32_t max_tqe_candidates;
  uint32_t seed;
  bool allow_zzphase;
  uint32_t thread_timeout;
  bool only_reduce;
  uint32_t trials;
  uint32_t round_bits;
  bool only_zeros;
  bool allow_global_phase;
  bool use_cx_fidelity;
  bool use_zzmax_fidelity;
  bool use_zzphase_fidelity;
  double zzmax_fidelity;
  double zzphase_fidelity;
  bool allow_classical;
  bool create_all_qubits;
} TketOptimizationOptions;

// Conversion between Circuit and c-string JSON
TketCircuit *tket_circuit_from_json(const char *json_str);
TketError tket_circuit_to_json(const TketCircuit *circuit, char **json_str);

// Loading a Pass from its c-string JSON
TketPass *tket_pass_from_json(const char *json_str);

// Applying a pass to a circuit
TketError tket_apply_pass(TketCircuit *circuit, const TketPass *pass);

// Free memory
void tket_free_circuit(TketCircuit *circuit);
void tket_free_pass(TketPass *pass);
void tket_free_string(char *str);

// Transform functions

/**
 * Apply two_qubit_squash transform to the circuit
 *
 * Squash sequences of two-qubit operations into minimal form using KAK
 * decomposition. Can decompose to TK2 or CX gates.
 *
 * @param circuit Circuit to transform (modified in-place)
 * @param target_gate Target two-qubit gate type (CX or TK2)
 * @param cx_fidelity Estimated CX gate fidelity (used when target_gate=CX)
 * @param allow_swaps Whether to allow implicit wire swaps
 * @return TKET_SUCCESS if successful, error code otherwise
 */
TketError tket_two_qubit_squash(
    TketCircuit *circuit, TketTargetGate target_gate, double cx_fidelity,
    bool allow_swaps);

/**
 * Apply clifford_simp transform to the circuit
 *
 * Resynthesise all Clifford subcircuits and simplify using Clifford rules.
 * This can significantly reduce the two-qubit gate count for Clifford-heavy
 * circuits.
 *
 * @param circuit Circuit to transform (modified in-place)
 * @param allow_swaps Whether the rewriting may introduce wire swaps
 * @return TKET_SUCCESS if successful, error code otherwise
 */
TketError tket_clifford_simp(
    TketCircuit *circuit, TketTargetGate target_gate, bool allow_swaps);

/**
 * Squash sequences of single-qubit gates into PhasedX and Rz gates.
 *
 * Also remove identity gates. Commute Rz gates to the back if possible.
 *
 * @param circuit Circuit to transform (modified in-place)
 * @return TKET_SUCCESS if successful, error code otherwise
 */
TketError tket_squash_phasedx_rz(TketCircuit *circuit);

/** Apply one named optimization transform in-place. */
TketError tket_apply_optimization(
    TketCircuit *circuit, TketOptimizationMethod method,
    TketTargetGate target_gate, double cx_fidelity, bool allow_swaps);

/** Initialize a TketOptimizationOptions value with stable defaults. */
void tket_optimization_options_init(TketOptimizationOptions *options);

/** Apply one named optimization transform using explicit options. */
TketError tket_apply_optimization_with_options(
    TketCircuit *circuit, TketOptimizationMethod method,
    const TketOptimizationOptions *options);

/** Return a stable method name, or NULL for an invalid method. */
const char *tket_optimization_method_name(TketOptimizationMethod method);

// ZXDiagram JSON and rewrite interface.
TketZXDiagram *tket_zx_diagram_from_json(const char *json_str);
TketError tket_zx_diagram_to_json(
    const TketZXDiagram *diagram, char **json_str);
TketZXDiagram *tket_zx_diagram_from_circuit_json(const char *json_str);
TketError tket_zx_diagram_to_circuit_json(
    const TketZXDiagram *diagram, char **json_str);
TketError tket_zx_diagram_to_graphviz(
    const TketZXDiagram *diagram, char **graphviz_str);
TketError tket_zx_diagram_metrics_json(
    const TketZXDiagram *diagram, char **json_str);
TketError tket_zx_apply_rewrite(
    TketZXDiagram *diagram, TketZXRewrite rewrite);
TketError tket_zx_apply_rewrite_sequence(
    TketZXDiagram *diagram, const TketZXRewrite *rewrites,
    uint32_t rewrite_count, bool repeat, uint32_t max_iterations);
const char *tket_zx_rewrite_name(TketZXRewrite rewrite);
bool tket_zx_rewrite_from_name(
    const char *name, TketZXRewrite *rewrite);
const char *tket_zx_last_error(void);
void tket_free_zx_diagram(TketZXDiagram *diagram);

// Utility functions
const char *tket_error_string(TketError error);
const char *tket_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // TKET_C_API_H
