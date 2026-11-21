#ifndef IPLANE_NESTING_H
#define IPLANE_NESTING_H
/* Independent native research implementation. Not the supplier's SDK binary.
 * Recompile clients with this header and iPlaneNesting.lib. No ordinal ABI promise.
 * All objects belong to their session and expire when it is deleted.
 */
#ifdef _WIN32
#ifdef ALMA_STUDY_BUILD
#define PN2S_API __declspec(dllexport)
#else
#define PN2S_API __declspec(dllimport)
#endif
#define PN2S_CALL __stdcall
#else
#define PN2S_API __attribute__((visibility("default")))
#define PN2S_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef int PN2_Bool;
typedef int PN2_ErrorCode;
typedef struct {
    double x, y;
} PN2_Point;
typedef struct {
    /* Reflect Y after rotation (reflection about the horizontal axis). */
    PN2_Bool horizontal_flip;
    /* Degrees, counterclockwise. Ordered intervals of width <= 360 are allowed. */
    double min_angle, max_angle;
} PN2_Orientation;
typedef struct PN2_SessionInternal *PN2_Session;
typedef struct PN2_ShapeInternal *PN2_Shape;
typedef struct PN2_ContourInternal *PN2_Contour;
typedef struct PN2_PartInternal *PN2_Part;
typedef struct PN2_SheetInternal *PN2_Sheet;
typedef struct PN2_ResultInternal *PN2_Result;
typedef struct PN2_MultiResultInternal *PN2_MultiResult;
typedef enum { ALWAYS_GLUE=0, NEVER_GLUE=1, GLUE_ON_NEST_MORE_ONLY=2 } PN2_PRE_NESTED_GLUE_MODE;
enum {
    PN2S_OK = 0,
    PN2S_INVALID = -20001,
    PN2S_UNSUPPORTED = -20002,
    PN2S_RESOURCE_LIMIT = -20003,
    PN2S_TIMEOUT = -20004,
    PN2S_INTERNAL = -20005
};
/* 2: recovered Offset value/reference factory on finite, grid-aligned, zero-gap
 * single-ring inputs. 3 (new-session default): recovered supervisor, original
 * presets, concrete strategy families, candidate pools and post-optimization.
 * Some geometry producers remain unfinished; whole-engine parity is not claimed.
 * Material/continuous-angle inputs continue through the general geometry host. */
enum { PN2S_MAP_GREEDY = 0, PN2S_RECTANGLE_RECURSIVE = 1, PN2S_OFFSET_RECOVERED = 2,
       PN2S_DEFAULT_RECOVERED = 3 };
PN2S_API const char *PN2S_CALL PN2S_Version(void);
/* Thread-local text: consume before the next session operation on this thread. */
PN2S_API const char *PN2S_CALL PN2S_LastErrorDetail(void);
PN2S_API PN2_Session PN2S_CALL PN2_CreateSession(void);
PN2S_API PN2_Bool PN2S_CALL PN2_DeleteSession(PN2_Session);
PN2S_API PN2_Orientation PN2S_CALL PN2_CreateOrientation(double angle);
PN2S_API PN2_Orientation PN2S_CALL PN2_CreateXFlippedOrientation(double angle);
/* A closed continuous interval [angle-tolerance, angle+tolerance]. */
PN2S_API PN2_Orientation PN2S_CALL PN2_CreateRangeOrientation(double angle, double tolerance);
PN2S_API PN2_Orientation PN2S_CALL PN2_CreateFreeOrientation(PN2_Bool flip);
/* CCW simple ring; do not repeat the closing vertex. Input arrays are copied. */
PN2S_API PN2_Shape PN2S_CALL PN2_AddShape(PN2_Session session, const PN2_Point *vertices,
                                          int vertex_count, PN2_Point pivot);
/* Session-owned contours. The final edge joins the last point to the first.
 * Positive bulges/sagittas place the arc to the right of its directed chord.
 * For a straight center edge, centers[i] equals points[(i+1)%count].
 * Center directions: 0 clockwise, 1 counterclockwise. A circle needs two arcs.
 */
