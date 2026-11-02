const startupScreenId = 'application-startup-screen';
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
  return `<section id="${startupScreenId}" class="td-startup" aria-label="工作台 启动" data-startup-screen data-startup-state="starting">
    <div class="td-startup__ambient" aria-hidden="true"></div>
    <div class="td-startup__content">
      <div class="td-startup__illustration" aria-hidden="true">
        <svg class="td-startup__drawing" viewBox="0 0 420 360" fill="none" xmlns="http://www.w3.org/2000/svg">
          <g stroke="#83dfc3" stroke-width="2" stroke-linejoin="round">
            <path d="M90 115 210 50 330 115 330 245 210 310 90 245Z" fill="#1c514b"/>
            <path d="M90 115 210 180 330 115M210 180V310M150 83 270 148M150 148 270 83"/>
            <path d="M90 180 210 245 330 180M150 148V278M270 148V278" opacity=".4"/>
          </g>
        </svg>
      </div>
      <div class="td-startup__identity">
        <div class="td-startup__eyebrow"><span></span>设计与加工</div>
        <h1 class="td-startup__brand">工作台<span class="td-startup__brand-point" aria-hidden="true"></span></h1>
        <p class="td-startup__description">正在连接应用</p>
        <div class="td-startup__status">
          <div class="td-startup__activity" aria-hidden="true"><span></span><span></span><span></span></div>
          <div class="td-startup__status-copy">
            <p class="td-startup__stage" role="status" aria-live="polite" data-startup-status>正在启动 工作台</p>
            <p class="td-startup__stage-detail" data-startup-detail hidden></p>
          </div>
        </div>
        <p class="td-startup__error" role="alert" hidden></p>
        <button class="td-startup__retry" type="button" data-startup-retry hidden>重新启动</button>
      </div>
    </div>
    <div class="td-startup__footer" aria-hidden="true"><span class="td-startup__footer-line"></span><span>工作台</span><span class="td-startup__footer-line"></span></div>
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
  let productName = '工作台';

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
    stage.textContent = `正在重新启动 ${productName}`;
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
    setPresentation(name, presentation = {}) {
      if (finished) return;
      productName = String(name || '工作台');
      element.setAttribute('aria-label', productName + ' 启动');
      const brand = element.querySelector('.td-startup__brand');
      brand.textContent = productName;
      if (Array.isArray(presentation.wordmark) && presentation.wordmark.join('') === productName) {
        brand.textContent = String(presentation.wordmark[0] || '');
        const suffix = document.createElement('span');
        suffix.textContent = presentation.wordmark.slice(1).join('');
        brand.append(suffix);
      }
      element.querySelector('.td-startup__eyebrow').textContent = presentation.eyebrow || '设计与加工';
      element.querySelector('.td-startup__description').textContent = presentation.description || '正在准备产品与工程';
      element.querySelector('.td-startup__footer > span:nth-child(2)').textContent = productName;
      // This markup comes from the installed product module, not user input.
      if (presentation.illustration) element.querySelector('.td-startup__illustration').innerHTML = presentation.illustration;
    },
    setStage(text, detailText = '') {
      if (finished) return;
      stage.textContent = String(text || `正在启动 ${productName}`);
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
      error.textContent = String(message || `请重新启动 ${productName}。`);
      error.hidden = false;
      onRetry = typeof retryCallback === 'function' ? retryCallback : null;
      retry.hidden = !onRetry;
    },
  };

  retry.addEventListener('click', handleRetry);
  screenControllers.set(element, controller);
  return controller;
}
