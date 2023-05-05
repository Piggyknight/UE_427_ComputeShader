// Fill out your copyright notice in the Description page of Project Settings.


#include "comp_parallel_reduction.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "ShaderCompilerCore.h"
#include "Engine/TextureRenderTarget2D.h"


class FParallelReductionMipsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FParallelReductionMipsCS);
	SHADER_USE_PARAMETER_STRUCT(FParallelReductionMipsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float>, InTex)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutResult)
			//SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint32>, OutResult)
			SHADER_PARAMETER(int, SrcWidth)
			SHADER_PARAMETER(int, SrcHeight)
			SHADER_PARAMETER(int, IsCompress)
	END_SHADER_PARAMETER_STRUCT()
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		//OutEnvironment.SetDefine(TEXT("THREADS_X"), FComputeShaderUtils::kGolden2DGroupSize);
		//OutEnvironment.SetDefine(TEXT("THREADS_Y"), FComputeShaderUtils::kGolden2DGroupSize);
		//OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FParallelReductionMipsCS, "/CustomShaders/parallel_reduction.usf", "main", SF_Compute);

void UCompParallelReduction::BeginDestroy()
{
	UObject::BeginDestroy();

	if(ReadbackRt!=nullptr)
	{
		ReadbackRt->RemoveFromRoot();
		ReadbackRt = nullptr;
	}
	ReadBackBuffer.Reset();
	IsRenderTargetInited = false;
	
	
	ENQUEUE_RENDER_COMMAND(WaitRHIThreadFenceForDynamicData)(
		[this](FRHICommandListImmediate& RHICmdList){
			this->ReleaseRhi();
		});
}

bool UCompParallelReduction::IsReadyForFinishDestroy()
{
	//destory until rhi thread finish
	return UObject::IsReadyForFinishDestroy() && !IsRhiInited;
}

void UCompParallelReduction::InitRenderTarget(UTextureRenderTarget2D* RenderTarget)
{
	if(IsRenderTargetInited)
		return;

	int s = 4;
	int SrcWidth = RenderTarget->SizeX;
	int SrcHeight = RenderTarget->SizeY;

	//create mipmap chain
	while (SrcWidth > 1 && SrcHeight > 1)
	{
		FReductionRes Res;
		Res.Size.X = SrcWidth;
		Res.Size.Y = SrcHeight;

		SrcWidth = FMath::Max(SrcWidth >> 2, 1);
		SrcHeight = FMath::Max(SrcHeight >> 2, 1);
		int dstWidth = s*FMath::DivideAndRoundUp(SrcWidth, s);
		int dstHeight = s*FMath::DivideAndRoundUp(SrcHeight, s);
		Res.DstSize.X = dstWidth;
		Res.DstSize.Y = dstHeight;

		ResArray.Add(Res);
		UE_LOG(LogTemp, Log, TEXT("Create Mip map RednerTarget: src : %d, %d, dst: %d, %d")
				, SrcWidth
				, SrcHeight
				, dstWidth
				, dstHeight);
	}

	UE_LOG(LogTemp, Log, TEXT("Create Total Rt Num: %d"), ResArray.Num());

	//only the last mip need render target for read back
	FReductionRes& Res = ResArray.Last();
	ReadbackRt = NewObject<UTextureRenderTarget2D>();
	ReadbackRt->AddToRoot();
	ReadbackRt->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f);
	ReadbackRt->ClearColor.A = 1.0f;
	ReadbackRt->TargetGamma = 0.0f;
	ReadbackRt->bAutoGenerateMips = false;
	ReadbackRt->InitCustomFormat(Res.DstSize.X
								, Res.DstSize.Y
								, PF_R32_FLOAT
								, true);

	IsRenderTargetInited = true;
}

