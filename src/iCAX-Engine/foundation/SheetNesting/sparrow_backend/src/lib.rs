//! Job-local bridge to the pinned Sparrow algorithm (MPL-2.0).
//! Cooperative lifecycle and single-instance exchange patches are documented in
//! src/third_party/sparrow/ICAX-UPSTREAM.json; optimization stages are retained.
//! Search results are proposals: the C++ host validates original material.

use std::collections::{HashMap, HashSet};
use std::ffi::{CStr, CString, c_char, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::time::{Duration, Instant};
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::io::ext_repr::ExtSPInstance;
use rand::{SeedableRng, rngs::SmallRng};
use serde::Deserialize;
use serde_json::{Value, json};
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::util::listener::{ReportType, SolutionListener};
use sparrow::util::terminator::Terminator;
use jagua_rs::probs::spp::entities::{SPInstance, SPPlacement, SPProblem, SPSolution};
use jagua_rs::geometry::DTransformation;
use jagua_rs::entities::Instance;
use sparrow::optimizer::separator::Separator;
use sparrow::quantify::tracker::CollisionTracker;

pub const UPSTREAM_REVISION: &str = "ab2aaef602755f1ed51b15406a2f0ef0950e18ce";
pub type CancelCallback = unsafe extern "C" fn(*mut c_void) -> bool;

#[derive(Deserialize)]
struct Request {
    protocol: u32,
    instance: Value,
    seed: u64,
    time_limit_seconds: f64,
    threads: usize,
    exploration_fraction: f64,
    polygon_simplification_ratio: Option<f32>,
    narrow_concavity_ratio: Option<f32>,
    numerical_guard: f32,
    mode: Option<String>,
    initial_layout: Option<Vec<InitialPlacement>>,
    initial_occupied_width: Option<f64>,
    stock_width_cap: Option<f64>,
}

#[derive(Deserialize)]
struct InitialPlacement {
    part_index: u64,
    instance: u64,
    x: f64,
    y: f64,
    angle_degrees: f64,
}

fn restore_warm_problem(request: &Request, ext: &ExtSPInstance, instance: &SPInstance) -> Result<SPProblem, String> {
    let poses = request.initial_layout.as_ref().ok_or("warm-refine requires complete original initial_layout")?;
    let width = request.initial_occupied_width.ok_or("warm-refine requires original occupied width")?;
    let cap = request.stock_width_cap.ok_or("warm-refine requires original stock cap")?;
    if !width.is_finite() || !cap.is_finite() || width <= 0.0 || width > cap || cap <= 0.0 {
        return Err("invalid warm-refine original width or stock cap".into());
    }
    let mut cap_width = cap as f32;
    if cap_width as f64 > cap { cap_width = cap_width.next_down(); }
    if !cap_width.is_finite() || cap_width <= 0.0 {
        return Err("warm-refine width cannot be represented within original bounds".into());
    }
    let required: usize = ext.items.iter().map(|item| item.demand as usize).sum();
    if poses.len() != required { return Err("warm-refine initial layout is not complete".into()); }
    let originals = request.instance["items"].as_array().ok_or("missing original items")?;
    let mut identities = HashSet::new();
    for pose in poses {
        let id = usize::try_from(pose.part_index).map_err(|_| "foreign warm-refine source index")?;
        if id >= ext.items.len() || pose.instance >= ext.items[id].demand
            || !identities.insert((pose.part_index, pose.instance)) {
            return Err("warm-refine initial layout has foreign, duplicate or out-of-demand original identity".into());
        }
        if !pose.x.is_finite() || !pose.y.is_finite() || !pose.angle_degrees.is_finite()
            || !(pose.x as f32).is_finite() || !(pose.y as f32).is_finite() {
            return Err("warm-refine initial pose is nonfinite or outside f32 range".into());
        }
        if let Some(domain) = originals[id].get("allowed_orientations").and_then(Value::as_array) {
            if !domain.iter().filter_map(Value::as_f64).any(|angle|
                ((pose.angle_degrees - angle + 180.0).rem_euclid(360.0) - 180.0).abs() <= 1e-9) {
                return Err("warm-refine initial angle is outside original finite domain".into());
            }
        }
    }
    let mut problem = SPProblem::new(instance.clone());
    problem.change_strip_width(cap_width);
    for pose in poses {
        let id = pose.part_index as usize;
        let external = DTransformation::new(pose.angle_degrees.to_radians() as f32, (pose.x as f32, pose.y as f32));
        let internal = jagua_rs::io::import::ext_to_int_transformation(&external, &instance.item(id).shape_orig.pre_transform);
        problem.place_item(SPPlacement { item_id: id, d_transf: internal });
    }
    if problem.item_demand_qtys.iter().any(|quantity| *quantity != 0) {
        return Err("warm-refine original demands were not exactly restored".into());
    }
    // Restore into the unchanged physical stock cap. If lower precision has
    // introduced collisions, the caller may run one explicitly marked bounded
    // separation pass; this loader never reports the imported seed feasible.
    Ok(problem)
}

fn warm_restore_diagnostics(request: &Request, instance: &SPInstance, problem: &SPProblem, epoch: Instant) -> Result<Value,String> {
    let supplied=request.initial_layout.as_ref().ok_or("missing warm poses")?;
    let exported=jagua_rs::probs::spp::io::export(instance,&problem.save(),epoch);
    if supplied.len()!=exported.layout.placed_items.len() { return Err("warm round-trip inventory mismatch".into()); }
    let mut rows=Vec::new();let (mut dx,mut dy,mut da)=(0.0_f64,0.0_f64,0.0_f64);
    for ((original,external),internal) in supplied.iter().zip(exported.layout.placed_items.iter()).zip(problem.layout.placed_items.values()) {
        if external.item_id!=original.part_index || internal.item_id as u64!=original.part_index {
            return Err("warm initial round-trip changed original item identity/order".into());
        }
        let x=external.transformation.translation.0 as f64;let y=external.transformation.translation.1 as f64;
        let angle=(external.transformation.rotation as f64).to_degrees();
        let angle_delta=(angle-original.angle_degrees+180.0).rem_euclid(360.0)-180.0;
        dx=dx.max((x-original.x).abs());dy=dy.max((y-original.y).abs());da=da.max(angle_delta.abs());
        let b=internal.shape.bbox;
        rows.push(json!({"part_index":original.part_index,"instance":original.instance,"x":x,"y":y,"angle_degrees":angle,
            "delta_x":x-original.x,"delta_y":y-original.y,"delta_angle_degrees":angle_delta,
            "internal_search_bbox":[b.x_min,b.y_min,b.x_max,b.y_max]}));
    }
    Ok(json!({"scope":"before any separation or compression","original_identity_order_verified":true,
        "maximum_absolute_x_delta":dx,"maximum_absolute_y_delta":dy,"maximum_absolute_angle_delta_degrees":da,"poses":rows}))
}

fn collision_diagnostics(separator: &Separator) -> Value {
    json!({"loss":separator.ct.get_total_loss(),"weighted_loss":separator.ct.get_total_weighted_loss(),
        "colliding_item_pairs":separator.ct.pair_collisions.data.iter().filter(|entry|entry.loss>0.0).count(),
        "colliding_container_items":separator.ct.container_collisions.iter().filter(|entry|entry.loss>0.0).count(),
        "placed":separator.prob.layout.placed_items.len(),"layout_feasible":separator.prob.layout.is_feasible()})
}

struct JobTerminator {
    global_deadline: Instant,
    phase_deadline: Option<Instant>,
    cancel: Option<CancelCallback>,
    user: *mut c_void,
}

impl JobTerminator {
    fn externally_cancelled(&self) -> bool {
        self.cancel.is_some_and(|callback| unsafe { callback(self.user) })
    }
}

impl Terminator for JobTerminator {
    fn kill(&self) -> bool {
        let now = Instant::now();
        now >= self.global_deadline
            || self.phase_deadline.is_some_and(|deadline| now >= deadline)
            || self.externally_cancelled()
    }
    fn new_timeout(&mut self, duration: Duration) {
        self.phase_deadline = Some((Instant::now() + duration).min(self.global_deadline));
    }
    fn timeout_at(&self) -> Option<Instant> {
        Some(self.phase_deadline.unwrap_or(self.global_deadline).min(self.global_deadline))
    }
}

struct JobListener {
    started: Instant,
    feasible_reports: usize,
    infeasible_reports: usize,
    first_feasible_seconds: Option<f64>,
    first_solution: Option<Value>,
    timeline: Vec<Value>,
}

impl SolutionListener for JobListener {
    fn report(&mut self, report: ReportType, solution: &SPSolution, instance: &SPInstance) {
        match report {
            ReportType::ExplFeas | ReportType::CmprFeas | ReportType::Final => {
                self.feasible_reports += 1;
                let seconds = self.started.elapsed().as_secs_f64();
                if self.first_feasible_seconds.is_none() {
                    self.first_feasible_seconds = Some(seconds);
                    self.first_solution = serde_json::to_value(jagua_rs::probs::spp::io::export(instance, solution, self.started)).ok();
                }
                if self.timeline.len() < 4096 {
                    self.timeline.push(json!({"seconds":seconds,"occupied_width":solution.strip_width()}));
                }
            },
            _ => self.infeasible_reports += 1,
        }
    }
}

fn positive_optional(value: Option<f32>, name: &str) -> Result<Option<f32>, String> {
    match value {
        Some(number) if !number.is_finite() || number < 0.0 || number > 1.0 => Err(format!("invalid {name}")),
        Some(0.0) => Ok(None),
        other => Ok(other),
    }
}

fn solve(text: &str, cancel: Option<CancelCallback>, user: *mut c_void) -> Result<Value, String> {
    let started = Instant::now();
    let request: Request = serde_json::from_str(text).map_err(|error| error.to_string())?;
    let warm = match request.mode.as_deref() {
        None => false,
        Some("warm_compress") => true,
        _ => return Err("unsupported Sparrow mode".into()),
    };
    if !warm && (request.initial_layout.is_some() || request.initial_occupied_width.is_some() || request.stock_width_cap.is_some()) {
        return Err("initial layout fields require explicit warm_compress mode".into());
    }
    if request.protocol != 1 || !request.time_limit_seconds.is_finite()
        || request.time_limit_seconds <= 0.0 || request.time_limit_seconds > 86400.0
        || !(1..=32).contains(&request.threads)
        || !request.exploration_fraction.is_finite()
        || !(0.0..1.0).contains(&request.exploration_fraction)
        || request.exploration_fraction == 0.0
        || !request.numerical_guard.is_finite() || request.numerical_guard < 0.0 {
        return Err("invalid protocol, time, threads, exploration ratio or numerical guard".into());
    }
    let ext: ExtSPInstance = serde_json::from_value(request.instance.clone()).map_err(|error| error.to_string())?;
    if ext.items.is_empty() || !ext.strip_height.is_finite() || ext.strip_height <= 0.0
        || ext.items.iter().enumerate().any(|(i, item)| item.base.id != i as u64 || item.demand == 0) {
        return Err("instance must contain positive demands, consecutive ids and a positive height".into());
    }
    let mut terminator = JobTerminator {
        global_deadline: started + Duration::from_secs_f64(request.time_limit_seconds),
        phase_deadline: None, cancel, user,
    };
    if terminator.externally_cancelled() {
        return Ok(json!({"protocol":1,"ok":false,"cancelled":true,"error":"cancelled before preparation"}));
    }
    let mut config = DEFAULT_SPARROW_CONFIG;
    config.expl_cfg.time_limit = Duration::from_secs_f64(request.time_limit_seconds * request.exploration_fraction);
    config.cmpr_cfg.time_limit = Duration::from_secs_f64(request.time_limit_seconds * (1.0 - request.exploration_fraction));
    config.expl_cfg.separator_config.n_workers = request.threads;
    config.cmpr_cfg.separator_config.n_workers = request.threads;
    config.poly_simpl_tolerance = positive_optional(request.polygon_simplification_ratio, "simplification")?;
    config.narrow_concavity_cutoff_ratio = positive_optional(request.narrow_concavity_ratio, "concavity")?;
    config.min_item_separation = if request.numerical_guard > 0.0 { Some(request.numerical_guard) } else { None };
    let importer = Importer::new(config.cde_config, config.poly_simpl_tolerance,
        config.min_item_separation, config.narrow_concavity_cutoff_ratio);
    let instance = jagua_rs::probs::spp::io::import(&importer, &ext).map_err(|error| error.to_string())?;
    let prepared_seconds = started.elapsed().as_secs_f64();
    let mut listener = JobListener { started, feasible_reports:0, infeasible_reports:0,
        first_feasible_seconds:None, first_solution:None, timeline:Vec::new() };
    let mut warm_import_seconds = None;
    let mut warm_work_width = None;
    let mut warm_repair = json!({"attempted":false});
    let mut warm_restore = Value::Null;
    let mut warm_initial_collisions = Value::Null;
    let mut warm_compression = Value::Null;
    let mut initial_solution_origin = "provided-incumbent";
    let solution = if warm {
        let problem = restore_warm_problem(&request, &ext, &instance)?;
        warm_work_width=Some(problem.strip_width());
        warm_restore=warm_restore_diagnostics(&request,&instance,&problem,started)?;
        let imported_feasible=problem.layout.is_feasible();
        let mut separator = Separator::new(instance.clone(), problem, SmallRng::seed_from_u64(request.seed), config.cmpr_cfg.separator_config);
        warm_initial_collisions=collision_diagnostics(&separator);
        warm_import_seconds = Some(started.elapsed().as_secs_f64());
        if !imported_feasible || separator.ct.get_total_loss()!=0.0 {
            let repair_started=Instant::now();
            let repair_budget=Duration::from_secs_f64((request.time_limit_seconds*0.1).min(15.0));
            terminator.new_timeout(repair_budget);
            let initial_loss=separator.ct.get_total_loss();
            // Separator reports improving/infeasible states only. In
            // particular this imported layout is never labelled ExplFeas.
            let (best,snapshot)=separator.separate(&terminator,&mut listener);
            separator.rollback(&best,Some(&snapshot));
            let accepted=separator.prob.layout.is_feasible() && separator.ct.get_total_loss()==0.0
                && separator.prob.item_demand_qtys.iter().all(|q|*q==0)
                && separator.prob.strip_width() as f64<=request.stock_width_cap.unwrap()
                && repair_started.elapsed()<=repair_budget;
            warm_repair=json!({"attempted":true,"accepted":accepted,"budget_seconds":repair_budget.as_secs_f64(),
                "elapsed_seconds":repair_started.elapsed().as_secs_f64(),"imported_layout_feasible":imported_feasible,
                "initial_loss":initial_loss,"final_loss":separator.ct.get_total_loss(),
                "final_collisions":collision_diagnostics(&separator),
                "original_stock_cap_unchanged":true,"imported_seed_reported_feasible":false});
            if !accepted {
                return Ok(json!({"protocol":1,"ok":false,"cancelled":terminator.externally_cancelled(),
                    "mode":"warm_compress","error":"warm-refine bounded f32 feasibility repair failed; original incumbent retained",
                    "initial_solution_origin":"provided-incumbent","warm_import_seconds":warm_import_seconds,
                    "warm_work_width":warm_work_width,"warm_feasibility_repair":warm_repair,
                    "warm_restore_diagnostics":warm_restore,"warm_initial_collisions":warm_initial_collisions}));
            }
            initial_solution_origin="provided-incumbent-after-internal-feasibility-repair";
        }
        let initial=separator.prob.save();
        let remaining=terminator.global_deadline.saturating_duration_since(Instant::now());
        config.cmpr_cfg.time_limit=remaining;
        terminator.new_timeout(config.cmpr_cfg.time_limit);
        if terminator.kill() { return Err("warm-refine budget/cancellation before compression; original incumbent retained".into()); }
        let solution = sparrow::optimizer::compress::compression_phase(&instance, &mut separator,
            &initial, &mut listener, &terminator, &config.cmpr_cfg);
        let mut final_problem=SPProblem::new(instance.clone());final_problem.restore(&solution);
        let final_tracker=CollisionTracker::new(&final_problem.layout);
        warm_compression=json!({"initial_internal_width":initial.strip_width(),"final_internal_width":solution.strip_width(),
            "final_loss":final_tracker.get_total_loss(),"final_layout_feasible":final_problem.layout.is_feasible(),
            "final_colliding_item_pairs":final_tracker.pair_collisions.data.iter().filter(|e|e.loss>0.0).count(),
            "final_colliding_container_items":final_tracker.container_collisions.iter().filter(|e|e.loss>0.0).count()});
        listener.report(ReportType::Final, &solution, &instance);
        solution
    } else {
        sparrow::optimizer::optimize(instance.clone(), SmallRng::seed_from_u64(request.seed),
            &mut listener, &mut terminator, &config.expl_cfg, &config.cmpr_cfg)
    };
    let exported = jagua_rs::probs::spp::io::export(&instance, &solution, started);
    let originals = request.instance["items"].as_array().ok_or("missing input items")?;
    let mut counts: HashMap<u64, usize> = HashMap::new();
    let mut placements = Vec::new();
    for item in exported.layout.placed_items {
        let id = item.item_id;
        let mut angle = (item.transformation.rotation as f64).to_degrees();
        // The original discrete choice is authoritative, not its f32 radians round trip.
        // Continuous orientations retain their full exported angle.
        if let Some(allowed) = originals[id as usize].get("allowed_orientations").and_then(Value::as_array) {
            let nearest = allowed.iter().filter_map(Value::as_f64)
                .map(|candidate| (candidate, ((angle - candidate + 180.0).rem_euclid(360.0) - 180.0).abs()))
                .min_by(|a, b| a.1.total_cmp(&b.1)).ok_or("empty discrete orientation domain")?;
            if nearest.1 > 0.001 { return Err("exported angle outside original discrete domain".into()); }
            angle = nearest.0;
        }
        let instance_id = counts.entry(id).or_default();
        placements.push(json!({"part_index":id,"instance":*instance_id,
            "x":item.transformation.translation.0 as f64,"y":item.transformation.translation.1 as f64,
            "angle_degrees":angle}));
        *instance_id += 1;
    }
    if ext.items.iter().any(|item| counts.get(&item.base.id).copied().unwrap_or(0) != item.demand as usize) {
        return Err("upstream output failed original demand conservation".into());
    }
    let mut response = json!({"protocol":1,"ok":true,"cancelled":terminator.externally_cancelled(),
        "placements":placements,"occupied_width":exported.strip_width as f64,
        "elapsed_seconds":started.elapsed().as_secs_f64(),"preparation_seconds":prepared_seconds,
        "feasible_reports":listener.feasible_reports,"infeasible_reports":listener.infeasible_reports,
        "first_upstream_feasible_seconds":listener.first_feasible_seconds,
        "initial_upstream_solution":listener.first_solution,"feasible_timeline":listener.timeline,
        "upstream_revision":UPSTREAM_REVISION,"jagua_revision":"a7bd8e9a85ceca57dbc61e4188a06f7e1facb461",
        "numerical_guard":request.numerical_guard,"search_geometry":"upstream f32; original material validation required",
        "timeout_scope":"global cooperative deadline polled between initial placement searches and separator worker rounds"});
    if warm {
        response["mode"] = json!("warm_compress");
        response["initial_solution_origin"] = json!(initial_solution_origin);
        response["warm_import_seconds"] = json!(warm_import_seconds);
        response["warm_work_width"] = json!(warm_work_width);
        response["warm_feasibility_repair"] = warm_repair;
        response["warm_restore_diagnostics"] = warm_restore;
        response["warm_initial_collisions"] = warm_initial_collisions;
        response["warm_compression"] = warm_compression;
        response["initial_occupied_width"] = json!(request.initial_occupied_width);
        response["stock_width_cap"] = json!(request.stock_width_cap);
    }
    Ok(response)
}

/// The native host must not throw across its callback. Returned text is owned
/// by this crate and must be released with sparrow_free_json, never C++ delete.
pub fn solve_json(text: &str, cancel: Option<CancelCallback>, user: *mut c_void) -> String {
    let result = catch_unwind(AssertUnwindSafe(|| solve(text, cancel, user)));
    let value = match result {
        Ok(Ok(value)) => value,
        Ok(Err(error)) => json!({"protocol":1,"ok":false,"cancelled":false,"error":error}),
        Err(_) => json!({"protocol":1,"ok":false,"cancelled":false,"error":"upstream panic contained at strategy boundary"}),
    };
    value.to_string()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sparrow_solve_json(request: *const c_char, cancel: Option<CancelCallback>, user: *mut c_void) -> *mut c_char {
    let output = if request.is_null() {
        json!({"protocol":1,"ok":false,"cancelled":false,"error":"null request"}).to_string()
    } else {
        match unsafe { CStr::from_ptr(request) }.to_str() {
            Ok(text) => solve_json(text, cancel, user),
            Err(_) => json!({"protocol":1,"ok":false,"cancelled":false,"error":"request is not UTF-8"}).to_string(),
        }
    };
    CString::new(output).expect("serialized JSON cannot contain a literal nul").into_raw()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sparrow_free_json(value: *mut c_char) {
    if !value.is_null() { drop(unsafe { CString::from_raw(value) }); }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request() -> Request {
        serde_json::from_value(json!({"protocol":1,"seed":17,"time_limit_seconds":0.05,"threads":1,
            "exploration_fraction":0.8,"polygon_simplification_ratio":0,"narrow_concavity_ratio":0,"numerical_guard":0,
            "mode":"warm_compress","initial_occupied_width":6,"stock_width_cap":10,
            "initial_layout":[{"part_index":0,"instance":0,"x":-8,"y":-18,"angle_degrees":0}],
            "instance":{"name":"warm-control","strip_height":10,"items":[{"id":0,"demand":1,"allowed_orientations":[0],
                "shape":{"type":"simple_polygon","data":[[10,20],[14,20],[14,22],[10,22]]}}]}})).unwrap()
    }
    fn restore(r: &Request) -> Result<SPProblem,String> {
        let ext: ExtSPInstance=serde_json::from_value(r.instance.clone()).unwrap();
        let importer=Importer::new(DEFAULT_SPARROW_CONFIG.cde_config,None,None,None);
        let instance=jagua_rs::probs::spp::io::import(&importer,&ext).unwrap();
        restore_warm_problem(r,&ext,&instance)
    }
    #[test]
    fn centered_original_pose_round_trips_without_double_translation() {
        let r=request();let p=restore(&r).unwrap();
        assert!(p.item_demand_qtys.iter().all(|q|*q==0));
        let exported=jagua_rs::probs::spp::io::export(&p.instance,&p.save(),Instant::now()-Duration::from_secs(1));
        let pose=&exported.layout.placed_items[0].transformation;
        assert_eq!(pose.translation,(-8.0,-18.0));assert_eq!(pose.rotation,0.0);
        assert_eq!(p.strip_width(),10.0);
        let mut turned=request();turned.instance["items"][0]["allowed_orientations"]=json!([90]);
        let pose=&mut turned.initial_layout.as_mut().unwrap()[0];pose.angle_degrees=90.;pose.x=25.;pose.y=-8.;
        let p=restore(&turned).unwrap();let d=warm_restore_diagnostics(&turned,&p.instance,&p,Instant::now()-Duration::from_secs(1)).unwrap();
        assert!(d["original_identity_order_verified"].as_bool().unwrap());
        assert!(d["maximum_absolute_x_delta"].as_f64().unwrap()<1e-4);
        assert!(d["maximum_absolute_y_delta"].as_f64().unwrap()<1e-4);
        assert!(d["maximum_absolute_angle_delta_degrees"].as_f64().unwrap()<1e-4);
    }
    #[test]
    fn original_inventory_domain_and_cap_are_required_before_restoration() {
        let mut r=request();r.initial_layout.as_mut().unwrap().push(InitialPlacement{part_index:0,instance:0,x:-8.,y:-18.,angle_degrees:0.});
        assert!(restore(&r).err().unwrap().contains("not complete"));
        let mut r=request();r.initial_layout.as_mut().unwrap()[0].instance=1;
        assert!(restore(&r).err().unwrap().contains("original identity"));
        let mut r=request();r.initial_layout.as_mut().unwrap()[0].angle_degrees=90.;
        assert!(restore(&r).err().unwrap().contains("finite domain"));
        let mut r=request();r.stock_width_cap=Some(5.);
        assert!(restore(&r).err().unwrap().contains("stock cap"));
    }
    #[test]
    fn lower_precision_infeasibility_is_not_hidden_and_original_cap_is_preserved() {
        let mut r=request();r.initial_layout.as_mut().unwrap()[0].y=-10.;
        let p=restore(&r).unwrap();assert!(!p.layout.is_feasible());assert_eq!(p.strip_width(),10.0);
        assert_eq!(r.initial_occupied_width,Some(6.));assert_eq!(r.stock_width_cap,Some(10.));
        let mut tight=request();tight.stock_width_cap=Some(6.);
        assert_eq!(restore(&tight).unwrap().strip_width(),6.0,"closed-contact f32 slack must not enlarge original cap");
    }
    #[test]
    fn warm_mode_is_explicit_and_cold_requests_cannot_smuggle_a_seed() {
        let r=request();
        // Start from a complete real request, then remove only its mode.
        let value=json!({"protocol":r.protocol,"instance":r.instance,"seed":r.seed,"time_limit_seconds":r.time_limit_seconds,
            "threads":r.threads,"exploration_fraction":r.exploration_fraction,"numerical_guard":0,
            "initial_layout":[],"initial_occupied_width":6,"stock_width_cap":10});
        let response:Value=serde_json::from_str(&solve_json(&value.to_string(),None,std::ptr::null_mut())).unwrap();
        assert_eq!(response["ok"],false);assert!(response["error"].as_str().unwrap().contains("explicit warm_compress"));
    }
}
