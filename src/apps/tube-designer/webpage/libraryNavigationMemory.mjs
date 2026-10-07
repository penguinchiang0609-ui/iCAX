import { createWindowStateMemory } from "../../../iCAX-UI/SDK/Forms/windowStateMemory.mjs";
import { productTemplateLibraryState } from "./templateLibrary.mjs";
import { profileLibraryState } from "./profileLibrary.mjs";
import { toolLibraryState } from "./toolLibrary.mjs";
import { assemblyLibraryState } from "./assemblyLibrary.mjs";
import { componentLibraryState } from "./componentLibrary.mjs";

const namespace = "icax.tube-designer.library-navigation";
const sessions = new WeakMap();
let memory;
const areas = [
  { key: "templates", state: productTemplateLibraryState, filters: ["scope", "search", "collapsed"],
    selection: "selectedId", ready: (view, state) => Array.isArray(view.scene?.tubeDesigner?.templates)
      && (state.scope !== "user" || (!view.tubeDesignerUserDataLoading && Array.isArray(view.tubeDesignerUserData?.productTemplates))) },
  { key: "profiles", state: profileLibraryState, filters: ["scope", "search", "type", "collapsed"],
    selection: "selectedKey", externalSelection: "tubeDesignerSelectedProfileId", byScope: true,
    ready: view => !view.tubeDesignerUserDataLoading
      && !!(view.tubeDesignerSystemProfiles?.length || view.tubeDesignerUserData?.profiles?.length) },
  { key: "tools", state: toolLibraryState, filters: ["scope", "search", "type", "category", "collapsed"],
    selection: "selectedKey", ready: (_view, state) => state.catalogueStatus === "ready" },
  { key: "assemblies", state: assemblyLibraryState, filters: ["search", "collapsed"],
    selection: "selectedId", ready: (_view, state) => state.catalogueStatus === "ready" },
  { key: "components", state: componentLibraryState, filters: ["scope", "search", "collapsed"],
    selection: "selectedKey", byScope: true, ready: (_view, state) => state.loadState === "loaded" },
];

function store() { return memory ??= createWindowStateMemory({ namespace }); }
function own(value, field) { return value && Object.hasOwn(value, field); }
function strings(value) { return Array.isArray(value) && value.every(item => typeof item === "string"); }
function scopeSelections(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) return undefined;
  return Object.fromEntries(["system", "user", "template"].filter(scope => typeof value[scope] === "string")
    .map(scope => [scope, value[scope]]));
}
function navigationFields(area, source) {
  const fields = {};
  for (const field of [...area.filters, area.selection]) {
    if (field === "collapsed" ? strings(source?.[field]) : typeof source?.[field] === "string")
      fields[field] = field === "collapsed" ? [...source[field]] : source[field];
  }
  if (area.byScope) {
    const selections = scopeSelections(source?.selectedByScope);
    if (selections) fields.selectedByScope = selections;
  }
  return fields;
}
function getSession(view) {
  let session = sessions.get(view);
  if (!session) { session = new Map(); sessions.set(view, session); }
  return session;
}
function filterIdentity(area, state) {
  return JSON.stringify(area.filters.filter(field => field !== "collapsed").map(field => state[field]));
}
function currentSelection(area, view, state) {
  return area.externalSelection ? view[area.externalSelection] : state[area.selection];
}
function setSelection(area, view, state, value) {
  if (area.externalSelection) view[area.externalSelection] = value;
  else state[area.selection] = value;
}

/** Restore navigation once per view; resource selection waits for its catalogue. */
export function restoreLibraryNavigation(view) {
  if (!view || typeof view !== "object") return;
  const session = getSession(view);
  for (const area of areas) {
    const state = area.state(view);
    let record = session.get(area.key);
    if (!record) {
      const saved = navigationFields(area, store().readFields(area.key));
      for (const field of area.filters) if (own(saved, field)) state[field] = saved[field];
      // The actual state factory remains responsible for valid filter choices.
      area.state(view);
      record = { saved, filters: filterIdentity(area, state), selectionRestored: false, selectionCancelled: false };
      session.set(area.key, record);
    } else if (!record.selectionRestored && filterIdentity(area, state) !== record.filters) {
      // A newer user filter must win over a selection waiting on an async load.
      record.selectionCancelled = true;
    }
    if (!record.selectionRestored && area.ready(view, state)) {
      if (!record.selectionCancelled) {
        if (own(record.saved, area.selection)) setSelection(area, view, state, record.saved[area.selection]);
        if (area.byScope && own(record.saved, "selectedByScope")) state.selectedByScope = record.saved.selectedByScope;
      }
      record.selectionRestored = true;
      // Templates and assemblies validate here; other libraries validate in their renderer.
      area.state(view);
    }
  }
}

/** Persist navigation only. Draft parameters, catalogue records and geometry stay in the view. */
export function rememberLibraryNavigation(view) {
  if (!view || typeof view !== "object") return;
  const session = sessions.get(view);
  if (!session) return;
  for (const area of areas) {
    const record = session.get(area.key);
    if (!record) continue;
    const state = area.state(view);
    if (!record.selectionRestored && filterIdentity(area, state) !== record.filters)
      record.selectionCancelled = true;
    const fields = navigationFields(area, state);
    const selection = currentSelection(area, view, state);
    if (typeof selection === "string") fields[area.selection] = selection;
    // Loading renders often clear the default card. Preserve the previous disk
    // selection until restoration has run with a complete resource catalogue.
    if (!record.selectionRestored || !area.ready(view, state)) {
      delete fields[area.selection];
      delete fields.selectedByScope;
    }
    store().writeFields(area.key, fields);
  }
}
