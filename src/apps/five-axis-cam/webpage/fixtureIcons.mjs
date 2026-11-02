const paths={
  check:'<path d="m5 12 4 4L19 6"/>',
  select:'<path d="m5 3 14 10-7 1-3 7z"/>',
  rectangle:'<rect x="4" y="5" width="16" height="14" rx="1"/>',
  circle:'<circle cx="12" cy="12" r="8"/><path d="M10 12h4m-2-2v4"/>',
  polyline:'<path d="m5 6 12-2 4 12-11 5-7-8z"/><path d="M5 4v4m-2-2h4m8-2h4m-2-2v4"/>',
  undo:'<path d="m8 4-5 5 5 5M3 9h10a7 7 0 0 1 7 7v4"/>',
  redo:'<path d="m16 4 5 5-5 5m5-5H11a7 7 0 0 0-7 7v4"/>',
  fit:'<path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5"/><rect x="7" y="7" width="10" height="10" rx="1"/>',
  edit:'<path d="m14 5 5 5M4 20l5-1L21 7a2 2 0 0 0-5-5L4 14z"/>',
  power:'<path d="M12 3v9m-5-7a9 9 0 1 0 10 0"/>',
  close:'<path d="m6 6 12 12M18 6 6 18"/>',
  delete:'<path d="M4 6h16M9 6V3h6v3M6 6l1 15h10l1-15M10 10v7m4-7v7"/>',
};
export const fixtureIcon=name=>`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${paths[name]||paths.edit}</svg>`;