PN2S_API PN2_Contour PN2S_CALL PN2_CreateContourFromPoints(PN2_Session,
    const PN2_Point *, int count);
PN2S_API PN2_Contour PN2S_CALL PN2_CreateContourFromArcsBulges(PN2_Session,
    const PN2_Point *, const double *bulges, int count);
PN2S_API PN2_Contour PN2S_CALL PN2_CreateContourFromArcsSagittas(PN2_Session,
    const PN2_Point *, const double *sagittas, int count);
PN2S_API PN2_Contour PN2S_CALL PN2_CreateContourFromArcsCenters(PN2_Session,
    const PN2_Point *, const PN2_Point *centers, const PN2_Bool *directions, int count);
/* Arc sampling uses the recovered default relative precision of .0001.
 * Outer contours are sampled outwards, holes inwards before material checks.
 */
PN2S_API PN2_Shape PN2S_CALL PN2_AddShapeFromContour(PN2_Session, PN2_Contour, PN2_Point pivot);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_ShapeAddHoleFromContour(PN2_Session, PN2_Shape, PN2_Contour);
PN2S_API PN2_Part PN2S_CALL PN2_AddPart(PN2_Session session, PN2_Shape shape,
                                        const PN2_Orientation *poses, int pose_count);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_AddSeveralParts(PN2_Session session, PN2_Shape shape,
                                                     const PN2_Orientation *poses, int pose_count,
                                                     int quantity, PN2_Part *parts_out);
/* Note height first, length along X second. All dimensions use caller's units. */
PN2S_API PN2_Sheet PN2S_CALL PN2_AddSheet(PN2_Session, double height, double length);
/* Standard polygon-stock entries. The original bounding-box minimum must be
 * (0,0); inputs are copied and an optional repeated closing point is accepted.
 * Border gap is a Euclidean margin on the outer boundary. Sheet defects below
 * have their own independent offsets. Supported defects are disjoint simple
 * rings strictly inside the stock. Nonuniform rectangle border gaps are not
 * supplied by this implementation.
 * These sheets select the shared native material/batch engine in PN2_Nest and
 * PN2_MultiNest, including permitted continuous angular intervals. They do not
 * establish supplier-engine equivalence. Material domain limits: 500 concrete
 * parts, 100 physical boards/types, 64 poses per type, 8192 vertices per ring.
 */
PN2S_API PN2_Sheet PN2S_CALL PN2_AddPolygonalSheet(PN2_Session, const PN2_Point *, int point_count);
PN2S_API PN2_Sheet PN2S_CALL PN2_AddPolygonalSheetWithBorderGap(PN2_Session, const PN2_Point *, int point_count, double border_gap);
/* Existing rich-contour sampling is conservative: stock inwards, defects
 * outwards. Their coordinates retain the caller's original sheet frame. */
