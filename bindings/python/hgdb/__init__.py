try:
    from .client import HGDBClient
except ImportError:
    pass

from .db import DebugSymbolTable
