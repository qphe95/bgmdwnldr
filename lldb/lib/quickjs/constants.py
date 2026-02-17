"""
QuickJS structure offsets and magic numbers.
"""


# JSObject offsets (ARM64)
class JSObjectOffset:
    """Offsets within JSObject structure."""
    CLASS_ID = 0        # uint16_t
    FLAGS = 2           # uint8_t
    PADDING = 3         # uint8_t (alignment)
    WEAKREF_COUNT = 4   # uint32_t
    SHAPE = 8           # JSShape*
    PROP = 16           # JSProperty*
    SIZE = 24           # Total minimum size


# JSShape offsets
class JSShapeOffset:
    """Offsets within JSShape structure."""
    IS_HASHED = 0
    HASH = 4
    PROP_HASH_MASK = 8
    PROP_SIZE = 12
    PROP_COUNT = 16
    DELETED_PROP_COUNT = 20
    SHAPE_HASH_NEXT = 24
    PROTO = 32
    SIZE = 40


# JSValue tags
class JSValueTag:
    """QuickJS JSValue tag values."""
    INT = 0
    UNDEFINED = 4
    NULL = 5
    BOOL = 6
    EXCEPTION = 7
    FLOAT64 = 8
    OBJECT = -1  # 0xFFFFFFFFFFFFFFFF


# Class IDs
class JSClassID:
    """QuickJS built-in class IDs."""
    OBJECT = 1
    ARRAY = 2
    FUNCTION = 3
    ERROR = 4
    NUMBER = 5
    STRING = 6
    BOOLEAN = 7
    SYMBOL = 8
    REGEXP = 9
    DATE = 10
    MATH = 11
    JSON = 12
    PROXY = 13
    MAP = 14
    SET = 15
    WEAKMAP = 16
    WEAKSET = 17
    ARRAY_BUFFER = 18
    SHARED_ARRAY_BUFFER = 19
    UINT8C_ARRAY = 20
    INT8_ARRAY = 21
    UINT8_ARRAY = 22
    INT16_ARRAY = 23
    UINT16_ARRAY = 24
    INT32_ARRAY = 25
    UINT32_ARRAY = 26
    FLOAT32_ARRAY = 27
    FLOAT64_ARRAY = 28
    DATAVIEW = 29
    PROMISE = 30
    GENERATOR = 31


# Suspicious patterns indicating corruption
SUSPICIOUS_PATTERNS = [
    0xc0000000,              # Common tagged value (JS_TAG_OBJECT)
    0xc0000008,              # Tagged value variant
    0xFFFFFFFFFFFFFFFE,      # JS_TAG_INT upper bound
    0xFFFFFFFFFFFFFFFF,      # JS_TAG_OBJECT / -1
    0,                       # NULL pointer
    0x8,                     # Small integer
    0x10,                    # Small pointer
]

# Valid pointer range (userspace ARM64)
MIN_VALID_PTR = 0x1000
MAX_VALID_PTR = 0x0000FFFFFFFFFFFF

# Tag decoding
def decode_js_tag(value: int) -> str:
    """Decode a QuickJS tag from a value."""
    tag = value & 0xF
    tag_names = {
        0: "JS_TAG_INT",
        1: "JS_TAG_BOOL/UNDEFINED",
        2: "JS_TAG_NULL",
        3: "JS_TAG_EXCEPTION",
        4: "JS_TAG_CATCH_OFFSET",
        5: "JS_TAG_UNINITIALIZED",
        6: "JS_TAG_UNDEFINED",
        7: "JS_TAG_UNDEFINED2",
        8: "JS_TAG_FLOAT64",
    }
    return tag_names.get(tag, f"UNKNOWN_TAG_{tag}")


def is_valid_pointer(ptr: int) -> bool:
    """Check if value looks like a valid heap pointer."""
    if ptr is None:
        return False
    if ptr == 0:
        return False
    if ptr < MIN_VALID_PTR:
        return False
    if ptr > MAX_VALID_PTR:
        return False
    if ptr & 0x7:  # Must be 8-byte aligned
        return False
    return True


def looks_like_tagged_value(value: int) -> bool:
    """Check if value looks like a tagged JSValue."""
    # Tagged values have low bits set and high bits clear
    if value & 0xF == 0:
        return False  # Not tagged
    if (value >> 4) != 0:
        return False  # Has high bits set (not a small tagged value)
    return True
