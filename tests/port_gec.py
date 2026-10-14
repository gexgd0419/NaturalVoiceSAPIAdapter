"""Generate constant tables from the pinned edge-tts 7.3.0 reference."""
import ast
from pathlib import Path

root = Path(__file__).resolve().parent.parent
tree = ast.parse((root / 'tests/reference/drm.py').read_text())
tables = {}
for node in tree.body:
    if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
        try:
            tables[node.targets[0].id] = ast.literal_eval(node.value)
        except (ValueError, TypeError):
            pass
names = {'_SHA256_INITIAL': 'Initial', '_SHA256_STATE': 'ResumeState',
         '_SHA256_SCHEDULE': 'Schedule', '_SHA256_ROUND_CONSTANTS': 'Rounds',
         '_CS64_COEFFICIENTS': 'Coefficients'}
out = '// Constants from edge-tts 7.3.0 drm.py (LGPL-3.0). See tests/reference.\n'
for key, name in names.items():
    values = tables[key]
    if isinstance(values[0], tuple):
        out += f'static constexpr uint32_t {name}[][2] = {{\n'
        out += ''.join(f'    {{0x{a:08X}, 0x{b:08X}}},\n' for a, b in values)
    else:
        out += f'static constexpr uint32_t {name}[] = {{\n'
        out += ''.join(f'    0x{v:08X},\n' for v in values)
    out += '};\n'
(root / 'NaturalVoiceSAPIAdapter/EdgeGecTables.h').write_text(out)
