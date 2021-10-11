try:
    from .client import HGDBClient
    from .symbol import (SymbolTableProvider, VariableSymbol, GeneratorVariableSymbol, ContextVariableSymbol,
                         BreakpointSymbol)
except ImportError:
    pass

from .db import DebugSymbolTable
