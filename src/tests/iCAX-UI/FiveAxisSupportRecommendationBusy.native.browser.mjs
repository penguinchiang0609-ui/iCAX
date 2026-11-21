process.env.ICAX_OPERATION_ACCEPTANCE='1';
process.env.ICAX_SUPPORT_RECOMMENDATION_BUSY_ACCEPTANCE='1';
process.env.ICAX_BROWSER_REPORT_DIRECTORY||='output/acceptance/support-recommendation-busy-20261105/browser';
process.env.ICAX_FIVE_AXIS_NATIVE_TEST_EXECUTABLE||='FiveAxisOperationCouplingTest.exe';
await import('./FiveAxisHeadWorkspace.native.browser.mjs');
