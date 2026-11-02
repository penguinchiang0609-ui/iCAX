import {capturePaneInteraction} from '../../_shared/workbench/utils/paneInteractionState.mjs';
import {blockingOperation} from './operationFeedback.mjs';

const activeDialogs = new WeakMap();
export function blockingProgress(view) {
  // Import owns the full lifecycle, including readback after shared pending clears.
  if(view.fiveAxisImportActive){
    const task=view.fiveAxisImportTask;
    return {title:task?.status==='Completed'?'正在显示模型…':view.fiveAxisImportCancelling?'正在取消导入…':'正在导入模型…',
      detail:String(view.sourcePath||'').split(/[\\/]/).at(-1),percent:null,
      cancel:task?.status==='Running'?'five.import.cancel':null,cancelLabel:'取消导入',
      cancelling:!!view.fiveAxisImportCancelling,cancellingTitle:'正在取消导入…'};
  }
  // The planning dock owns progress and real task cancellation throughout
  // preparation, readback and playback loading, independent of shared pending.
  if(blockingOperation(view)?.inline)return null;
  // Head calibration/catalog changes own an inline busy state. Keep their
  // live selector and the current pane interaction outside a modal focus trap.
  if(view.fiveSimulation?.busy&&view.fiveSimulation.request?.method==='Configure')return null;
  // ReadState clears the shared pending flag between recipe save and Generate.
  // The owning fixture request remains active until calculation and publication
  // have both finished, so that readback must not unlock the page.
  if ((!view.pending && !view.fixtureRequest) || !view.progress?.title) {
    const operation=blockingOperation(view);
    return operation&&(view.pending||view.fiveSimulation?.checking||view.fiveSimulation?.repairing)?{...operation,percent:null,cancel:operation.cancel?'five.operation.cancel':null}:null;
  }
  const task = view.fiveAxisFixtureTask;
  // Fixture generation currently has no measurable fraction. Do not invent one.
  const value = view.progress.percent;
  const percent = typeof value === 'number' && Number.isFinite(value) ? Math.max(0, Math.min(100, value)) : null;
  return {...view.progress, percent, cancel: task?.status === 'Running' ? 'five.fixture.cancel' : null};
}

export function installBlockingProgressStyle(document) {
  if (document.getElementById('five-blocking-progress-style')) return;
  const link = document.createElement('link');
  Object.assign(link, {id:'five-blocking-progress-style', rel:'stylesheet', href:new URL('./blockingProgress.css', import.meta.url).href});
  document.head.append(link);
}

function close(record, restore) {
  record.dialog.close();
  record.dialog.remove();
  record.document.defaultView.removeEventListener('keydown', record.keydown, true);
  record.document.defaultView.removeEventListener('keyup', record.keydown, true);
  activeDialogs.delete(record.document);
  if(record.pausedViewport===record.view.viewport&&!record.view.disposed&&!record.pausedViewport?.isDisposed)
    record.pausedViewport?.setContinuousRendering(true);
  if (restore && record.mount.isConnected && !record.view.disposed && record.context.isCurrentProject?.() !== false) record.restore();
}

export function disposeBlockingProgress(view, document) {
  const record = activeDialogs.get(document);
  if (record?.view === view) close(record, false);
}

