# Window state memory

`createWindowStateMemory({ namespace, storage? })` stores user defaults for each
semantic window and field. Its API is `read(windowKey, fieldKey, defaultValue)`,
`write(windowKey, fieldKey, value)`, `readFields(windowKey)`,
`writeFields(windowKey, fields)`, and `clear(windowKey)`. Values retain JSON types
and are copied on read and write. Use a stable application namespace. The default
storage is localStorage, which persists across application restarts on the same
origin. If storage is unavailable, edits remain in this controller's memory.
Records are bounded to 128 windows, 256 fields per window and 512 KiB total;
malformed records and unsupported values are ignored. This is a current schema,
with no migration or historical field mapping.

`installWindowStateMemory(document, options)` installs delegated input/change
capture and opening restoration. It returns `{ memory, refresh, restoreWindow,
captureWindow, dispose }`. The default window selector is
`dialog,[role="dialog"],.new-project-dialog`; an application can include owned
editor panes in `dialogSelector`.

Remembered controls are an explicit per-window policy. Declare
`data-window-state-controls="CSS selector"` on the window or return
`{ controlSelector: 'CSS selector' }` from `describeWindow`. A descriptor's
`controlSelector: false` disables memory for all of its controls. Without a
window declaration, only controls explicitly marked `data-window-state-field`
or `data-window-state-key` participate; IDs, names or model bindings alone do
not opt a control in. `describeControl` cannot bypass this selection policy.
Each page can also declare `data-window-state-controls` on its own form or
section inside a shared workbench pane. The closest declaring region owns the
selection of its child controls, so pages need not modify the shared pane or
maintain a central field whitelist. Nested windows each declare their own
selection, independent of the containing window. For an intentionally broad,
known form, install options can explicitly set
`controlSelector: 'input,select,textarea'`. Precedence is whole-window descriptor
`false`, closest declaring region inside that window, descriptor selector,
window attribute, install option, then the explicit-field-only default.
The policy is applied before describing fields for capture, hydration and
semantic retirement. Malformed or empty selectors fail closed.

Provide `data-window-state-key` on each window and `data-window-state-field` on
each editable control. Stable IDs, names, binding attributes and aria labels
also work; DOM position and array index never identify a field. Ambiguous
duplicate field IDs are excluded. Radio groups use one named value, checkboxes
use booleans and multi-selects use arrays. File inputs, passwords, hidden,
disabled and read-only controls are excluded, including inherited disabled
fieldsets and controls inside inert regions. Unavailable select options keep
the form's current default. `data-window-state-ignore` on a control or ancestor
opts it out when an application-owned model adapter persists the form.

`describeWindow(element)` can return `{ key, scope, owner }`. Persistence uses
`[key, scope]`; `owner` cancels pending work when a project or model is replaced.
An existing visible window whose descriptor temporarily returns null is
suspended while its model loads. A close ends the session. Synchronous DOM
replacement with the same semantic identity continues the session. Windows
whose descriptor returns `ready: false` (or `suspended: true`) retain their
session identity across replacement while postponing hydration until ready.
Use this explicit readiness flag for windows rebuilt during asynchronous loads.
Fields
hydrate once per logical open, including conditional fields when they become
visible. Normal rerender never reapplies older saved defaults. Live user drafts
can transfer silently to replacement controls, without changing focus,
selection or scrolling.
When a field's semantic identity truly disappears (for example, switching pipe
profiles), its live hydration/draft state is retired and pending adapters become
stale. Its persisted value remains. When that identity returns, the normal
restoration adapter synchronizes both DOM and model again. Merely becoming
hidden, disabled or inert does not retire a field that remains in the DOM.

`describeControl(control, windowDescriptor)` can return
`{ key, read, apply, restore, dispatch, transfer }` or null. `restore: false` captures edits
without restoring; `dispatch: false` disables default event synchronization.
`transfer: false` disables silent live-draft transfer after DOM replacement,
while retaining persistence and restoration on the next opening. Use it for
model-owned selection controls whose values can be changed by a master/group
action; text controls retain normal draft protection.
Application data models should use the optional asynchronous
`restoreControl(control, value, context)` adapter. The DOM value is already
applied; the adapter can invoke existing model handlers and await the resulting
update. `context.previousValue` contains the typed DOM value immediately before
applying the remembered value. An adapter can compare normalized values and
avoid unnecessary model updates when the current value already matches.
Hydration awaits adapters sequentially. `context.dispatch()` sends
input/change events without recording them as fresh edits. For custom event
handlers, synchronously emitted events are also excluded from capture.
`context.isCurrent()` and `context.signal` guard deferred responses after user
input, node replacement, closing or owner changes. Actual user edits during a
pending adapter take priority. Callbacks `onCapture`, `onRestore` and `onError`
are optional. Call `dispose()` when the document owner is torn down.

For complex model-owned windows, use the storage API to load the draft before
rendering and capture the draft through model actions. Mark that window ignored
by delegated DOM memory; never restore controls independently from their model.
