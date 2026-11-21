process.env.ICAX_OPERATION_ACCEPTANCE='1';
process.env.ICAX_BROWSER_REPORT_DIRECTORY||='output/acceptance/operations-ui-dedup-20261104/browser';
process.env.ICAX_FIVE_AXIS_NATIVE_TEST_EXECUTABLE||='FiveAxisOperationCouplingTest.exe';
await import('./FiveAxisHeadWorkspace.native.browser.mjs');
