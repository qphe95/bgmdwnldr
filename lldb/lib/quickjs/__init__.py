"""
QuickJS-specific debugging utilities.
"""

from lib.quickjs.constants import (
    JSObjectOffset,
    JSShapeOffset,
    JSValueTag,
    JSClassID,
    SUSPICIOUS_PATTERNS,
    MIN_VALID_PTR,
    MAX_VALID_PTR,
)
from lib.quickjs.types import JSObject, JSShape, JSValue
from lib.quickjs.inspector import ObjectInspector, InspectionResult
from lib.quickjs.corruption import CorruptionType, CorruptionReport, CorruptionDetector

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
