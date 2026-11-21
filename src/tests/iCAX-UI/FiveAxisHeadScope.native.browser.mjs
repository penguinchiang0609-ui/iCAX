process.env.ICAX_HEAD_SCOPE_ACCEPTANCE='1';
process.env.ICAX_BROWSER_REPORT_DIRECTORY||='output/acceptance/head-scopes-20261104/browser';
await import('./FiveAxisHeadWorkspace.native.browser.mjs');
