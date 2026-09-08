// Canonical vector artwork for the TubeDesigner hollow-section identity.
const tube = `
  <path fill="#089d9d" d="M48 103 143 48Q149 44 156 47L211 66Q220 69 220 80V160Q220 170 212 175L133 221 133 145Z"/>
  <path fill="#10bdb5" d="m48 103 95-55q6-4 13-1l55 19q7 3 8 9l-94 55Z"/>
  <path fill="#072b43" d="m103 120 96-57 9 3-96 58Z"/>
  <path fill="#072b43" d="m133 170 87-51v7l-87 51Z"/>
  <path fill="#fff" d="M48 99 122 124Q137 129 137 143V207Q137 224 121 219L47 194Q36 190 36 177V113Q36 95 48 99Z"/>
  <path fill="#072b43" d="m54 116 64 22q4 1 4 6v57q0 3-3 2l-64-22q-4-1-4-6v-55q0-5 3-4Z"/>
  <path fill="#0cafaa" d="m54 178 37-22 31 11v34q0 3-3 2l-64-22Z"/>`;

export function tubeDesignerSvg({ tile = true } = {}) {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" role="img" aria-label="TubeDesigner 管材设计">${tile ? '<rect width="256" height="256" rx="52" fill="#06233d"/>' : ''}${tube}</svg>`;
}

export const tubeDesignerIconUrl = new URL('./assets/tube-designer.ico', import.meta.url).href;
export const tubeDesignerSvgUrl = new URL('./assets/tube-designer.svg', import.meta.url).href;

export function installTubeDesignerBranding() {
  if (typeof document === 'undefined') return;
  let icon = document.head.querySelector('link[data-tube-designer-icon]');
  if (!icon) {
    icon = document.createElement('link');
    icon.rel = 'icon';
    icon.type = 'image/x-icon';
    icon.dataset.tubeDesignerIcon = '';
    document.head.append(icon);
  }
  icon.href = tubeDesignerIconUrl;
  if (!document.head.querySelector('style[data-tube-designer-brand]')) {
    const style = document.createElement('style');
    style.dataset.tubeDesignerBrand = '';
    style.textContent = `body:has(.tube-designer-workspace) .title-bar .app-button { display:inline-flex; align-items:center; gap:6px; }
      body:has(.tube-designer-workspace) .title-bar .app-button::before { content:""; flex:0 0 22px; width:22px; height:22px; background:url("${tubeDesignerSvgUrl}") center/contain no-repeat; }`;
    document.head.append(style);
  }
}
