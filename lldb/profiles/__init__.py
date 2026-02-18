"""
Pre-configured debug profiles.
"""

from profiles.base import DebugProfile
from profiles.comprehensive import ComprehensiveProfile
from profiles.minimal import MinimalProfile
from profiles.shape_only import ShapeOnlyProfile
from profiles.register_focus import RegisterFocusProfile
from profiles.stripped_binary import StrippedBinaryProfile

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
