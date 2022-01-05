try:
    from .client import HGDBClient, HGDBClientException
    from .symbol import (SymbolTableProvider, VariableSymbol, GeneratorVariableSymbol, ContextVariableSymbol,
                         BreakpointSymbol)
except ImportError:
    pass

from .db import DebugSymbolTable
