const startupScreenId = 'tube-designer-startup-screen';
const screenControllers = new WeakMap();

function opacityTransitionMilliseconds(element, view) {
  const style = view.getComputedStyle(element);
  const properties = style.transitionProperty.split(',').map(value => value.trim());
  const durations = style.transitionDuration.split(',');
  const delays = style.transitionDelay.split(',');
  const milliseconds = value => {
    const text = value.trim();
    const number = Number.parseFloat(text);
    return Number.isFinite(number) ? number * (text.endsWith('ms') ? 1 : 1000) : 0;
  };
  return properties.reduce((maximum, property, index) => {
    if (property !== 'opacity' && property !== 'all') return maximum;
    return Math.max(maximum, milliseconds(durations[index % durations.length]) + milliseconds(delays[index % delays.length]));
  }, 0);
}

export function renderStartupScreenMarkup() {
  return `<section id="${startupScreenId}" class="td-startup" aria-label="TubeDesigner 启动" data-startup-screen data-startup-state="starting">
    <div class="td-startup__ambient" aria-hidden="true"></div>
    <div class="td-startup__content">
      <div class="td-startup__illustration" aria-hidden="true">
        <svg class="td-startup__drawing" viewBox="0 0 420 360" fill="none" xmlns="http://www.w3.org/2000/svg">
          <defs>
            <linearGradient id="td-startup-face" x1="100" y1="80" x2="305" y2="292" gradientUnits="userSpaceOnUse">
              <stop stop-color="#9AEED7"/><stop offset=".5" stop-color="#4FC1AC"/><stop offset="1" stop-color="#208C80"/>
            </linearGradient>
            <linearGradient id="td-startup-side" x1="120" y1="82" x2="327" y2="300" gradientUnits="userSpaceOnUse">
              <stop stop-color="#3A9B91"/><stop offset="1" stop-color="#155B59"/>
            </linearGradient>
            <linearGradient id="td-startup-light" x1="0" y1="0" x2="1" y2="0">
              <stop stop-color="#62D6BE" stop-opacity="0"/><stop offset=".5" stop-color="#A7F9DE"/><stop offset="1" stop-color="#62D6BE" stop-opacity="0"/>
            </linearGradient>
            <linearGradient id="td-startup-scan" x1="0" y1="0" x2="0" y2="1">
              <stop stop-color="#80E7C9" stop-opacity="0"/><stop offset="1" stop-color="#80E7C9" stop-opacity=".11"/>
            </linearGradient>
          </defs>
          <g class="td-startup__guides" stroke="#4C8F89" stroke-width="1">
            <path d="M52 311 330 236M83 328 362 253M115 344 390 270M68 283 167 334M106 273 205 324M144 263 243 314M182 253 281 304M220 243 319 294M258 233 357 284" opacity=".2"/>
            <path d="M82 119 82 311M72 122 92 116M72 314 92 308M96 90 289 38M94 83 98 97M287 31 291 45" opacity=".42"/>
            <path d="M112 111 286 64 286 246 112 293Z" stroke-dasharray="4 7" opacity=".5"/>
            <path d="M128 125 270 87 270 232 128 270Z" stroke-dasharray="3 6" opacity=".32"/>
            <path d="M112 97V123M99 111H125M286 50V76M273 64H299M286 233V259M273 246H299M112 280V306M99 293H125" opacity=".32"/>
          </g>
          <ellipse class="td-startup__shadow" cx="218" cy="309" rx="114" ry="14" fill="#031819" opacity=".7"/>
          <g class="td-startup__tube td-startup__tube--left">
            <path d="M112 111 136 124 136 306 112 293Z" fill="url(#td-startup-side)"/>
            <path d="M128 125 152 138 152 283 128 270Z" fill="#0F4E4D"/>
            <path d="M112 111 128 125 128 270 112 293Z" fill="url(#td-startup-face)"/>
            <path d="M112 111 112 293M128 125 128 270" stroke="#9AEDD5" stroke-opacity=".6"/>
            <path d="M112 293 136 306 152 283 128 270Z" fill="#276C65"/>
            <path d="M119 291 137 300 145 285 128 277Z" fill="#0A3838" stroke="#84CFBC" stroke-opacity=".35"/>
          </g>
          <g class="td-startup__tube td-startup__tube--bottom">
            <path d="M112 293 286 246 310 259 136 306Z" fill="url(#td-startup-side)"/>
            <path d="M128 270 270 232 294 245 152 283Z" fill="#559B88"/>
            <path d="M112 293 128 270 270 232 286 246Z" fill="url(#td-startup-face)"/>
            <path d="M112 293 286 246M128 270 270 232" stroke="#9AEDD5" stroke-opacity=".55"/>
          </g>
          <g class="td-startup__tube td-startup__tube--right">
            <path d="M286 64 310 77 310 259 286 246Z" fill="url(#td-startup-side)"/>
            <path d="M270 87 294 100 294 245 270 232Z" fill="#0F4E4D"/>
            <path d="M286 64 286 246 270 232 270 87Z" fill="url(#td-startup-face)"/>
            <path d="M286 64 286 246M270 87 270 232" stroke="#9AEDD5" stroke-opacity=".65"/>
          </g>
          <g class="td-startup__tube td-startup__tube--top">
            <path d="M112 111 286 64 310 77 136 124Z" fill="#74CDB5"/>
            <path d="M128 125 270 87 294 100 152 138Z" fill="#0C4846"/>
            <path d="M112 111 286 64 270 87 128 125Z" fill="url(#td-startup-face)"/>
            <path d="M112 111 286 64M128 125 270 87M286 64 310 77" stroke="#B6F4DE" stroke-opacity=".8"/>
          </g>
          <g class="td-startup__finish-lines" stroke="#9CF2D4" stroke-width="1.2" stroke-linecap="round">
            <path pathLength="1" d="M112 293V111L286 64V246L112 293"/>
            <path pathLength="1" d="M128 270V125L270 87V232L128 270" opacity=".46"/>
          </g>
          <g class="td-startup__scan">
            <path d="M88 125 323 61V103L88 166Z" fill="url(#td-startup-scan)"/>
            <path d="M88 166 323 103" stroke="url(#td-startup-light)" stroke-width="1.6"/>
          </g>
          <g class="td-startup__detail" stroke="#83C7B9" stroke-width="1" opacity=".5">
            <path d="M337 154H362M349 142V166M49 241H61M55 235V247"/>
            <circle cx="349" cy="154" r="15" opacity=".32"/>
            <path d="M323 121H350L366 107M76 189H51L39 201" opacity=".38"/>
          </g>
        </svg>
      </div>
      <div class="td-startup__identity">
        <div class="td-startup__eyebrow"><span></span>管材设计与加工</div>
        <h1 class="td-startup__brand">Tube<span>Designer</span><span class="td-startup__brand-point" aria-hidden="true"></span></h1>
        <p class="td-startup__description">设计<span>·</span>拆单<span>·</span>排样<span>·</span>加工<span>·</span>仿真</p>
        <div class="td-startup__status">
          <div class="td-startup__activity" aria-hidden="true"><span></span><span></span><span></span></div>
          <div class="td-startup__status-copy">
            <p class="td-startup__stage" role="status" aria-live="polite" data-startup-status>正在启动 TubeDesigner</p>
            <p class="td-startup__stage-detail" data-startup-detail hidden></p>
          </div>
        </div>
        <p class="td-startup__error" role="alert" hidden></p>
        <button class="td-startup__retry" type="button" data-startup-retry hidden>重新启动</button>
      </div>
    </div>
    <div class="td-startup__footer" aria-hidden="true"><span class="td-startup__footer-line"></span><span>TubeDesigner</span><span class="td-startup__footer-line"></span></div>
  </section>`;
}

