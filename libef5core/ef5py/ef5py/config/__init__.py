"""
Configuration loading and parsing for ef5py.

Supports:
- YAML: Modern configuration format
- TXT: Legacy EF5 control file format (backward compatibility)
"""

from .yaml_parser import load_yaml, Config, ForcingConfig, GaugeConfig
from .txt_parser import load_txt

def load_config(path: str):
    """
    Load configuration from file.
    
    Automatically detects format based on extension:
    - .yaml, .yml -> YAML format
    - .txt, .ctl, .control -> Legacy EF5 format
    
    Parameters
    ----------
    path : str
        Path to configuration file
        
    Returns
    -------
    Config
        Parsed configuration object
    """
    import os
    ext = os.path.splitext(path)[1].lower()
    
    if ext in ('.yaml', '.yml'):
        return load_yaml(path)
    elif ext in ('.txt', '.ctl', '.control'):
        return load_txt(path)
    else:
        # Try YAML first, fall back to TXT
        try:
            return load_yaml(path)
        except:
            return load_txt(path)

__all__ = ["load_config", "load_yaml", "load_txt", "Config", "ForcingConfig", "GaugeConfig"]

