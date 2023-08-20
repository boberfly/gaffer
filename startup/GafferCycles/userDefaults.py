##########################################################################
#
#  Copyright (c) 2022, Cinesite VFX Ltd. All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are
#  met:
#
#      * Redistributions of source code must retain the above
#        copyright notice, this list of conditions and the following
#        disclaimer.
#
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following
#        disclaimer in the documentation and/or other materials provided with
#        the distribution.
#
#      * Neither the name of John Haddon nor the names of
#        any other contributors to this software may be used to endorse or
#        promote products derived from this software without specific prior
#        written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
#  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
#  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
#  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
#  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
#  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
#  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
##########################################################################

import Gaffer

# Correction for Blender's Z-up default mapping.

Gaffer.Metadata.registerValue( "cycles:surface:environment_texture:tex_mapping__scale.x", "userDefault", -1.0 )
Gaffer.Metadata.registerValue( "cycles:surface:environment_texture:tex_mapping__y_mapping", "userDefault", "z" )
Gaffer.Metadata.registerValue( "cycles:surface:environment_texture:tex_mapping__z_mapping", "userDefault", "y" )

Gaffer.Metadata.registerValue( "cycles:surface:sky_texture:tex_mapping__scale.x", "userDefault", -1.0 )
Gaffer.Metadata.registerValue( "cycles:surface:sky_texture:tex_mapping__y_mapping", "userDefault", "z" )
Gaffer.Metadata.registerValue( "cycles:surface:sky_texture:tex_mapping__z_mapping", "userDefault", "y" )

# Matches Blender's defaults for grid attribute names

Gaffer.Metadata.registerValue( "cycles:volume:principled_volume:density_attribute", "userDefault", "density" )
Gaffer.Metadata.registerValue( "cycles:volume:principled_volume:temperature_attribute", "userDefault", "temperature" )
