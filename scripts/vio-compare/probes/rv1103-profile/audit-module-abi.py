#!/usr/bin/env python3
"""Compare kernel module layouts in DWARF with shipped .ko section sizes."""
import argparse
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

def layouts(path):
    result = {}
    with path.open('rb') as file:
        dwarf = ELFFile(file).get_dwarf_info()
        for unit in dwarf.iter_CUs():
            for die in unit.iter_DIEs():
                if die.tag != 'DW_TAG_structure_type':
                    continue
                name, size = die.attributes.get('DW_AT_name'), die.attributes.get('DW_AT_byte_size')
                if not name or not size or name.value not in (b'module', b'module_layout', b'task_struct'):
                    continue
                fields = {}
                for child in die.iter_children():
                    if child.tag == 'DW_TAG_member' and all(key in child.attributes for key in ('DW_AT_name', 'DW_AT_data_member_location')):
                        fields[child.attributes['DW_AT_name'].value.decode()] = child.attributes['DW_AT_data_member_location'].value
                result[name.value.decode()] = dict(size=size.value, fields=fields)
            if 'module' in result and 'module_layout' in result:
                return result
    raise ValueError(f'Module layout not found: {path}')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('old_object', type=Path)
parser.add_argument('new_object', type=Path)
parser.add_argument('module_directory', type=Path)
args = parser.parse_args()
result = dict(old=layouts(args.old_object), new=layouts(args.new_object), modules={})
for path in sorted(args.module_directory.glob('*.ko')):
    with path.open('rb') as file:
        elf = ELFFile(file)
        section = elf.get_section_by_name('.gnu.linkonce.this_module')
        modinfo = elf.get_section_by_name('.modinfo')
        if section:
            result['modules'][path.name] = dict(module_section_size=section['sh_size'],
                vermagic=[x.decode() for x in modinfo.data().split(b'\0') if x.startswith(b'vermagic=')])
if not result['modules']:
    raise SystemExit(f'No .ko files found in {args.module_directory}')
result['module_layout_compatible'] = all(
    row['module_section_size'] == result['new']['module']['size']
    for row in result['modules'].values())
print(json.dumps(result, indent=2))
raise SystemExit(0 if result['module_layout_compatible'] else 1)
