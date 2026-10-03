"""Sequential strict validation of the final native Update14 modules."""
from pathlib import Path
import importlib.util
import json
import sys

WORK = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('strict14', WORK / 'release_update06.py')
strict = importlib.util.module_from_spec(spec)
spec.loader.exec_module(strict)
strict.BASE = Path('E:/GILLPRODUCTION/Development14')
strict.build_root = lambda group: strict.BASE / group.removeprefix('GILL')

if __name__ == '__main__':
    catalog = json.loads((WORK / 'packaging/macos/products.json').read_text(encoding='utf-8'))
    assert len(catalog) == 60 and all(p['version'] == '0.14.0' for p in catalog)
    selected = [p for p in catalog if not sys.argv[1:] or p['target'] in sys.argv[1:]]
    assert selected and (not sys.argv[1:] or {p['target'] for p in selected} == set(sys.argv[1:]))
    results = []
    for product in selected:
        results.append(strict.validate(product))
    raise SystemExit(0 if all(results) else 1)
