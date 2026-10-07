// Catalogues belong to the connected product, not to an opened project.
// Store only resource data here; scene snapshots, selections and editor drafts
// remain in each project view. Every consumer gets its own copy.
const productSessions = new WeakMap();
const viewBindings = new WeakMap();
const resourceKeys = {
  userData: ["tubeDesignerUserData", "tubeDesignerSystemProfiles", "tubeDesignerTemplateProfiles"],
  tools: ["tubeDesignerSystemPunchTools", "tubeDesignerTemplatePunchTools"],
};
const copy = (value) => structuredClone(value);

export function productResourceSession(context, view) {
  const proxy = context?.productProxy;
  if (!proxy || typeof proxy.invoke !== "function") return null;
  const channel = String(proxy.productChannelId ?? "");
  let session = productSessions.get(proxy);
  if (!session || session.channel !== channel) {
    session = { channel, revision: 0, resources: {}, descriptors: new Map(), descriptorInvalidations: new Map(), descriptorRequests: {}, preloadPromise: null, userDataPromise: null, toolsPromise: null };
    productSessions.set(proxy, session);
  }
  if (view && viewBindings.get(view)?.session !== session) {
    if (viewBindings.has(view)) {
      for (const keys of Object.values(resourceKeys)) for (const key of keys) delete view[key];
      view.tubeDesignerUserDataLoaded = false;
      view.tubeDesignerUserDataLoading = false;
      view.tubeDesignerUserDataRefreshPromise = null;
      view.tubeDesignerUserDataSynchronizationPromise = null;
      view.tubeDesignerLoaded = false;
      view.tubeDesignerLoading = false;
      view.tubeDesignerSynchronizationPromise = null;
      view.tubeDesignerTemplateDescriptors = {};
      if (view.tubeDesignerToolLibrary) {
        view.tubeDesignerToolLibrary.catalogueStatus = "idle";
        view.tubeDesignerToolLibrary.cataloguePromise = null;
        view.tubeDesignerToolLibrary.catalogueRequestToken = (view.tubeDesignerToolLibrary.catalogueRequestToken ?? 0) + 1;
      }
    }
    viewBindings.set(view, { session, revisions: {}, descriptors: new Map(), descriptorInvalidations: new Map() });
  }
  return session;
}

export function isProductResourceSessionCurrent(context, view, session) {
  if (!session) return true;
  return productSessions.get(context.productProxy) === session
    && session.channel === String(context.productProxy.productChannelId ?? "")
    && viewBindings.get(view)?.session === session;
}

export function restoreProductResources(context, view) {
  const session = productResourceSession(context, view);
  if (!session) return null;
  const binding = viewBindings.get(view);
  for (const [kind, keys] of Object.entries(resourceKeys)) {
    const cached = session.resources[kind];
    if (!cached || binding.revisions[kind] === cached.revision) continue;
    for (const key of keys) view[key] = copy(cached.data[key]);
    binding.revisions[kind] = cached.revision;
    if (kind === "userData") {
      view.tubeDesignerUserDataLoaded = true;
      view.tubeDesignerUserDataError = "";
      view.tubeDesignerSystemProfilesError = "";
    } else {
      view.tubeDesignerToolLibrary ??= {};
      view.tubeDesignerToolLibrary.catalogueStatus = "ready";
      view.tubeDesignerToolLibrary.error = "";
    }
  }
  const registry = view.tubeDesignerTemplateDescriptors ??= {};
  for (const [id, revision] of session.descriptorInvalidations) {
    if (binding.descriptorInvalidations.get(id) === revision) continue;
    delete registry[id];
    binding.descriptors.delete(id);
    binding.descriptorInvalidations.set(id, revision);
    const catalogue = view.scene?.tubeDesigner?.templates?.find((item) => String(item?.id ?? "") === id);
    if (catalogue) {
      delete catalogue.parameters;
      catalogue.descriptorLoaded = false;
    }
  }
  for (const [id, cached] of session.descriptors) {
    if (binding.descriptors.get(id) === cached) continue;
    registry[id] = copy(cached);
    binding.descriptors.set(id, cached);
  }
  return session;
}

// A save may finish after switching projects. Merge its changed record into
// the latest product catalogue instead of publishing that view's older copy.
export function restoreProductUserData(view) {
  const binding = viewBindings.get(view);
  const cached = binding?.session.resources.userData;
  if (!cached || binding.revisions.userData === cached.revision) return;
  for (const key of resourceKeys.userData) view[key] = copy(cached.data[key]);
  binding.revisions.userData = cached.revision;
}

export function rememberProductResources(view, kind, expectedSession = viewBindings.get(view)?.session) {
  const binding = viewBindings.get(view);
  const keys = resourceKeys[kind];
  if (!binding || binding.session !== expectedSession || !keys || keys.some((key) => view[key] === undefined)) return;
  if (kind === "userData" && (view.tubeDesignerUserDataError
      || !Array.isArray(view.tubeDesignerSystemProfiles)
      || !Array.isArray(view.tubeDesignerTemplateProfiles))) return;
  const revision = ++binding.session.revision;
  binding.session.resources[kind] = {
    revision, data: Object.fromEntries(keys.map((key) => [key, copy(view[key])])),
  };
  binding.revisions[kind] = revision;
  if (kind === "userData") view.tubeDesignerUserDataLoaded = true;
}

export function rememberProductTemplateDescriptor(view, descriptor, expectedSession = viewBindings.get(view)?.session) {
  const binding = viewBindings.get(view);
  const id = String(descriptor?.id ?? "").trim();
  if (!binding || binding.session !== expectedSession || !id || !Array.isArray(descriptor?.parameters)) return;
  const cached = copy(descriptor);
  binding.session.descriptors.set(id, cached);
  binding.descriptors.set(id, cached);
}

export function forgetProductTemplateDescriptor(view, templateId) {
  const binding = viewBindings.get(view);
  const id = String(templateId ?? "");
  binding?.session.descriptors.delete(id);
  binding?.descriptors.delete(id);
  if (binding) binding.session.descriptorInvalidations.set(id, ++binding.session.revision);
  delete (view.tubeDesignerTemplateDescriptors ?? {})[id];
}

export function invalidateProductResources(context, view, kind) {
  const session = productResourceSession(context, view);
  if (session) delete session.resources[kind];
}
