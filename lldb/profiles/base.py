"""
Base class for debug profiles.
"""

from typing import List


class DebugProfile:
    """Base class for debug profiles."""
    
    name: str = "base"
    description: str = "Base debug profile"
    
    def configure(self, session):
        """Configure the debug session with modules.
        
        Override this method in subclasses to add modules.
        """
        raise NotImplementedError
    
    def get_info(self) -> dict:
        """Get profile information."""
        return {
            'name': self.name,
            'description': self.description,
        }
