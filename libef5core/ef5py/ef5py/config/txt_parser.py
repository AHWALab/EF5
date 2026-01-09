"""
Legacy TXT configuration parser for ef5py.

Parses the original EF5 control file format for backward compatibility.

Format:
-------
[SectionName Key]
PARAM1=value1
PARAM2=value2

Sections include:
- [Basic] - DEM, DDM, FAM, PROJ
- [PrecipForcing Name] - TYPE, UNIT, LOC, NAME, FREQ
- [PETForcing Name]
- [Gauge Name] - lat, lon, cellx, celly, outputts, obs
"""

import re
from pathlib import Path
from .yaml_parser import Config, ForcingConfig, GaugeConfig


def parse_section_header(line: str) -> tuple:
    """
    Parse section header like [Basic] or [Gauge MyGauge].
    
    Returns (section_type, section_name or None)
    """
    match = re.match(r'\[(\w+)\s*(\w+)?\]', line.strip())
    if match:
        return match.group(1), match.group(2)
    return None, None


def parse_key_value_line(line: str) -> dict:
    """
    Parse a line with key=value pairs, separated by spaces.
    
    Example: "lat=22.5 lon=-83.5 outputts=true"
    """
    result = {}
    # Handle both space-separated and newline-separated
    parts = line.split()
    for part in parts:
        if '=' in part:
            key, value = part.split('=', 1)
            key = key.strip().lower()
            value = value.strip()
            
            # Convert to appropriate type
            if value.lower() == 'true':
                result[key] = True
            elif value.lower() == 'false':
                result[key] = False
            else:
                try:
                    if '.' in value:
                        result[key] = float(value)
                    else:
                        result[key] = int(value)
                except ValueError:
                    result[key] = value
    return result


def load_txt(path: str) -> Config:
    """
    Load configuration from legacy EF5 TXT control file.
    
    Parameters
    ----------
    path : str
        Path to .txt control file
        
    Returns
    -------
    Config
        Parsed configuration
    """
    config = Config()
    
    with open(path, 'r') as f:
        content = f.read()
    
    lines = content.split('\n')
    current_section = None
    current_name = None
    section_data = {}
    
    for line in lines:
        line = line.strip()
        
        # Skip empty lines and comments
        if not line or line.startswith('#'):
            continue
        
        # Check for section header
        if line.startswith('['):
            # Save previous section if any
            if current_section:
                _apply_section(config, current_section, current_name, section_data)
            
            current_section, current_name = parse_section_header(line)
            section_data = {}
            
            # Inline parameters (like gauge definitions)
            remainder = line[line.index(']')+1:].strip()
            if remainder:
                section_data.update(parse_key_value_line(remainder))
            continue
        
        # Parse key=value pairs
        if '=' in line:
            section_data.update(parse_key_value_line(line))
    
    # Apply final section
    if current_section:
        _apply_section(config, current_section, current_name, section_data)
    
    return config


def _apply_section(config: Config, section: str, name: str, data: dict):
    """Apply parsed section data to config object."""
    section = section.lower()
    
    if section == 'basic':
        config.dem = data.get('dem', '')
        config.fdir = data.get('ddm', data.get('fdir', ''))
        config.fac = data.get('fam', data.get('fac', ''))
        config.projection = data.get('proj', 'geographic')
        config.esri_ddm = data.get('esriddm', True)
        
    elif section == 'precipforcing':
        config.precipitation = ForcingConfig(
            type=data.get('type', 'tif'),
            location=data.get('loc', ''),
            pattern=data.get('name', ''),
            unit=data.get('unit', 'mm/hr'),
            frequency=data.get('freq', '1h')
        )
        
    elif section == 'petforcing':
        config.pet = ForcingConfig(
            type=data.get('type', 'tif'),
            location=data.get('loc', ''),
            pattern=data.get('name', ''),
            unit=data.get('unit', 'mm/day'),
            frequency=data.get('freq', 'monthly')
        )
        
    elif section == 'gauge':
        gauge = GaugeConfig(
            name=name or '',
            lat=data.get('lat'),
            lon=data.get('lon'),
            cellx=data.get('cellx'),
            celly=data.get('celly'),
            observe_file=data.get('obs'),
            output_ts=data.get('outputts', True)
        )
        config.gauges.append(gauge)
        
    elif section == 'crest':
        config.water_balance_model = 'CREST'
        # Parameter files
        for key in ['wm', 'b', 'im', 'ke', 'fc', 'iwu', 'ksat']:
            if key in data:
                config.crest_params[key] = data[key]
                
    elif section == 'kinematic' or section == 'kw':
        config.routing_model = 'kinematic'
        for key in ['alpha', 'alpha0', 'beta', 'threshold', 'leaki']:
            if key in data:
                config.kinematic_params[key] = data[key]