void UCompParallelReduction::InitDynamicRhi(FRHICommandListImmediate& RHICmdList)
{
	//1. deffence
	if(!IsRenderTargetInited || IsRhiInited)
		return;

	IsRhiInited = true;

	//2. Loop through all res to create IPoolRenderTarget
	for(auto& Res : ResArray)
	{
		FIntPoint Ext = FIntPoint(Res.DstSize.X, Res.DstSize.Y);
		auto Desc = FPooledRenderTargetDesc::Create2DDesc(Ext
														, ReadbackRt->GetFormat()
														, FClearValueBinding::Black
														, TexCreate_ShaderResource
														, TexCreate_ShaderResource | TexCreate_UAV
														, false);
		
		FPooledRenderTargetDesc ComputeShaderOutputDesc(Desc);
		ComputeShaderOutputDesc.DebugName = TEXT("ParallelReductionRt");
		GRenderTargetPool.FindFreeElement(RHICmdList
											, ComputeShaderOutputDesc
											, Res.PooledMipRt
											, TEXT("ParallelReductionRt"));
	}

	//3. create IPooledRenderTarget for ReadbackRt
	if(!PooledReadbackRt.IsValid())
	{
		PooledReadbackRt = CreateRenderTarget(ReadbackRt->GetRenderTargetResource()->TextureRHI
											, *ReadbackRt->GetName());
	}
		
}

void UCompParallelReduction::ReleaseRhi()
{
	for (auto& Res : ResArray)
	{
		Res.RdgRt = nullptr;
		Res.PooledMipRt.SafeRelease();
	}
	ResArray.Reset();

	PooledReadbackRt.SafeRelease();
	RdgReadbackRt = nullptr;
	IsRhiInited = false;
	
}

void UCompParallelReduction::ParallelReductionSum(FRDGBuilder& GraphBuilder, FRDGTextureRef Texture)
{
	if(!IsRenderTargetInited || !IsRhiInited)
		return;
	
	for (int i=0; i< ResArray.Num(); ++i)
	{
		FReductionRes& Res = ResArray[i];
		Res.RdgRt = GraphBuilder.RegisterExternalTexture(Res.PooledMipRt
																, TEXT("RDG_Mip_OUT_RT")
																, ERenderTargetTexture::ShaderResource
																, ERDGTextureFlags::MultiFrame);

		TShaderMapRef<FParallelReductionMipsCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FParallelReductionMipsCS::FParameters* MipPassParameters = GraphBuilder.AllocParameters<FParallelReductionMipsCS::FParameters>();
		if(i == 0)
		{
			MipPassParameters->InTex = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Texture));
			MipPassParameters->IsCompress = 1;
		}
		else
		{
			MipPassParameters->IsCompress = -1;
			MipPassParameters->InTex = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ResArray[i-1].RdgRt));
		}
		
		MipPassParameters->SrcWidth = Res.Size.X;
		MipPassParameters->SrcHeight = Res.Size.Y;
		
		MipPassParameters->OutResult = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(Res.RdgRt));

		FIntPoint DestTextureSize = FIntPoint(Res.DstSize.X, Res.DstSize.Y);

		FComputeShaderUtils::AddPass(GraphBuilder,
									RDG_EVENT_NAME("Parallel Reduction"),
									ComputeShader,
									MipPassParameters,
									FComputeShaderUtils::GetGroupCount(DestTextureSize, 4));

	}

	RdgReadbackRt = GraphBuilder.RegisterExternalTexture(PooledReadbackRt
															, TEXT("RDG_Readback_RT")
															, ERenderTargetTexture::ShaderResource
															, ERDGTextureFlags::MultiFrame);
	FReductionRes LastRes = ResArray.Last();
	FRHICopyTextureInfo CopyInfo;
	CopyInfo.Size = FIntVector(ReadbackRt->SizeX, ReadbackRt->SizeY, 1);
	AddCopyTexturePass(GraphBuilder,LastRes.RdgRt, RdgReadbackRt, CopyInfo);
	
	FReadSurfaceDataFlags ReadDataFlags;
	ReadDataFlags.SetLinearToGamma(false);
	ReadDataFlags.SetOutputStencil(false);
	ReadDataFlags.SetMip(0);

	FIntPoint Extent = FIntPoint(ReadbackRt->SizeX, ReadbackRt->SizeY);

	AddReadbackTexturePass(GraphBuilder
		, RDG_EVENT_NAME("DirtnessReadBack")
		, RdgReadbackRt
		, [this, Extent, ReadDataFlags](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.ReadSurfaceData(ReadbackRt->GetRenderTargetResource()->TextureRHI
										, FIntRect(0, 0, 1, 1)
										, this->ReadBackBuffer
										, ReadDataFlags);
			
			for (auto& Color : this->ReadBackBuffer)
			{
				if(Color.R > 0.0f)
					UE_LOG(LogTemp, Log, TEXT("Color: %d, %d, %d"), Color.R, Color.G, Color.B)
			}
		});
}