// The dialog lives outside the patched workbench. showModal makes the whole
// document inert, including the shell ribbon, project tabs and other dialogs.
export function syncBlockingProgress(context, view, mount, ops, {beforeRender=false}={}) {
  const document = mount.ownerDocument;
  let record = activeDialogs.get(document);
  if (record && (record.view !== view || !record.mount.isConnected)) { close(record, false); record = null; }
  const progress = !view.disposed && context.isCurrentProject?.() !== false && blockingProgress(view);
  if (!progress) { if (record && !beforeRender) close(record, true); return; }
  if (!record) {
    installBlockingProgressStyle(document);
    const restore = capturePaneInteraction(mount, ['.cam-context-pane', '.cam-info-pane', '[data-five-dock]', '[data-five-overlay]']);
    const dialog = document.createElement('dialog');
    dialog.className = 'five-blocking-progress';
    dialog.setAttribute('aria-labelledby', 'five-blocking-progress-title');
    dialog.setAttribute('aria-describedby', 'five-blocking-progress-detail');
    dialog.setAttribute('aria-modal', 'true');
    dialog.setAttribute('aria-busy', 'true');
    dialog.tabIndex = -1;
    dialog.innerHTML = `<section class="five-blocking-progress-card">
      <header>
      <h2 id="five-blocking-progress-title" role="status" aria-live="polite"></h2>
      <button type="button" data-progress-cancel title="取消计算" aria-label="取消计算" hidden><svg viewBox="0 0 20 20" aria-hidden="true"><path d="m5 5 10 10M15 5 5 15"/></svg></button>
      </header>
      <p id="five-blocking-progress-detail"></p>
      <div class="five-blocking-progress-track" role="progressbar"><i></i></div>
      <footer data-progress-hint hidden></footer>
    </section>`;
    // The masked scene is static. Keep the progress animation responsive and
    // stop spending GPU/CPU time redrawing it during fixture geometry work.
    const pausedViewport=view.fiveFixtureDesignOpen&&view.viewport?.continuousRendering?view.viewport:null;
    pausedViewport?.setContinuousRendering(false);
    record = {document, dialog, view, context, mount, ops, restore, pausedViewport, cancelling:false};
    const current = record;
    current.keydown = event => {
      // Block application shortcuts, Escape and keyboard navigation behind the
      // dialog. Only its explicit cancel button remains keyboard-operable.
      const cancel = current.dialog.querySelector('[data-progress-cancel]');
      if (event.key === 'Tab') {
        event.preventDefault(); event.stopImmediatePropagation();
        (cancel.hidden || cancel.disabled ? current.dialog : cancel).focus({preventScroll:true});
      } else if (!['Enter', ' ', 'Spacebar'].includes(event.key) || event.target !== cancel || event.ctrlKey || event.metaKey || event.altKey) {
        event.preventDefault(); event.stopImmediatePropagation();
      } else event.stopPropagation();
    };
    dialog.addEventListener('cancel', event => event.preventDefault());
    dialog.querySelector('[data-progress-cancel]').addEventListener('click', async () => {
      if (current.cancelling || !current.cancel) return;
      current.cancelling = true;
      syncBlockingProgress(current.context, current.view, current.mount, current.ops);
      try { await current.context.handleAreaRibbonCommand(current.context, current.view, current.cancel, current.ops); }
      catch (error) {
        if (activeDialogs.get(document) !== current) return;
        current.cancelling = false;
        current.cancelError = '取消未成功，请重试或等待计算结束。';
        current.ops?.appendProjectLog?.(current.context, 'error', error.message);
        syncBlockingProgress(current.context, current.view, current.mount, current.ops);
      }
    });
    document.body.append(dialog);
    activeDialogs.set(document, record);
    document.defaultView.addEventListener('keydown', current.keydown, true);
    document.defaultView.addEventListener('keyup', current.keydown, true);
    dialog.showModal();
    dialog.focus({preventScroll:true});
  }
  Object.assign(record, {context, cancel:progress.cancel});
  if (ops) record.ops = ops;
  const {dialog} = record;
  const text = (selector, value) => {const node=dialog.querySelector(selector);if(node.textContent!==value)node.textContent=value;};
  text('h2', record.cancelling && progress.cancel ? progress.cancellingTitle||'正在取消计算…' : progress.title);
  const detail=progress.detail||progress.stage||'';text('p',detail);dialog.querySelector('p').hidden=!detail;
  const hint=record.cancelError||(record.cancelling?'正在停止…':'');text('[data-progress-hint]',hint);dialog.querySelector('[data-progress-hint]').hidden=!hint;
  const track=dialog.querySelector('[role="progressbar"]'), determinate=progress.percent!==null;
  track.setAttribute('aria-label', progress.title);
  track.classList.toggle('is-determinate', determinate);
  if (determinate) {track.setAttribute('aria-valuenow',progress.percent);track.setAttribute('aria-valuemin','0');track.setAttribute('aria-valuemax','100');track.firstElementChild.style.width=progress.percent+'%';}
  else {for(const name of ['aria-valuenow','aria-valuemin','aria-valuemax'])track.removeAttribute(name);track.firstElementChild.style.removeProperty('width');}
  const cancel=dialog.querySelector('[data-progress-cancel]');cancel.hidden=!progress.cancel;cancel.disabled=record.cancelling||!!progress.cancelling;
  cancel.title=cancel.disabled?'正在取消…':progress.cancelLabel||'取消计算';cancel.setAttribute('aria-label',cancel.title);
}
