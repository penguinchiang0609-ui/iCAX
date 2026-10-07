export function makeLibraryNavigationFixture(assemblyTemplates) {
  const product = (id, name) => ({ id, name, available: true, parameters: [], version: "1.0.0" });
  const profile = (id, name) => ({ id, name, profileForm: "parametric", category: "矩形管", sectionType: "rectangular",
    snapshot: { sectionType: "rectangular", width: 30, depth: 20, wallThickness: 2 } });
  const tool = (id, displayName) => ({ id, displayName, kind: "fixed", category: "槽口", parameters: [] });
  const component = (id, name, scope) => ({ id, name, scope, category: "连接", sourcing: "purchased", bounds: { width: 10, depth: 10, height: 10 } });
  return {
    scene: { tubeDesigner: { templates: [product("system-a", "System A"), product("system-b", "System B")] } },
    tubeDesignerUserDataLoading: false,
    tubeDesignerUserData: {
      productTemplates: [product("user-a", "验收产品 A"), product("user-b", "验收产品 B")],
      profiles: [profile("user-a", "验收管型 A"), profile("user-b", "验收管型 B")],
      punchTools: [tool("user-a", "验收工艺 A"), tool("user-b", "验收工艺 B")].map(item => ({ ...item, libraryScope: "user" })),
    },
    tubeDesignerSystemProfiles: [profile("system-a", "System A"), profile("system-b", "System B")],
    tubeDesignerSystemPunchTools: [tool("system-a", "System A"), tool("system-b", "System B")],
    tubeDesignerToolLibrary: { catalogueStatus: "ready" },
    tubeDesignerAssemblyTemplates: structuredClone(assemblyTemplates).map((item, index) => ({ ...item, displayName: `验收装配 ${index ? "B" : "A"}` })),
    tubeDesignerAssemblyLibrary: { catalogueStatus: "ready" },
    tubeDesignerComponentLibrary: { loadState: "loaded", models: [component("system-a", "System A", "system"),
      component("user-a", "验收配件 A", "user"), component("user-b", "验收配件 B", "user")] },
  };
}
