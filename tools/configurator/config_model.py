"""Configuration validation shared by the local configurator prototype."""
from __future__ import annotations

import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DEFAULT_PATH = ROOT / 'default_config.json'
MAX_SOURCES, MAX_GAUGES, MAX_SECONDARIES = 10, 10, 3
GAUGE_TYPES = {'standard', 'shiftlight', 'gmeter', 'accelTimer'}

def load_default() -> dict:
    return json.loads(DEFAULT_PATH.read_text(encoding='utf-8'))

def default_copy() -> dict:
    return copy.deepcopy(load_default())

def validate(config: object) -> list[str]:
    errors: list[str] = []
    if not isinstance(config, dict): return ['Configuration must be a JSON object.']
    if config.get('version') != 1: errors.append('version must be 1.')
    sources, gauges = config.get('dataSources'), config.get('gauges')
    if not isinstance(sources, list): return errors + ['dataSources must be an array.']
    if not isinstance(gauges, list): return errors + ['gauges must be an array.']
    if len(sources) > MAX_SOURCES: errors.append(f'Use at most {MAX_SOURCES} data sources.')
    if len(gauges) > MAX_GAUGES: errors.append(f'Use at most {MAX_GAUGES} gauges.')
    ids: set[str] = set()
    for index, source in enumerate(sources):
        label=f'dataSources[{index}]'
        if not isinstance(source, dict): errors.append(f'{label} must be an object.'); continue
        identifier=source.get('id')
        if not isinstance(identifier, str) or not 1 <= len(identifier) <= 15: errors.append(f'{label}.id must be 1 to 15 characters.')
        elif identifier in ids: errors.append(f'Duplicate data source id: {identifier}.')
        else: ids.add(identifier)
        if source.get('type') not in {'obd','analog'}: errors.append(f'{label}.type must be obd or analog.')
        if source.get('type') == 'obd' and not isinstance(source.get('pid'), int): errors.append(f'{label}.pid must be an integer.')
        if source.get('type') == 'analog' and (not isinstance(source.get('pin'), int) or not isinstance(source.get('multiplier'), (int,float)) or not isinstance(source.get('offset'), (int,float))): errors.append(f'{label} needs numeric pin, multiplier, and offset fields.')
    for index, gauge in enumerate(gauges):
        label=f'gauges[{index}]'
        if not isinstance(gauge, dict): errors.append(f'{label} must be an object.'); continue
        kind=gauge.get('type')
        if kind not in GAUGE_TYPES: errors.append(f'{label}.type is not supported.'); continue
        if not isinstance(gauge.get('name'), str) or not gauge['name']: errors.append(f'{label}.name is required.')
        elif len(gauge['name']) > 31: errors.append(f'{label}.name must be at most 31 characters.')
        if len(str(gauge.get('unitLabel',''))) > 15: errors.append(f'{label}.unitLabel must be at most 15 characters.')
        if kind == 'standard':
            main=gauge.get('mainSourceId')
            if main not in ids: errors.append(f'{label}.mainSourceId must reference a data source.')
            if not isinstance(gauge.get('minVal'), (int,float)) or not isinstance(gauge.get('maxVal'), (int,float)) or gauge.get('minVal') >= gauge.get('maxVal'): errors.append(f'{label} needs a valid minVal and maxVal.')
            secondaries=gauge.get('secondaries', [])
            if not isinstance(secondaries, list) or len(secondaries) > MAX_SECONDARIES: errors.append(f'{label} supports at most {MAX_SECONDARIES} secondaries.')
            else:
                for secondary in secondaries:
                    if not isinstance(secondary, dict) or secondary.get('sourceId') not in ids: errors.append(f'{label} has a secondary with an unknown source.'); continue
                    if len(str(secondary.get('prefix',''))) > 15 or len(str(secondary.get('suffix',''))) > 7: errors.append(f'{label} secondary labels must be at most 15 characters and units at most 7.')
                    if secondary.get('rangeColors', False):
                        low, high = secondary.get('lowerThreshold', 0), secondary.get('upperThreshold', 100)
                        if not isinstance(low, (int,float)) or not isinstance(high, (int,float)) or low >= high: errors.append(f'{label} secondary colour thresholds must have lowerThreshold lower than upperThreshold.')
                        for field in ('colorBelow','colorBetween','colorAbove'):
                            if secondary.get(field) not in {'white','gray','blue','cyan','green','yellow','orange','red'}: errors.append(f'{label} secondary {field} is not supported.')
        if kind == 'shiftlight':
            targets=gauge.get('shiftTargets', [])
            if not isinstance(targets, list) or len(targets) != 6 or any(not isinstance(value, int) or value < 0 or value > 12000 for value in targets): errors.append(f'{label}.shiftTargets must contain six RPM values from 0 to 12000.')
        if kind == 'accelTimer':
            if gauge.get('mainSourceId') not in ids: errors.append(f'{label}.mainSourceId must reference a data source.')
            if not isinstance(gauge.get('minVal'), (int,float)) or not isinstance(gauge.get('maxVal'), (int,float)) or gauge.get('minVal') >= gauge.get('maxVal'): errors.append(f'{label} needs a valid start and finish speed.')
    return errors