PN2S_API PN2_Sheet PN2S_CALL PN2_AddSheetFromContour(PN2_Session, PN2_Contour);
PN2S_API PN2_Sheet PN2S_CALL PN2_AddSheetFromContourWithBorderGap(PN2_Session, PN2_Contour, double border_gap);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SheetAddDefect(PN2_Session, PN2_Sheet, const PN2_Point *, int point_count);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SheetAddDefectWithOffset(PN2_Session, PN2_Sheet, const PN2_Point *, int point_count, double offset);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SheetAddDefectFromContour(PN2_Session, PN2_Sheet, PN2_Contour);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SheetAddDefectFromContourWithOffset(PN2_Session, PN2_Sheet, PN2_Contour, double offset);
/* Explicit engineering extensions: up to 64 source orientation constraints,
 * including intervals, and a non-grid material gap in [0,1000]. Old AddPart /
 * AddSeveralParts retain their 32-pose limit; SetRectangleGap retains its grid
 * requirement. Typed material parts and SetMaterialGap select the material
 * route even on a rectangular PN2_AddSheet. Successful SetRectangleGap clears
 * the explicit material-gap selection. These calls do not enable technology,
 * predefined continuation, L-offcut or simultaneous cutting heads. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_AddMaterialParts(PN2_Session, PN2_Shape,
    const PN2_Orientation *, int pose_count, int quantity, PN2_Part *parts_out);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_SetMaterialGap(PN2_Session, double gap);
typedef struct {
    unsigned int struct_size;
    PN2_Bool is_material;
    PN2_Bool stopped;
    /* Construction + replacement + consolidation + repack attempts. */
    unsigned long long trials;
    /* Immutable UTF-8 text owned by the session. Valid until session deletion;
     * subsequent SDK operations do not invalidate it. Empty for legacy results.
     * stopped can describe a retained, verified partial material result. */
    const char *warning_utf8;
} PN2S_MaterialResultInfo;
/* Accepts a live Result or MultiResult from this session. No output mutation
 * on failure. Set struct_size=sizeof(PN2S_MaterialResultInfo) before querying. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_GetMaterialResultInfos(PN2_Session,
    const void *result_or_multi, PN2S_MaterialResultInfo *info_out);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_SetStrategy(PN2_Session, int strategy);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_SetRandomSeed(PN2_Session, unsigned int seed);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_SetPitch(PN2_Session, double pitch); /* 0: adaptive */
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_SetRectangleGap(PN2_Session, double gap);
/* L-offcut objective: maximize reusable top/right stairs, retaining the sheet
 * height. Currently requires the recovered default supervisor, finite exact-grid
 * poses, single rings, zero gap and zero protection. MultiNest applies it only
 * to its last sheet. The minimum arm height defaults to 0; the setter preserves
 * the supplied double exactly, as the recovered SDK setter does. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2_ActivateLOffcut(PN2_Session, PN2_Bool activate);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetLOffcutMinimumUsableHeight(PN2_Session, double height);
/* Instance handles must be distinct. A successful result can leave parts unplaced. */
PN2S_API PN2_Result PN2S_CALL PN2_Nest(PN2_Session session, PN2_Sheet sheet, const PN2_Part *parts,
                                       int part_count, double seconds);
/* Manual pre-nesting applies the supplied constraints, including the original
 * behavior of accepting overlap/outside-sheet geometry. Finite grid poses,
 * single rings and zero gap/protection currently required. Arrays are copied;
 * repeated SDK identities remain unsupported until their native mapping is restored. */
PN2S_API PN2_Result PN2S_CALL PN2_PreNest(PN2_Session, PN2_Sheet, const PN2_Part *,
    const PN2_Orientation *, const PN2_Point *, int part_count);
/* Complete the existing result through the mandatory pre-nester and recovered
 * default strategy. Zero new parts returns the same result handle. Existing
 * manual defects are preserved only while those concrete parts are unchanged. */
PN2S_API PN2_Result PN2S_CALL PN2_NestMore(PN2_Session, PN2_Result, const PN2_Part *,
    int part_count, double seconds);
/* Default 2: fixed in NestMore, movable in Shake/Compact. Mode 0 always fixes,
 * mode 1 permits movement. Other enum bit patterns are stored as in the SDK. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetPreNestedGlueMode(PN2_Session, PN2_Part,
    PN2_PRE_NESTED_GLUE_MODE);
/* Recovered integer-price entry: direct manager6, no Shaker postop. Prices are
 * copied in SDK record order; the last price for one source shape is retained
 * across its pose catalogs. Finite grid poses, single rings, zero gap/protection and ordinary
 * objective only. This is separate from a shape's native objective value. */
PN2S_API PN2_Result PN2S_CALL PN2_PricedNest(PN2_Session session, PN2_Sheet sheet,
    const PN2_Part *parts, int *prices, int part_count, double seconds);
/* Explicit research entry for recovered Generator14 + Bt. This is not the
 * original default scheduler. The numeric field suffixes identify recovered
 * upstream fields whose complete producers have not yet been restored.
 * Finite grid poses, one outer ring, zero gap; values[i] belongs to parts[i].
 * Reference: 0 normal, 1 wide, 2 triangle. length_mode must be 0 or 1.
 * Seed comes from PN2S_SetRandomSeed; it persists across rounds within this call.
 */
