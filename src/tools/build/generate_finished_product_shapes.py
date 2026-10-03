"""Bundle the standalone product catalogue for the browser's initial render."""
import json
from pathlib import Path

APP = Path(__file__).resolve().parents[2] / 'apps/tube-designer'
catalogue = json.loads((APP / 'templates/finished-product/shapes.json').read_text(encoding='utf-8'))
(APP / 'webpage/finishedProductShapes.generated.mjs').write_text(
    '// Generated from templates/finished-product/shapes.json. Regenerate with generate_finished_product_shapes.py.\n'
    + 'export default ' + json.dumps(catalogue, ensure_ascii=False, indent=2) + ';\n', encoding='utf-8')
