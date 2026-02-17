"""
Pre-configured debug profiles.
"""

from .base import DebugProfile
from .comprehensive import ComprehensiveProfile
from .minimal import MinimalProfile
from .shape_only import ShapeOnlyProfile
from .register_focus import RegisterFocusProfile
from .stripped_binary import StrippedBinaryProfile

AVAILABLE_PROFILES = {
    'comprehensive': ComprehensiveProfile,
    'minimal': MinimalProfile,
    'shape_only': ShapeOnlyProfile,
    'shape': ShapeOnlyProfile,  # Alias
    'register_focus': RegisterFocusProfile,
    'register': RegisterFocusProfile,  # Alias
    'stripped': StrippedBinaryProfile,
    'stripped_binary': StrippedBinaryProfile,
}

__all__ = [
    'DebugProfile',
    'ComprehensiveProfile',
    'MinimalProfile',
    'ShapeOnlyProfile',
    'RegisterFocusProfile',
    'StrippedBinaryProfile',
    'AVAILABLE_PROFILES',
]