typedef struct {
    unsigned int struct_size;
    int batch_size, reference_kind, length_mode;
    double direction_scale_218, direction_scale_220;
    double target_14b8, queue_20, maximum_length;
} PN2S_Direction14Options;
/* Generator10: each pair shares three directions, A uses new perturbed
 * ShapeComplexity weights, B and outer scoring retain caller values.
 * count <= 0 generates no pairs; positive count is limited to 10000.
 * The perimeter probability/scale are explicit original configuration fields.
 */
typedef struct {
    unsigned int struct_size;
    int batch_size, reference_kind, length_mode;
    double direction_scale_218, direction_scale_220;
    double target_14b8, queue_20, maximum_length;
    int candidate_count_10;
    double alpha_50, gamma_58, perimeter_probability_1d8, perimeter_scale_1e0;
} PN2S_Direction10Options;
PN2S_API PN2_Result PN2S_CALL PN2S_NestDirection10(PN2_Session session, PN2_Sheet sheet,
    const PN2_Part *parts, int part_count, const double *values,
    const PN2S_Direction10Options *options, double seconds);
PN2S_API PN2_Result PN2S_CALL PN2S_NestDirection14(PN2_Session session, PN2_Sheet sheet,
    const PN2_Part *parts, int part_count, const double *values,
    const PN2S_Direction14Options *options, double seconds);
/* Configured original Generator18, not the full default candidate factory.
 * Same input domain and explicit upstream fields as Direction14.
 * Generates only for a group's remaining count >= 2 * batch_size.
 * candidate_limit_18 is the original signed field, checked AFTER append:
 * <= 0 permits one pair when eligible; k >= 0 permits at most k + 1 pairs.
 * Alone this route can leave parts unplaced; query every input instance.
 */
typedef struct {
    unsigned int struct_size;
    int batch_size, reference_kind, length_mode;
    double direction_scale_218, direction_scale_220;
    double target_14b8, queue_20, maximum_length;
    int candidate_limit_18;
} PN2S_Direction18Options;
PN2S_API PN2_Result PN2S_CALL PN2S_NestDirection18(PN2_Session session, PN2_Sheet sheet,
    const PN2_Part *parts, int part_count, const double *values,
    const PN2S_Direction18Options *options, double seconds);
/* Configured 441ec0 factory: 14,18,1c,10 order; positive fields enable routes.
 * Uses empty process lists. 1c adds per-type noise and per-evaluation randomness.
 * replay_previous appends previous B as A, followed by the last NEW pair's B.
 * filter_replay_front is an explicit upstream predicate, not inferred licensing
 * or process state. fixed_front is in caller units, zero disables the bound.
 * Same finite grid/single-ring/zero-gap domain as the individual Bt entries.
 * This still requires caller-supplied context, not the original default scheduler.
 */
typedef struct {
    unsigned int struct_size;
    int batch_size, reference_kind, length_mode;
    double direction_scale_218, direction_scale_220;
    double target_14b8, queue_20, maximum_length;
    int count_10, flag_14, limit_18, count_1c;
    double alpha_50, gamma_58, perimeter_probability_1d8, perimeter_scale_1e0;
    double fixed_front;
    int force_process_inspection, replay_previous, filter_replay_front;
} PN2S_BtOptions;
PN2S_API PN2_Result PN2S_CALL PN2S_NestBt(PN2_Session session, PN2_Sheet sheet,
    const PN2_Part *parts, int part_count, const double *values,
    const PN2S_BtOptions *options, double seconds);
/* Recovered MultiStrategy factory (44e880), followed by the Bt host.
   Single sheet, finite exact-grid poses, single rings, zero gap. Generates its
   own Surface/ShapeComplexity values and default 5/5/0/5 strategy fields.
   Context batch/direction fields and target/queue remain explicit upstream inputs.
   Replay controls belong to the host adapter; this is not the full supervisor. */
