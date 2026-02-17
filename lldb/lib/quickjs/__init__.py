"""
QuickJS-specific debugging utilities.
"""

from .constants import (
    JSObjectOffset,
    JSShapeOffset,
    JSValueTag,
    SUSPICIOUS_PATTERNS,
    MIN_VALID_PTR,
    MAX_VALID_PTR,
)
from .types import JSObject, JSShape, JSValue, JSClassID
from .inspector import ObjectInspector, InspectionResult
from .corruption import CorruptionType, CorruptionReport, CorruptionDetector

__all__ = [
    'JSObjectOffset',
    'JSShapeOffset',
    'JSValueTag',
    'SUSPICIOUS_PATTERNS',
    'MIN_VALID_PTR',
    'MAX_VALID_PTR',
    'JSObject',
    'JSShape',
    'JSValue',
    'JSClassID',
    'ObjectInspector',
    'InspectionResult',
    'CorruptionType',
    'CorruptionReport',
    'CorruptionDetector',
]
