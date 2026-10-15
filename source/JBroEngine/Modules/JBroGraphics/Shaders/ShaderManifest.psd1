# The one list of built-in shaders (D-329, postprocess-plan section 2.4).
#
# Compile.ps1 reads this file. It bakes every stage to DXIL (D3D12), DXBC (D3D11) and SPIR-V (Vulkan) as
# <Symbol>.generated.h, <Symbol>_SM5.generated.h and <Symbol>_SPV.generated.h next to Source/, and writes
# BuiltinShaders.generated.h/.cpp: the program table the renderer reads (GetBuiltinShader) instead of including
# every header and spelling every binding count by hand.
#
# Adding a shader is one edit here plus a run of Compile.ps1 - not three arrays and three include blocks.
#
# Stages     one entry point each. Stage is Vertex, Pixel or Compute.
# Programs   what a pipeline uses: its stages, push constant size and stages, and the binding layout (RHI.h
#            BindingLayoutDesc: b1.. constant buffers, t0.. textures, s0.. samplers). Texture and sampler stages
#            default to Pixel. BuiltinShaderTests checks every program against the shader's own reflection.
@{
    Stages = @(
        @{ Symbol = 'BuiltinSpriteVS';           File = 'BuiltinSprite.hlsl';           Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinSpritePS';           File = 'BuiltinSprite.hlsl';           Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinSpriteLitPS';        File = 'BuiltinSprite.hlsl';           Entry = 'PSLitMain';      Stage = 'Pixel' }
        @{ Symbol = 'BuiltinMeshVS';             File = 'BuiltinMesh.hlsl';             Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinMeshPS';             File = 'BuiltinMesh.hlsl';             Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinSdfTextVS';          File = 'BuiltinSdfText.hlsl';          Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinSdfTextPS';          File = 'BuiltinSdfText.hlsl';          Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinSdfTextLitPS';       File = 'BuiltinSdfText.hlsl';          Entry = 'PSLitMain';      Stage = 'Pixel' }
        @{ Symbol = 'BuiltinWorldTextVS';        File = 'BuiltinWorldText.hlsl';        Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinWorldTextPS';        File = 'BuiltinWorldText.hlsl';        Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinOutlineVS';          File = 'BuiltinOutlineGrow.hlsl';      Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinOutlineGrowPS';      File = 'BuiltinOutlineGrow.hlsl';      Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinOutlineCompositePS'; File = 'BuiltinOutlineComposite.hlsl'; Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLayerCompositePS';   File = 'BuiltinLayerComposite.hlsl';   Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLayerBackdropPS';    File = 'BuiltinLayerBackdrop.hlsl';    Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLight2DVS';          File = 'BuiltinLight2D.hlsl';          Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinLight2DPS';          File = 'BuiltinLight2D.hlsl';          Entry = 'PSMain';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLight2DShadowedPS';  File = 'BuiltinLight2D.hlsl';          Entry = 'PSShadowedMain'; Stage = 'Pixel' }
        @{ Symbol = 'BuiltinShadow2DVS';         File = 'BuiltinShadow2D.hlsl';         Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinShadow2DPS';         File = 'BuiltinShadow2D.hlsl';         Entry = 'PSMain';         Stage = 'Pixel' }
    )

    Programs = @(
        # Sprites: a view-projection matrix in the vertex push block; the atlas (and the light map when lit).
        @{ Name = 'Sprite';           Vertex = 'BuiltinSpriteVS';    Pixel = 'BuiltinSpritePS';           PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1; Samplers = 1 }
        @{ Name = 'SpriteLit';        Vertex = 'BuiltinSpriteVS';    Pixel = 'BuiltinSpriteLitPS';        PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 2; Samplers = 1 }
        @{ Name = 'SdfText';          Vertex = 'BuiltinSdfTextVS';   Pixel = 'BuiltinSdfTextPS';          PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1; Samplers = 1 }
        @{ Name = 'SdfTextLit';       Vertex = 'BuiltinSdfTextVS';   Pixel = 'BuiltinSdfTextLitPS';       PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 2; Samplers = 1 }
        @{ Name = 'WorldText';        Vertex = 'BuiltinWorldTextVS'; Pixel = 'BuiltinWorldTextPS';        PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1; Samplers = 1 }
        @{ Name = 'Mesh';             Vertex = 'BuiltinMeshVS';      Pixel = 'BuiltinMeshPS';             PushConstantBytes = 64; PushConstantStages = 'Vertex' }
        # Full screen passes over a quad: the selection outline and layer compositing read their inputs in the pixel stage.
        @{ Name = 'OutlineGrow';      Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinOutlineGrowPS';      PushConstantBytes = 32; PushConstantStages = 'Pixel';  SampledTextures = 1 }
        @{ Name = 'OutlineComposite'; Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinOutlineCompositePS'; PushConstantBytes = 32; PushConstantStages = 'Pixel';  SampledTextures = 2 }
        @{ Name = 'LayerComposite';   Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinLayerCompositePS';   PushConstantBytes = 16; PushConstantStages = 'Pixel';  SampledTextures = 1 }
        @{ Name = 'LayerBackdrop';    Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinLayerBackdropPS';    PushConstantBytes = 16; PushConstantStages = 'Pixel';  SampledTextures = 2 }
        # 2D lighting: the light map pass, its shadowed variant reading the shadow mask, and the mask pass itself.
        @{ Name = 'Light2D';          Vertex = 'BuiltinLight2DVS';   Pixel = 'BuiltinLight2DPS';          PushConstantBytes = 64; PushConstantStages = 'Vertex' }
        @{ Name = 'Light2DShadowed';  Vertex = 'BuiltinLight2DVS';   Pixel = 'BuiltinLight2DShadowedPS';  PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1 }
        @{ Name = 'Shadow2D';         Vertex = 'BuiltinShadow2DVS';  Pixel = 'BuiltinShadow2DPS';         PushConstantBytes = 80; PushConstantStages = 'Vertex' }
    )
}