typedef struct {
    unsigned int struct_size;
    int context_batch_238, current_reference_kind, length_mode;
    double direction_scale_218, direction_scale_220;
    double target_14b8, queue_20, maximum_length;
    double perimeter_probability_1d8, perimeter_scale_1e0, alpha_limit_260, gamma_limit_268;
    double fixed_front;
    int force_process_inspection, replay_previous, filter_replay_front;
} PN2S_MultiStrategyOptions;
PN2S_API PN2_Result PN2S_CALL PN2S_NestMultiStrategy(PN2_Session session, PN2_Sheet sheet,
    const PN2_Part *parts, int count, const PN2S_MultiStrategyOptions *options, double seconds);
/* Recovered scheduling kernel. These functions select caller-registered manager
   IDs; they do not instantiate vendor managers or run nesting workers.
   Each handle owns one explicit seeded random stream; original workers can
   supply separate streams to the C++ kernel. Entries are ordered by ID, as in
   the preset map. calibration is virtual slot2
   (e.g. Multi=17, Rectangle=250), not a measured millisecond duration. */
typedef void *PN2S_StrategySchedule;
typedef struct { int strategy_id, calibration; double weight, accumulated_budget; } PN2S_StrategyEntry;
typedef struct {
    unsigned int struct_size, seed;
    int worker_count, initial_trials, max_trials, initial_pool_limit;
    double time_1c0, total_weight, pool_growth_1568;
} PN2S_ScheduleOptions;
PN2S_API PN2S_StrategySchedule PN2S_CALL PN2S_CreateStrategySchedule(PN2_Session,
    const PN2S_StrategyEntry *entries, int count, const PN2S_ScheduleOptions *);
/* 1 selected, 0 trial limit reached, negative error. Exhaustion consumes no RNG.
   Operations are serialized by the DLL. A failed call leaves state unchanged. */
PN2S_API int PN2S_CALL PN2S_NextScheduledStrategy(PN2_Session, PN2S_StrategySchedule,
    int *strategy_id, int *trial_count, int *pool_limit);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_GetScheduleEntry(PN2_Session, PN2S_StrategySchedule,
    int index, PN2S_StrategyEntry *entry, double *sampling_weight);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_DeleteStrategySchedule(PN2_Session, PN2S_StrategySchedule);
typedef struct { double metric; int source_id; } PN2S_StrategyResult;
typedef struct { int strategy_id; double weight; } PN2S_StrategyWeight;
/* Nonempty candidate pool in its EXISTING order. No sorting or quality inference.
   Query with output=NULL, capacity=0; otherwise capacity must cover all weights.
   Config fields retain original integer truncation/wrap semantics; zero divisor
   is rejected. Nonpositive source IDs consume prefix positions but add no weight. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_SelectStrategyWeights(const PN2S_StrategyResult *pool,
    int count, int trials, double config_198, double config_190,
    PN2S_StrategyWeight *output, int capacity, int *weight_count, int *equal_prefix, int *selected_count);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetResultInfos(PN2_Session, PN2_Result, double *occupied_length,
                                                    int *placed_count);
/* Zero-based index. Position is the transformed pivot's translation, not bbox min. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetKthNestedPart(PN2_Session session, PN2_Result result, int index,
                                                      PN2_Part *part_out, PN2_Point *position_out,
                                                      PN2_Orientation *orientation_out);
/* Returns 1 found, 0 unplaced, negative error code: do not treat every nonzero as true. */
PN2S_API PN2_Bool PN2S_CALL PN2_GetNestedPart(PN2_Session, PN2_Result, PN2_Part, PN2_Point *,
                                              PN2_Orientation *);
/* Finite stock quantities, multiple sheet types, conserved part instances.
 * Uses native pattern portfolios and cross-board merging, not the supplier LP. */
PN2S_API PN2_MultiResult PN2S_CALL PN2_MultiNest(PN2_Session session, const PN2_Sheet *sheets,
                                                 int sheet_type_count, const int *sheet_quantities,
                                                 const PN2_Part *parts, int part_count, double seconds);
/* Manual multi-board input uses one global concrete-instance catalog. Counts
 * slice the flat arrays in board order; repeated sheet handles are distinct
 * physical boards. Empty boards and manual overlap/outside positions persist.
 * Finite grid poses, clean single rings, zero gap/protection, unique SDK IDs. */
