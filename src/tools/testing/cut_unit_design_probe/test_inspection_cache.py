"""Display export updates must invalidate cache even for unchanged cut geometry."""
import unittest
from run_unit_inspection import matches_exporter


class DisplayCacheTests(unittest.TestCase):
    def test_unchanged_geometry_does_not_reuse_old_display_exporter(self):
        report={'passed':True,'sourceHash':'same-source','inspectionSha256':'same-existing-step',
                'inspectionExporterSha256':'old-pipe-exporter'}
        self.assertFalse(matches_exporter(report,'sampled-display-exporter'))

    def test_old_report_without_producer_does_not_infer_it_from_new_stamp(self):
        report={'passed':True,'sourceHash':'same-source','inspectionSha256':'old-step'}
        self.assertFalse(matches_exporter(report,'current-exporter'))

    def test_verified_current_display_export_can_be_reused(self):
        self.assertTrue(matches_exporter({'passed':True,'inspectionExporterSha256':'current'},'current'))

    def test_failed_readback_is_never_reused(self):
        self.assertFalse(matches_exporter({'passed':False,'inspectionExporterSha256':'current'},'current'))


if __name__=='__main__':unittest.main(verbosity=2)
