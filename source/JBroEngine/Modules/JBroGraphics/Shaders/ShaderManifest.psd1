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
        @{ Symbol = 'BuiltinMeshUnlitPS';        File = 'BuiltinMesh.hlsl';             Entry = 'PSUnlitMain';    Stage = 'Pixel' }
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
        @{ Symbol = 'BuiltinPostVS';             File = 'BuiltinPostProcess.hlsl';      Entry = 'VSMain';         Stage = 'Vertex' }
        @{ Symbol = 'BuiltinPostCopyPS';         File = 'BuiltinPostProcess.hlsl';      Entry = 'PSCopy';         Stage = 'Pixel' }
        @{ Symbol = 'BuiltinPostExposurePS';     File = 'BuiltinPostProcess.hlsl';      Entry = 'PSExposure';     Stage = 'Pixel' }
        @{ Symbol = 'BuiltinPostTonemapPS';      File = 'BuiltinPostProcess.hlsl';      Entry = 'PSTonemap';      Stage = 'Pixel' }
        @{ Symbol = 'BuiltinBloomPrefilterPS';   File = 'BuiltinBloom.hlsl';            Entry = 'PSPrefilter';    Stage = 'Pixel' }
        @{ Symbol = 'BuiltinBloomDownsamplePS';  File = 'BuiltinBloom.hlsl';            Entry = 'PSDownsample';   Stage = 'Pixel' }
        @{ Symbol = 'BuiltinBloomUpsamplePS';    File = 'BuiltinBloom.hlsl';            Entry = 'PSUpsample';     Stage = 'Pixel' }
        @{ Symbol = 'BuiltinBloomCompositePS';   File = 'BuiltinBloom.hlsl';            Entry = 'PSComposite';    Stage = 'Pixel' }
        @{ Symbol = 'BuiltinColorAdjustPS';      File = 'BuiltinColorGrading.hlsl';     Entry = 'PSAdjust';       Stage = 'Pixel' }
        @{ Symbol = 'BuiltinColorLookupPS';      File = 'BuiltinColorGrading.hlsl';     Entry = 'PSLookup';       Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLayerBlurPS';              File = 'BuiltinLayerEffects.hlsl'; Entry = 'PSBlur';             Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLayerOutlineDistancePS';   File = 'BuiltinLayerEffects.hlsl'; Entry = 'PSOutlineDistance';  Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLayerOutlineCompositePS';  File = 'BuiltinLayerEffects.hlsl'; Entry = 'PSOutlineComposite'; Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLayerShadowBlurPS';        File = 'BuiltinLayerEffects.hlsl'; Entry = 'PSShadowBlur';       Stage = 'Pixel' }
        @{ Symbol = 'BuiltinLayerShadowCompositePS';   File = 'BuiltinLayerEffects.hlsl'; Entry = 'PSShadowComposite';  Stage = 'Pixel' }
    )

    Programs = @(
        # Sprites: a view-projection matrix in the vertex push block; the atlas (and the light map when lit).
        @{ Name = 'Sprite';           Vertex = 'BuiltinSpriteVS';    Pixel = 'BuiltinSpritePS';           PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1; Samplers = 1 }
        @{ Name = 'SpriteLit';        Vertex = 'BuiltinSpriteVS';    Pixel = 'BuiltinSpriteLitPS';        PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 2; Samplers = 1 }
        @{ Name = 'SdfText';          Vertex = 'BuiltinSdfTextVS';   Pixel = 'BuiltinSdfTextPS';          PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1; Samplers = 1 }
        @{ Name = 'SdfTextLit';       Vertex = 'BuiltinSdfTextVS';   Pixel = 'BuiltinSdfTextLitPS';       PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 2; Samplers = 1 }
        @{ Name = 'WorldText';        Vertex = 'BuiltinWorldTextVS'; Pixel = 'BuiltinWorldTextPS';        PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1; Samplers = 1 }
        # Meshes: the standard material (D-402) - b1 the material block, b2 the view's 3D lights (D-367), t0..t4 its pictures.
        @{ Name = 'Mesh';             Vertex = 'BuiltinMeshVS';      Pixel = 'BuiltinMeshPS';             PushConstantBytes = 80; PushConstantStages = 'Vertex|Pixel'; ConstantBuffers = 2; ConstantBufferStages = 'Pixel'; SampledTextures = 5; Samplers = 1 }
        @{ Name = 'MeshUnlit';        Vertex = 'BuiltinMeshVS';      Pixel = 'BuiltinMeshUnlitPS';        PushConstantBytes = 80; PushConstantStages = 'Vertex'; ConstantBuffers = 1; ConstantBufferStages = 'Pixel'; SampledTextures = 2; Samplers = 1 }
        # Full screen passes over a quad: the selection outline and layer compositing read their inputs in the pixel stage.
        @{ Name = 'OutlineGrow';      Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinOutlineGrowPS';      PushConstantBytes = 32; PushConstantStages = 'Pixel';  SampledTextures = 1 }
        @{ Name = 'OutlineComposite'; Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinOutlineCompositePS'; PushConstantBytes = 32; PushConstantStages = 'Pixel';  SampledTextures = 2 }
        @{ Name = 'LayerComposite';   Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinLayerCompositePS';   PushConstantBytes = 16; PushConstantStages = 'Pixel';  SampledTextures = 1 }
        @{ Name = 'LayerBackdrop';    Vertex = 'BuiltinOutlineVS';   Pixel = 'BuiltinLayerBackdropPS';    PushConstantBytes = 16; PushConstantStages = 'Pixel';  SampledTextures = 2 }
        # 2D lighting: the light map pass, its shadowed variant reading the shadow mask, and the mask pass itself.
        @{ Name = 'Light2D';          Vertex = 'BuiltinLight2DVS';   Pixel = 'BuiltinLight2DPS';          PushConstantBytes = 64; PushConstantStages = 'Vertex' }
        @{ Name = 'Light2DShadowed';  Vertex = 'BuiltinLight2DVS';   Pixel = 'BuiltinLight2DShadowedPS';  PushConstantBytes = 64; PushConstantStages = 'Vertex'; SampledTextures = 1 }
        @{ Name = 'Shadow2D';         Vertex = 'BuiltinShadow2DVS';  Pixel = 'BuiltinShadow2DPS';         PushConstantBytes = 80; PushConstantStages = 'Vertex' }
        # Post-process (D-342): a full screen triangle from the vertex index; the pass reads its input with Load and the effect's
        # parameters from b1. No push constants.
        @{ Name = 'PostCopy';         Vertex = 'BuiltinPostVS';      Pixel = 'BuiltinPostCopyPS';         SampledTextures = 1 }
        @{ Name = 'PostExposure';     Vertex = 'BuiltinPostVS';      Pixel = 'BuiltinPostExposurePS';     SampledTextures = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostTonemap';      Vertex = 'BuiltinPostVS';      Pixel = 'BuiltinPostTonemapPS';      SampledTextures = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        # Bloom (D-343): a raster mip chain sampled with a linear clamp sampler; the upsample and composite read two textures.
        @{ Name = 'PostBloomPrefilter';  Vertex = 'BuiltinPostVS';   Pixel = 'BuiltinBloomPrefilterPS';   SampledTextures = 1; Samplers = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostBloomDownsample'; Vertex = 'BuiltinPostVS';   Pixel = 'BuiltinBloomDownsamplePS';  SampledTextures = 1; Samplers = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostBloomUpsample';   Vertex = 'BuiltinPostVS';   Pixel = 'BuiltinBloomUpsamplePS';    SampledTextures = 2; Samplers = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostBloomComposite';  Vertex = 'BuiltinPostVS';   Pixel = 'BuiltinBloomCompositePS';   SampledTextures = 2; Samplers = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        # Colour grading (D-343): adjustments read the input with Load; the lookup samples a 3D LUT at t1.
        @{ Name = 'PostColorAdjust';     Vertex = 'BuiltinPostVS';   Pixel = 'BuiltinColorAdjustPS';      SampledTextures = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostColorLookup';     Vertex = 'BuiltinPostVS';   Pixel = 'BuiltinColorLookupPS';      SampledTextures = 2; Samplers = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        # Layer effects (D-345): blur, outline and drop shadow on a layer's premultiplied texture, read with Load. The composites read the layer at t1.
        @{ Name = 'PostLayerBlur';             Vertex = 'BuiltinPostVS'; Pixel = 'BuiltinLayerBlurPS';             SampledTextures = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostLayerOutlineDistance';  Vertex = 'BuiltinPostVS'; Pixel = 'BuiltinLayerOutlineDistancePS';  SampledTextures = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostLayerOutlineComposite'; Vertex = 'BuiltinPostVS'; Pixel = 'BuiltinLayerOutlineCompositePS'; SampledTextures = 2; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostLayerShadowBlur';       Vertex = 'BuiltinPostVS'; Pixel = 'BuiltinLayerShadowBlurPS';       SampledTextures = 1; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
        @{ Name = 'PostLayerShadowComposite';  Vertex = 'BuiltinPostVS'; Pixel = 'BuiltinLayerShadowCompositePS';  SampledTextures = 2; ConstantBuffers = 1; ConstantBufferStages = 'Pixel' }
    )
}