PN2S_API PN2_MultiResult PN2S_CALL PN2_MultiPreNest(PN2_Session, int sheet_count,
    const PN2_Sheet *, const int *parts_per_sheet, const PN2_Part *,
    const PN2_Orientation *, const PN2_Point *);
/* Continue a live MultiPreNest or MultiNestMore result with explicit recovered
 * Offset selected. Keeps placed old instances fixed, drops old missing demand,
 * and fills old physical boards before using new stock. New stock is optional.
 * Unique SDK IDs, finite grid single rings, zero gap/protection; moving old
 * glue mode 1 and the full default continuation controller are unsupported. */
PN2S_API PN2_MultiResult PN2S_CALL PN2_MultiNestMore(PN2_Session, PN2_MultiResult,
    const PN2_Sheet *, int sheet_type_count, const int *sheet_quantities,
    PN2_Part *, int added_count, double seconds);
/* Cutting-head configuration follows the original setter comparison rules.
 * Invalid arguments/indexes return -973. The floating setters accept NaN and
 * infinity as the original does. Linked queries read stored result relations;
 * an unlinked ID has count one and index zero returns that same ID. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsNumber(PN2_Session, int);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsMinimumGap(PN2_Session, double);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetCuttingHeadsMaximumGap(PN2_Session, double);
/* The original SDK spells this entry Additionnal. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2_SetAdditionnalHeadCuttingUnitCost(PN2_Session, double);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetPartMultipleHeadsNumber(PN2_Session, PN2_Result, PN2_Part, int *);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetPartMultipleHeadsLinkedPart(PN2_Session, PN2_Result, PN2_Part, int, PN2_Part *);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_GetMultiResultInfos(PN2_Session, PN2_MultiResult, int *);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_GetMultiUnplacedCount(PN2_Session, PN2_MultiResult, int *);
PN2S_API PN2_ErrorCode PN2S_CALL PN2S_GetMultiUnplacedPart(PN2_Session, PN2_MultiResult, int,
                                                           PN2_Part *);
PN2S_API PN2_Result PN2S_CALL PN2_GetResult(PN2_Session, PN2_MultiResult, int, PN2_Sheet *);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_ShapeAddHole(PN2_Session, PN2_Shape, const PN2_Point *, int);
/* Protection lies around the material and inside holes. Pair clearance is the
 * maximum of both protections and the session gap. Protection may cross sheet
 * borders. The finite coordinate domain accepts offsets in [0, 1000000]. */
PN2S_API PN2_ErrorCode PN2S_CALL PN2_ShapeSetProtectionOffset(PN2_Session, PN2_Shape, double);
/* Post-optimization creates a new result owned by the same session. The source
 * result survives both successful and failed calls. Hole splitting, simultaneous
 * LP movement and continuous-angle refinement use the recovered straight kernel.
 * A geometrically rejected optimization returns a new copy of the source layout.
 * Compact preserves orientations. Direction 1 is bottom-left, other values use
 * top-left, matching the original branch. Shake minimizes occupied length and
 * stays in the connected allowed angle interval containing the current pose.
 * Gap/protection components use recovered integer/floating offsets. The public
 * resolution is the recovered SDK default 24. The host independently validates
 * material clearance before accepting the optimized layout.
 */
PN2S_API PN2_Result PN2S_CALL PN2_Shake(PN2_Session, PN2_Result, double);
PN2S_API PN2_Result PN2S_CALL PN2_Compact(PN2_Session, PN2_Result, int, double);
PN2S_API PN2_Bool PN2S_CALL PN2_IsObjectError(void *);
PN2S_API PN2_ErrorCode PN2S_CALL PN2_ErrorCodeFromObject(void *);
PN2S_API const char *PN2S_CALL PN2_ErrorMessage(int);
PN2S_API const char *PN2S_CALL PN2_ErrorMessageFromObject(void *);
#ifdef __cplusplus
}
static_assert(sizeof(PN2_Point) == 16);
static_assert(sizeof(PN2_Orientation) == 24);
#endif
#endif
