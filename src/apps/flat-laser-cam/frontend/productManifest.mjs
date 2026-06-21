import { createFlatLaserCamMockBackend } from './mockBackend.mjs';

export const flatLaserCamProductManifest = Object.freeze({
  productId: 'icax.flat-laser-cam',
  productName: 'Flat Laser CAM',
  productVersion: '0.1.0',
  frontendEntry: 'apps/flat-laser-cam/frontend/entry.mjs',
  defaultProjectStartupComponent: 'CFlatLaserProjectComponent',
  projectFile: Object.freeze({
    magic: 'ICAX_FLAT_LASER_CAM',
    formatVersion: 1,
    fileExtensions: Object.freeze(['.ilcam']),
    magicOffset: 0,
    probeBytes: 64,
  }),
});

export function createFlatLaserCamMockProduct() {
  return {
    ...flatLaserCamProductManifest,
    isStarted: false,
    productMailId: '',
    recentProjects: [
      {
        path: 'D:/Projects/laser/demo-sheet.ilcam',
        displayName: 'demo-sheet.ilcam',
        lastOpenedAt: '2026-06-20T09:00:00+08:00',
      },
    ],
    mockBackend: createFlatLaserCamMockBackend(),
  };
}

export { createFlatLaserCamMockProduct as createMockProduct };