export function createStartupScreen(document) {
  let element = document.getElementById(startupScreenId);
  if (!element) {
    const template = document.createElement('template');
    template.innerHTML = renderStartupScreenMarkup();
    element = template.content.firstElementChild;
    document.body.append(element);
  }
  const existingController = screenControllers.get(element);
  if (existingController) return existingController;

  const application = document.getElementById('app');
  application?.setAttribute('inert', '');
  application?.setAttribute('aria-hidden', 'true');
  const stage = element.querySelector('.td-startup__stage');
  const detail = element.querySelector('[data-startup-detail]');
  const error = element.querySelector('.td-startup__error');
  const retry = element.querySelector('.td-startup__retry');
  let onRetry = null;
  let finished = false;
  let removed = false;
  let removalTimer = null;

  function remove() {
    if (removed) return;
    removed = true;
    if (removalTimer !== null) document.defaultView.clearTimeout(removalTimer);
    element.removeEventListener('transitionend', onFadeEnd);
    element.removeEventListener('transitioncancel', onFadeEnd);
    document.removeEventListener('visibilitychange', onVisibilityChange);
    document.defaultView.removeEventListener('pagehide', onPageHide);
    retry.removeEventListener('click', handleRetry);
    element.remove();
  }

  function onFadeEnd(event) {
    if (finished && event.target === element && event.propertyName === 'opacity') remove();
  }

  function onVisibilityChange() {
    if (finished && document.hidden) remove();
  }

  function onPageHide() {
    if (finished) remove();
  }

  async function handleRetry() {
    if (finished || !onRetry || retry.disabled) return;
    const callback = onRetry;
    retry.disabled = true;
    error.hidden = true;
    retry.hidden = true;
    detail.textContent = '';
    detail.hidden = true;
    element.dataset.startupState = 'starting';
    stage.textContent = '正在重新启动 TubeDesigner';
    try {
      await callback();
    } catch (reason) {
      controller.fail(reason?.message || '启动未完成，请重试。', callback);
    } finally {
      retry.disabled = false;
    }
  }

  const controller = {
    element,
    setStage(text, detailText = '') {
      if (finished) return;
      stage.textContent = String(text || '正在启动 TubeDesigner');
      detail.textContent = String(detailText || '');
      detail.hidden = !detail.textContent.trim();
    },
    finish() {
      if (finished) return;
      finished = true;
      application?.removeAttribute('inert');
      application?.removeAttribute('aria-hidden');
      if (document.hidden || document.defaultView.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        remove();
        return;
      }
      const fadeMilliseconds = opacityTransitionMilliseconds(element, document.defaultView);
      element.addEventListener('transitionend', onFadeEnd);
      element.addEventListener('transitioncancel', onFadeEnd);
      document.addEventListener('visibilitychange', onVisibilityChange);
      document.defaultView.addEventListener('pagehide', onPageHide);
      element.setAttribute('aria-hidden', 'true');
      element.dataset.startupState = 'ready';
      if (!element.isConnected || fadeMilliseconds <= 0) {
        remove();
        return;
      }
      removalTimer = document.defaultView.setTimeout(remove, fadeMilliseconds + 64);
    },
    fail(message, retryCallback) {
      if (finished) return;
      element.dataset.startupState = 'error';
      stage.textContent = '启动未完成';
      detail.textContent = '';
      detail.hidden = true;
      error.textContent = String(message || '请重新启动 TubeDesigner。');
      error.hidden = false;
      onRetry = typeof retryCallback === 'function' ? retryCallback : null;
      retry.hidden = !onRetry;
    },
  };

  retry.addEventListener('click', handleRetry);
  screenControllers.set(element, controller);
  return controller;
}
